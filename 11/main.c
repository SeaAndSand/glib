#include <glib.h>
#include "common.h"

// 题目: 使用g_strdup复制字符串并释放内存。

int main() {
    init_console_utf8();

    gchar *dest = g_strdup("Hello, GLib!");
    g_print("复制的字符串: %s\n", dest);
    g_free(dest);
    
    return 0;
}