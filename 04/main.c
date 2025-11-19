#include <glib.h>
#include <gio/gio.h>
#include "common.h"

// 题目: 使用GLib创建一个csv文件，并写入一些数据，写200W条。

int main() {
    init_console_utf8();

    GError *error = NULL;
    GFile *file = g_file_new_for_path("output.csv");
    GOutputStream *out_stream = G_OUTPUT_STREAM(g_file_replace(file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &error));
    if (!out_stream) {
        g_print("无法创建文件: %s\n", error->message);
        g_error_free(error);
        g_object_unref(file);
        return 1;
    }

    // 写入CSV表头
    gchar *header = "id,name,value\n";
    g_output_stream_write(out_stream, header, strlen(header), NULL, &error);

    // 写入200W条数据
    for (gint i = 1; i <= 2000000; ++i) {
        gchar *line = g_strdup_printf("%d,Name%d,%.2f\n", i, i, i * 0.01);
        g_output_stream_write(out_stream, line, strlen(line), NULL, &error);
        g_free(line);

        // 可选：每10万条刷新一次，避免缓存过大
        if (i % 100000 == 0) {
            g_output_stream_flush(out_stream, NULL, NULL);
        }
    }

    g_output_stream_flush(out_stream, NULL, NULL);
    g_output_stream_close(out_stream, NULL, NULL);
    g_object_unref(out_stream);
    g_object_unref(file);

    g_print("CSV文件写入完成。\n");
    return 0;
}