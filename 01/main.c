#include <glib.h>
#include "common.h"

// 第一道题目: 使用GList实现一个整数链表，插入10个元素并遍历输出。

int main() {
    init_console_utf8();

    GList *list = NULL;
    for (gint i = 1; i < 10; i++) {
        list = g_list_append(list, GINT_TO_POINTER(i));
    }

    for (GList *l = list; l != NULL; l = l->next) {
        g_print("%d\n", GPOINTER_TO_INT(l->data));
    }

    g_list_free(list);
    return 0;
}