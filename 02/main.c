#include <glib.h>
#include "common.h"

// 第一道题目: 用GSList实现字符串链表，添加、删除、查找指定字符串。

GSList *g_slist_remove_custom(GSList *list, gconstpointer data, GCompareFunc func) {
    GSList *found = g_slist_find_custom(list, data, func);
    if (found) {
        gpointer item = found->data;
        list = g_slist_remove(list, item);
        g_free(item);
    }
    return list;
}

int main() {
    init_console_utf8();

    GSList *list = NULL;
    // 添加字符串元素
    list = g_slist_append(list, g_strdup("01"));
    list = g_slist_append(list, g_strdup("02"));
    list = g_slist_append(list, g_strdup("03"));

    for (GSList *l = list; l != NULL; l = l->next) {
        g_print("%s\n", (char *)l->data);
    }
    // 删除字符串元素
    list = g_slist_remove_custom(list, "02", (GCompareFunc)g_strcmp0);

    for (GSList *l = list; l != NULL; l = l->next) {
        g_print("%s\n", (char *)l->data);
    }
    // 查找字符串元素
    g_slist_find_custom(list, "03", (GCompareFunc)g_strcmp0) ? g_print("Found 03\n") : g_print("03 not found\n");

    g_slist_free_full(list, g_free);
    return 0;
}