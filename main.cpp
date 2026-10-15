#include <windows.h>
#include <iostream>
#include <cstdint>

static HHOOK g_hook = nullptr;
static LARGE_INTEGER g_freq;
static uint64_t g_last_down = 0;
static uint64_t g_last_up = 0;
static uint64_t g_last_up_attempt = 0;
static bool g_blocked_current = false;
static bool g_is_down = false;
static UINT_PTR g_timer_id = 0;
static uint64_t g_threshold_ms = 80;
static UINT g_release_delay_ms = 25;

static inline uint64_t GetTimeMs() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>((counter.QuadPart * 1000) / g_freq.QuadPart);
}

static VOID CALLBACK ReleaseTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_timer_id != 0) {
        KillTimer(nullptr, g_timer_id);
        g_timer_id = 0;
    }
    g_is_down = false;
    g_last_up = GetTimeMs();

    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &input, sizeof(INPUT));
}

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (pMouse->flags & LLMHF_INJECTED) {
            return CallNextHookEx(g_hook, nCode, wParam, lParam);
        }

        if (wParam == WM_LBUTTONDOWN) {
            uint64_t now = GetTimeMs();

            if (g_timer_id != 0) {
                KillTimer(nullptr, g_timer_id);
                g_timer_id = 0;
                uint64_t diff = now - g_last_up_attempt;
                std::cout << "[+] drag chatter blocked (" << diff << "ms)\n";
                return 1;
            }

            uint64_t diff_down = (g_last_down != 0) ? (now - g_last_down) : 999999;
            uint64_t diff_up = (g_last_up != 0) ? (now - g_last_up) : 999999;

            if (diff_up < g_threshold_ms || diff_down < g_threshold_ms) {
                g_blocked_current = true;
                uint64_t min_diff = (diff_up < diff_down) ? diff_up : diff_down;
                std::cout << "[+] double-click blocked (" << min_diff << "ms)\n";
                return 1;
            }

            g_blocked_current = false;
            g_is_down = true;
            g_last_down = now;
        } else if (wParam == WM_LBUTTONUP) {
            if (g_blocked_current) {
                g_blocked_current = false;
                return 1;
            }

            if (!g_is_down) {
                return CallNextHookEx(g_hook, nCode, wParam, lParam);
            }

            g_last_up_attempt = GetTimeMs();
            if (g_timer_id != 0) {
                KillTimer(nullptr, g_timer_id);
            }
            g_timer_id = SetTimer(nullptr, 0, g_release_delay_ms, ReleaseTimerProc);
            return 1;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        if (g_timer_id != 0) {
            KillTimer(nullptr, g_timer_id);
            g_timer_id = 0;
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &input, sizeof(INPUT));
        }
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
    QueryPerformanceFrequency(&g_freq);

    if (argc > 1) {
        int custom_threshold = std::atoi(argv[1]);
        if (custom_threshold > 0 && custom_threshold < 1000) {
            g_threshold_ms = static_cast<uint64_t>(custom_threshold);
        }
    }

    if (argc > 2) {
        int custom_drag_delay = std::atoi(argv[2]);
        if (custom_drag_delay > 0 && custom_drag_delay < 200) {
            g_release_delay_ms = static_cast<UINT>(custom_drag_delay);
        }
    }

    g_hook = SetWindowsHookExA(WH_MOUSE_LL, MouseProc, GetModuleHandleA(nullptr), 0);
    if (!g_hook) {
        return 1;
    }

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_timer_id != 0) {
        KillTimer(nullptr, g_timer_id);
        g_timer_id = 0;
    }

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    return 0;
}
