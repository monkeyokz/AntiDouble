#include <windows.h>
#include <iostream>
#include <cstdint>

static HHOOK g_hook = nullptr;
static LARGE_INTEGER g_freq;
static uint64_t g_last_down = 0;
static uint64_t g_last_up = 0;
static bool g_blocked_current = false;
static uint64_t g_threshold_ms = 80;

static inline uint64_t GetTimeMs() {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>((counter.QuadPart * 1000) / g_freq.QuadPart);
}

static LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_LBUTTONDOWN) {
            uint64_t now = GetTimeMs();
            uint64_t diff_down = (g_last_down != 0) ? (now - g_last_down) : 999999;
            uint64_t diff_up = (g_last_up != 0) ? (now - g_last_up) : 999999;

            if (diff_up < g_threshold_ms || diff_down < g_threshold_ms) {
                g_blocked_current = true;
                uint64_t min_diff = (diff_up < diff_down) ? diff_up : diff_down;
                std::cout << "[+] double-click blocked (" << min_diff << "ms)\n";
                return 1;
            }

            g_blocked_current = false;
            g_last_down = now;
        } else if (wParam == WM_LBUTTONUP) {
            if (g_blocked_current) {
                g_blocked_current = false;
                return 1;
            }
            g_last_up = GetTimeMs();
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
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

    g_hook = SetWindowsHookExA(WH_MOUSE_LL, MouseProc, GetModuleHandleA(nullptr), 0);
    if (!g_hook) {
        return 1;
    }

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }

    return 0;
}
