// 虚拟挂载，一个设备暴露了FTP服务，我们现在需要进行远程挂载并进行展示
// 没有界面，我们需要在控制台中展示界面

#include <glib.h>
#include <gio/gio.h>
#include "common.h"
#include <string.h>

static gboolean ftp_send_command(GOutputStream *out, const gchar *command, GError **error) {
    gchar *payload = g_strconcat(command, "\r\n", NULL);
    gboolean ok = g_output_stream_write_all(out, payload, strlen(payload), NULL, NULL, error);
    g_free(payload);
    return ok;
}

static gboolean ftp_read_response(GDataInputStream *in, guint *code, gchar **message, GError **error) {
    gchar *line = NULL;
    gchar code_buf[4] = {0};
    gboolean more = TRUE;

    while (more) {
        g_free(line);
        line = g_data_input_stream_read_line(in, NULL, NULL, error);
        if (!line) {
            return FALSE;
        }
        g_print("< %s\n", line);
        if (g_ascii_isdigit(line[0]) && g_ascii_isdigit(line[1]) && g_ascii_isdigit(line[2])) {
            memcpy(code_buf, line, 3);
            more = line[3] == '-';
        } else {
            more = TRUE;
        }
    }

    if (code) {
        *code = (guint)g_ascii_strtoull(code_buf, NULL, 10);
    }
    if (message) {
        *message = line;
    } else {
        g_free(line);
    }
    return TRUE;
}

static gboolean ftp_parse_pasv_endpoint(const gchar *response, gchar **host, guint16 *port) {
    const gchar *start = strchr(response, '(');
    const gchar *end = start ? strchr(start, ')') : NULL;
    if (!start || !end || end <= start + 1) {
        return FALSE;
    }

    gchar *inside = g_strndup(start + 1, end - start - 1);
    gchar **parts = g_strsplit(inside, ",", 6);
    gboolean ok = parts && g_strv_length(parts) == 6;

    if (ok) {
        *host = g_strdup_printf("%s.%s.%s.%s", parts[0], parts[1], parts[2], parts[3]);
        *port = (guint16)(atoi(parts[4]) * 256 + atoi(parts[5]));
    }

    g_strfreev(parts);
    g_free(inside);
    return ok;
}

int main() {
    init_console_utf8();

    const gchar *host = "192.168.23.174";
    const gchar *username = "root";
    const gchar *password = "Huasu@12345";

    GError *error = NULL;
    GSocketClient *control_client = g_socket_client_new();
    GSocketConnection *control_conn = g_socket_client_connect_to_host(control_client, host, 21, NULL, &error);
    if (!control_conn) {
        g_printerr("无法连接 FTP 控制通道: %s\n", error ? error->message : "未知错误");
        g_clear_error(&error);
        g_object_unref(control_client);
        return 1;
    }

    GInputStream *ctrl_in_raw = g_io_stream_get_input_stream(G_IO_STREAM(control_conn));
    GDataInputStream *ctrl_in = g_data_input_stream_new(ctrl_in_raw);
    g_data_input_stream_set_newline_type(ctrl_in, G_DATA_STREAM_NEWLINE_TYPE_CR_LF);
    GOutputStream *ctrl_out = g_io_stream_get_output_stream(G_IO_STREAM(control_conn));

    guint code = 0;
    gchar *message = NULL;

    if (!ftp_read_response(ctrl_in, &code, &message, &error) || code != 220) {
        g_printerr("连接握手失败。\n");
        goto cleanup;
    }
    g_clear_pointer(&message, g_free);

    gchar *user_cmd = g_strdup_printf("USER %s", username);
    g_print("> %s\n", user_cmd);
    if (!ftp_send_command(ctrl_out, user_cmd, &error) || !ftp_read_response(ctrl_in, &code, &message, &error) || (code != 230 && code != 331)) {
        g_printerr("USER 命令失败。\n");
        g_free(user_cmd);
        goto cleanup;
    }
    g_free(user_cmd);
    g_clear_pointer(&message, g_free);

    gchar *pass_cmd = g_strdup("PASS ******");
    g_print("> %s\n", pass_cmd);
    g_free(pass_cmd);
    if (!ftp_send_command(ctrl_out, g_strdup_printf("PASS %s", password), &error) ||
        !ftp_read_response(ctrl_in, &code, &message, &error) || code != 230) {
        g_printerr("PASS 命令失败。\n");
        goto cleanup;
    }
    g_clear_pointer(&message, g_free);

    g_print("> PASV\n");
    if (!ftp_send_command(ctrl_out, "PASV", &error) || !ftp_read_response(ctrl_in, &code, &message, &error) || code != 227) {
        g_printerr("PASV 命令失败。\n");
        goto cleanup;
    }

    gchar *pasv_host = NULL;
    guint16 pasv_port = 0;
    if (!ftp_parse_pasv_endpoint(message, &pasv_host, &pasv_port)) {
        g_printerr("无法解析 PASV 地址。\n");
        goto cleanup;
    }
    g_print("建立被动数据连接 %s:%u\n", pasv_host, pasv_port);
    g_clear_pointer(&message, g_free);

    GSocketClient *data_client = g_socket_client_new();
    GSocketConnection *data_conn = g_socket_client_connect_to_host(data_client, pasv_host, pasv_port, NULL, &error);
    g_free(pasv_host);
    if (!data_conn) {
        g_printerr("无法建立数据通道: %s\n", error ? error->message : "未知错误");
        g_clear_error(&error);
        g_object_unref(data_client);
        goto cleanup;
    }

    g_print("> LIST\n");
    if (!ftp_send_command(ctrl_out, "LIST", &error)) {
        g_printerr("发送 LIST 失败。\n");
        g_object_unref(data_conn);
        g_object_unref(data_client);
        goto cleanup;
    }

    GInputStream *data_in = g_io_stream_get_input_stream(G_IO_STREAM(data_conn));
    gchar buffer[4096];
    gssize bytes = 0;
    while ((bytes = g_input_stream_read(data_in, buffer, sizeof buffer, NULL, &error)) > 0) {
        g_print("%.*s", (int)bytes, buffer);
    }
    g_print("\n");
    if (bytes < 0) {
        g_printerr("读取数据通道失败: %s\n", error ? error->message : "未知错误");
        g_clear_error(&error);
    }

    g_io_stream_close(G_IO_STREAM(data_conn), NULL, NULL);
    g_object_unref(data_conn);
    g_object_unref(data_client);

    if (!ftp_read_response(ctrl_in, &code, &message, &error) || code != 226) {
        g_printerr("LIST 完成响应异常。\n");
        goto cleanup;
    }
    g_clear_pointer(&message, g_free);

    g_print("> QUIT\n");
    if (!ftp_send_command(ctrl_out, "QUIT", &error) || !ftp_read_response(ctrl_in, &code, &message, &error)) {
        g_printerr("QUIT 失败。\n");
    }
    g_clear_pointer(&message, g_free);

cleanup:
    if (error) {
        g_printerr("错误: %s\n", error->message);
        g_clear_error(&error);
    }
    if (ctrl_in) {
        g_object_unref(ctrl_in);
    }
    if (control_conn) {
        g_io_stream_close(G_IO_STREAM(control_conn), NULL, NULL);
        g_object_unref(control_conn);
    }
    if (control_client) {
        g_object_unref(control_client);
    }
    return 0;
}
