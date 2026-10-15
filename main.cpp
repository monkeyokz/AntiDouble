#include <windows.h>
#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <string>
#include <atomic>

static HHOOK g_hook = nullptr;
static LARGE_INTEGER g_freq;

static std::atomic<bool> g_running(true);
static std::atomic<bool> g_is_down(false);
static std::atomic<bool> g_is_dragging(false);
static std::atomic<bool> g_blocked_current(false);
static std::atomic<bool> g_pending_release(false);

static uint64_t g_last_down = 0;
static uint64_t g_last_up = 0;
static uint64_t g_last_up_attempt = 0;
static POINT g_down_pos = {0, 0};

static uint64_t g_threshold_ms = 80;
static uint64_t g_drag_bridge_ms = 50;
static uint64_t g_click_bridge_ms = 25;

static std::mutex g_release_mutex;
static std::condition_variable g_release_cv;
static uint64_t g_target_release_time = 0;

static std::mutex g_log_mutex;
static std::condition_variable g_log_cv;
static std::queue<std::string> g_log_queue;

static inline uint64_t GetTimeMs() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>((counter.QuadPart * 1000) / g_freq.QuadPart);
}

static void LogWorker() {
    while (g_running) {
        std::unique_lock<std::mutex> lock(g_log_mutex);
        g_log_cv.wait(lock, [] { return !g_log_queue.empty() || !g_running; });

        while (!g_log_queue.empty()) {
            std::string msg = std::move(g_log_queue.front());
            g_log_queue.pop();
            lock.unlock();
            std::cout << msg << std::flush;
            lock.lock();
        }
    }
}

static void PostLog(const std::string& msg) {
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_queue.push(msg);
    }
    g_log_cv.notify_one();
}

static void ReleaseWorker() {
    while (g_running) {
        std::unique_lock<std::mutex> lock(g_release_mutex);
        g_release_cv.wait(lock, [] { return g_pending_release.load() || !g_running; });

        if (!g_running) break;

        while (g_pending_release.load() && g_running) {
            uint64_t now = GetTimeMs();
            if (now >= g_target_release_time) {
                g_pending_release = false;
                g_is_down = false;
                g_is_dragging = false;
                g_last_up = GetTimeMs();

                INPUT input = {};
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));
                break;
            }

            uint64_t wait_ms = g_target_release_time - now;
            g_release_cv.wait_for(lock, std::chrono::milliseconds(wait_ms));
        }
    }
}

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (pMouse->flags & LLMHF_INJECTED) {
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        if (wParam == WM_MOUSEMOVE) {
            if (g_is_down.load()) {
                int dx = std::abs(pMouse->pt.x - g_down_pos.x);
                int dy = std::abs(pMouse->pt.y - g_down_pos.y);
                if (dx > 3 || dy > 3) {
                    g_is_dragging = true;
                }
            }
        } else if (wParam == WM_LBUTTONDOWN) {
            uint64_t now = GetTimeMs();

            if (g_pending_release.load()) {
                {
                    std::lock_guard<std::mutex> lock(g_release_mutex);
                    g_pending_release = false;
                }
                g_release_cv.notify_one();

                uint64_t diff = now - g_last_up_attempt;
                PostLog("[+] drag chatter blocked (" + std::to_string(diff) + "ms)\n");
                return 1;
            }

            if (!g_is_dragging.load()) {
                uint64_t diff_down = (g_last_down != 0) ? (now - g_last_down) : 999999;
                uint64_t diff_up = (g_last_up != 0) ? (now - g_last_up) : 999999;

                if (diff_up < g_threshold_ms || diff_down < g_threshold_ms) {
                    g_blocked_current = true;
                    uint64_t min_diff = (diff_up < diff_down) ? diff_up : diff_down;
                    PostLog("[+] double-click blocked (" + std::to_string(min_diff) + "ms)\n");
                    return 1;
                }
            }

            g_blocked_current = false;
            g_is_down = true;
            g_is_dragging = false;
            g_down_pos = pMouse->pt;
            g_last_down = now;
        } else if (wParam == WM_LBUTTONUP) {
            if (g_blocked_current.load()) {
                g_blocked_current = false;
                return 1;
            }

            if (!g_is_down.load()) {
                return CallNextHookEx(g_hook, nCode, wParam, lParam);
            }

            uint64_t now = GetTimeMs();
            g_last_up_attempt = now;

            uint64_t delay = (g_is_dragging.load() || (now - g_last_down > 120)) ? g_drag_bridge_ms : g_click_bridge_ms;

            {
                std::lock_guard<std::mutex> lock(g_release_mutex);
                g_target_release_time = now + delay;
                g_pending_release = true;
            }
            g_release_cv.notify_one();
            return 1;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static void DisableQuickEdit() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hIn, &mode)) {
            mode &= ~ENABLE_QUICK_EDIT_MODE;
            mode |= ENABLE_EXTENDED_FLAGS;
            SetConsoleMode(hIn, mode);
        }
    }
}

static BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        g_running = false;
        if (g_pending_release.load()) {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        g_release_cv.notify_all();
        g_log_cv.notify_all();
        if (g_hook) {
            UnhookWindowsHookEx(g_hook);
            g_hook = nullptr;
        }
        ExitProcess(0);
    }
    return TRUE;
}

int main(int argc, char* argv[]) {
    SetConsoleTitleA("Anti-Double | https://github.com/monkeyokz");
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    DisableQuickEdit();
    QueryPerformanceFrequency(&g_freq);

    if (argc > 1) {
        int custom_threshold = std::atoi(argv[1]);
        if (custom_threshold > 0 && custom_threshold < 1000) {
            g_threshold_ms = static_cast<uint64_t>(custom_threshold);
        }
    }

    if (argc > 2) {
        int custom_drag_bridge = std::atoi(argv[2]);
        if (custom_drag_bridge > 0 && custom_drag_bridge < 300) {
            g_drag_bridge_ms = static_cast<uint64_t>(custom_drag_bridge);
        }
    }

    std::thread log_th(LogWorker);
    log_th.detach();

    std::thread release_th(ReleaseWorker);
    release_th.detach();

    g_hook = SetWindowsHookExA(WH_MOUSE_LL, MouseProc, GetModuleHandleA(nullptr), 0);
    if (!g_hook) {
        return 1;
    }

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    g_running = false;
    g_release_cv.notify_all();
    g_log_cv.notify_all();

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    return 0;
}
