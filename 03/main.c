#include <glib.h>
#include "common.h"

// 题目: 用GArray实现动态数组，存储浮点数并求平均值。

int main() {
    init_console_utf8();

    GArray *array = g_array_new(FALSE, FALSE, sizeof(gfloat));
    gfloat numbers[] = {1.0f, 2.5f, 3.75f, 4.25f, 5.0f};
    for (guint i = 0; i < G_N_ELEMENTS(numbers); i++) {
        g_array_append_val(array, numbers[i]);
    }

    gfloat sum = 0.0f;
    for (guint i = 0; i < array->len; i++) {
        sum += g_array_index(array, gfloat, i);
    }

    gfloat average = sum / array->len;

    g_print("Average: %.2f\n", average);

    g_array_free(array, TRUE);
    return 0;
}