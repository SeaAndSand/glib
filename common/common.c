#include <glib.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* 一个简单的模块处理函数，使用超时定时器来模拟异步工作。 */
void init_console_utf8(void) {
#ifdef G_OS_WIN32
    /* Windows 平台：设置控制台代码页为 UTF-8 */
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    
    /* 可选：启用 VT100 转义序列支持（用于彩色输出） */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#else
    /* Unix/Linux 平台：通常已经是 UTF-8 */
    g_setenv("LANG", "zh_CN.UTF-8", TRUE);
#endif
}