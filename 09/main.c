#include <glib.h>
#include "common.h"

// 题目: 用GList实现链表反转。

// GList有内置的反转函数g_list_reverse，如果自己实现，可以参考以下代码:

GList* reverse_glist(GList *list) {
    GList *prev = NULL;
    GList *current = list;
    GList *next = NULL;

    while (current != NULL) {
        next = current->next;
        current->next = prev;
        current->prev = next;
        prev = current;
        current = next;
    }
    return prev;
}


int main() {
    init_console_utf8();

    GList *list = NULL;
    for (int i = 1; i <= 10; ++i) {
        list = g_list_append(list, GINT_TO_POINTER(i));
    }

    g_print("原始链表: ");
    for (GList *iter = list; iter != NULL; iter = iter->next) {
        g_print("%d ", GPOINTER_TO_INT(iter->data));
    }

    list = g_list_reverse(list);

    g_print("\n反转后链表: ");
    for (GList *iter = list; iter != NULL; iter = iter->next) {
        g_print("%d ", GPOINTER_TO_INT(iter->data));
    }

    list = reverse_glist(list);

    g_print("\n自定义反转后链表: ");
    for (GList *iter = list; iter != NULL; iter = iter->next) {
        g_print("%d ", GPOINTER_TO_INT(iter->data));
    }

    g_list_free(list);
    
    return 0;
}