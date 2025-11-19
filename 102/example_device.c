/**
 * 设备连接状态管理示例
 * 
 * 场景：IoT 设备的网络连接状态管理
 * 
 * 状态转换图：
 * 
 *     [断开连接] 
 *         ↓ (连接请求)
 *     [连接中]
 *         ↓ (连接成功)
 *     [已连接]
 *         ↓ (心跳超时/网络错误)
 *     [重连中]
 *         ↓ (重连成功)
 *     [已连接]
 *         ↓ (断开请求)
 *     [断开连接]
 * 
 * 特点：
 * - 多个设备独立管理（每个设备一个子HSM）
 * - 自动心跳检测
 * - 断线自动重连
 * - 主HSM监控所有设备状态
 */

#include "hsm.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* ============================================================================
 * 设备数据结构
 * ========================================================================== */

typedef enum {
    DEVICE_STATUS_DISCONNECTED,
    DEVICE_STATUS_CONNECTING,
    DEVICE_STATUS_CONNECTED,
    DEVICE_STATUS_RECONNECTING,
    DEVICE_STATUS_ERROR
} DeviceStatus;

typedef struct {
    gchar *device_id;           /* 设备ID */
    gchar *address;             /* 设备地址 */
    DeviceStatus status;        /* 设备状态 */
    gint retry_count;           /* 重连次数 */
    gint max_retries;           /* 最大重连次数 */
    gint heartbeat_timer;       /* 心跳定时器ID */
    gint heartbeat_interval;    /* 心跳间隔（毫秒） */
    gint heartbeat_timeout;     /* 心跳超时次数 */
    guint64 connected_time;     /* 连接时间 */
    guint64 last_heartbeat;     /* 最后心跳时间 */
} DeviceContext;

/* ============================================================================
 * 工具函数
 * ========================================================================== */

static void init_console_utf8(void) {
#ifdef G_OS_WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#else
    g_setenv("LANG", "zh_CN.UTF-8", TRUE);
#endif
}

static DeviceContext *device_context_new(const gchar *device_id, const gchar *address) {
    DeviceContext *ctx = g_new0(DeviceContext, 1);
    ctx->device_id = g_strdup(device_id);
    ctx->address = g_strdup(address);
    ctx->status = DEVICE_STATUS_DISCONNECTED;
    ctx->retry_count = 0;
    ctx->max_retries = 5;
    ctx->heartbeat_timer = 0;
    ctx->heartbeat_interval = 3000;  /* 3秒心跳 */
    ctx->heartbeat_timeout = 0;
    ctx->connected_time = 0;
    ctx->last_heartbeat = 0;
    return ctx;
}

static void device_context_free(DeviceContext *ctx) {
    if (!ctx) return;
    g_free(ctx->device_id);
    g_free(ctx->address);
    g_free(ctx);
}

static const gchar *status_to_string(DeviceStatus status) {
    switch (status) {
        case DEVICE_STATUS_DISCONNECTED: return "断开连接";
        case DEVICE_STATUS_CONNECTING: return "连接中";
        case DEVICE_STATUS_CONNECTED: return "已连接";
        case DEVICE_STATUS_RECONNECTING: return "重连中";
        case DEVICE_STATUS_ERROR: return "错误";
        default: return "未知";
    }
}

/* ============================================================================
 * 设备状态处理函数
 * ========================================================================== */

/**
 * 断开连接状态
 */
static gboolean disconnected_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    DeviceContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->status = DEVICE_STATUS_DISCONNECTED;
        g_print("[%s] 状态: %s\n", ctx->device_id, status_to_string(ctx->status));
        
        /* 取消心跳定时器 */
        if (ctx->heartbeat_timer > 0) {
            hsm_cancel_timer(h, ctx->heartbeat_timer);
            ctx->heartbeat_timer = 0;
        }
        
        /* 向父HSM报告状态 */
        Hsm *parent = hsm_get_parent(h);
        if (parent) {
            HsmEvent *status_ev = hsm_event_new(HSM_EVT_STEP, "device_status", ctx, ctx->device_id, 0);
            hsm_post_event(parent, status_ev);
        }
        
        return TRUE;
    }
    
    if (type == HSM_EVT_START) {
        g_print("[%s] 收到连接请求...\n", ctx->device_id);
        ctx->retry_count = 0;
        hsm_change_state(h, "connecting");
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 连接中状态
 */
static gboolean connecting_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    DeviceContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->status = DEVICE_STATUS_CONNECTING;
        g_print("[%s] 状态: %s → %s\n", ctx->device_id, status_to_string(DEVICE_STATUS_DISCONNECTED), status_to_string(ctx->status));
        g_print("[%s] 正在连接到 %s...\n", ctx->device_id, ctx->address);
        
        /* 模拟异步连接，2秒后完成 */
        hsm_schedule_timer(h, 2000);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        /* 模拟随机连接成功/失败（80% 成功率） */
        if (g_random_int_range(0, 10) < 8) {
            g_print("[%s] ✓ 连接成功\n", ctx->device_id);
            ctx->connected_time = g_get_monotonic_time();
            hsm_change_state(h, "connected");
        } else {
            g_print("[%s] ✗ 连接失败\n", ctx->device_id);
            
            if (ctx->retry_count < ctx->max_retries) {
                ctx->retry_count++;
                g_print("[%s] ↻ 重试 %d/%d\n", ctx->device_id, ctx->retry_count, ctx->max_retries);
                hsm_change_state(h, "reconnecting");
            } else {
                g_print("[%s] ✗ 达到最大重试次数\n", ctx->device_id);
                hsm_change_state(h, "error");
            }
        }
        return TRUE;
    }
    
    if (type == HSM_EVT_CANCEL) {
        g_print("[%s] 连接被取消\n", ctx->device_id);
        hsm_change_state(h, "disconnected");
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 已连接状态
 */
static gboolean connected_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    DeviceContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->status = DEVICE_STATUS_CONNECTED;
        g_print("[%s] 状态: %s\n", ctx->device_id, status_to_string(ctx->status));
        g_print("[%s] ♥ 启动心跳检测（间隔 %dms）\n", ctx->device_id, ctx->heartbeat_interval);
        
        /* 重置计数器 */
        ctx->retry_count = 0;
        ctx->heartbeat_timeout = 0;
        ctx->last_heartbeat = g_get_monotonic_time();
        
        /* 启动心跳定时器 */
        ctx->heartbeat_timer = hsm_schedule_timer(h, ctx->heartbeat_interval);
        
        /* 向父HSM报告状态 */
        Hsm *parent = hsm_get_parent(h);
        if (parent) {
            HsmEvent *status_ev = hsm_event_new(HSM_EVT_STEP, "device_status", ctx, ctx->device_id, 0);
            hsm_post_event(parent, status_ev);
        }
        
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        /* 心跳定时器触发 */
        guint64 now = g_get_monotonic_time();
        guint64 elapsed = (now - ctx->last_heartbeat) / 1000000;
        
        /* 模拟心跳应答（90% 成功率） */
        if (g_random_int_range(0, 10) < 9) {
            g_print("[%s] ♥ 心跳正常 (连接时长: %lu秒)\n", ctx->device_id, 
                    (now - ctx->connected_time) / 1000000);
            ctx->last_heartbeat = now;
            ctx->heartbeat_timeout = 0;
            
            /* 继续下一次心跳 */
            ctx->heartbeat_timer = hsm_schedule_timer(h, ctx->heartbeat_interval);
        } else {
            ctx->heartbeat_timeout++;
            g_print("[%s] ⚠ 心跳超时 (%d次)\n", ctx->device_id, ctx->heartbeat_timeout);
            
            if (ctx->heartbeat_timeout >= 3) {
                g_print("[%s] ✗ 连接丢失，准备重连...\n", ctx->device_id);
                hsm_change_state(h, "reconnecting");
            } else {
                /* 继续检测 */
                ctx->heartbeat_timer = hsm_schedule_timer(h, ctx->heartbeat_interval);
            }
        }
        return TRUE;
    }
    
    if (type == HSM_EVT_CANCEL) {
        g_print("[%s] 收到断开请求\n", ctx->device_id);
        hsm_change_state(h, "disconnected");
        return TRUE;
    }
    
    if (type == HSM_EVT_EXIT) {
        /* 取消心跳定时器 */
        if (ctx->heartbeat_timer > 0) {
            hsm_cancel_timer(h, ctx->heartbeat_timer);
            ctx->heartbeat_timer = 0;
        }
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 重连中状态
 */
static gboolean reconnecting_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    DeviceContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->status = DEVICE_STATUS_RECONNECTING;
        g_print("[%s] 状态: %s (尝试 %d/%d)\n", ctx->device_id, 
                status_to_string(ctx->status), ctx->retry_count, ctx->max_retries);
        
        /* 等待 1 秒后重连 */
        hsm_schedule_timer(h, 1000);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        /* 尝试重连 */
        hsm_change_state(h, "connecting");
        return TRUE;
    }
    
    if (type == HSM_EVT_CANCEL) {
        g_print("[%s] 重连被取消\n", ctx->device_id);
        hsm_change_state(h, "disconnected");
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 错误状态
 */
static gboolean error_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    DeviceContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->status = DEVICE_STATUS_ERROR;
        g_print("[%s] 状态: %s - 连接失败，进入错误状态\n", ctx->device_id, status_to_string(ctx->status));
        
        /* 向父HSM报告错误 */
        Hsm *parent = hsm_get_parent(h);
        if (parent) {
            HsmEvent *err_ev = hsm_event_new(HSM_EVT_RESULT_ERROR, "device_error", ctx, ctx->device_id, 0);
            hsm_post_event(parent, err_ev);
        }
        
        return TRUE;
    }
    
    if (type == HSM_EVT_START) {
        g_print("[%s] 从错误状态重新启动...\n", ctx->device_id);
        ctx->retry_count = 0;
        hsm_change_state(h, "connecting");
        return TRUE;
    }
    
    return FALSE;
}

/* ============================================================================
 * 主控制器状态处理函数
 * ========================================================================== */

/**
 * 主控制器：监控所有设备状态
 */
static gboolean controller_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        g_print("\n╔══════════════════════════════════════════╗\n");
        g_print("║   设备管理控制器启动                     ║\n");
        g_print("╚══════════════════════════════════════════╝\n\n");
        return TRUE;
    }
    
    /* 接收设备状态更新 */
    if (type == HSM_EVT_STEP) {
        const gchar *event_name = hsm_event_get_name(ev);
        if (g_strcmp0(event_name, "device_status") == 0) {
            DeviceContext *ctx = hsm_event_get_data(ev);
            const gchar *source = hsm_event_get_source(ev);
            g_print("\n[控制器] 设备 %s 状态更新: %s\n", source, status_to_string(ctx->status));
            return TRUE;
        }
    }
    
    /* 接收设备错误 */
    if (type == HSM_EVT_RESULT_ERROR) {
        const gchar *source = hsm_event_get_source(ev);
        g_print("\n[控制器] ⚠ 设备 %s 发生错误\n", source);
        return TRUE;
    }
    
    /* 定时检查任务（演示用） */
    if (type == HSM_EVT_TIMEOUT) {
        g_print("\n[控制器] 系统运行正常，所有设备状态稳定\n");
        return TRUE;
    }
    
    return FALSE;
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

/*
 * 启动调度器状态机：负责分阶段向设备投递START事件，并在15秒后自动停止主控制器
 */
typedef struct {
    Hsm *controller;
    Hsm *device1;
    Hsm *device2;
    Hsm *device3;
    int step;
} SchedulerContext;

static gboolean scheduler_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    SchedulerContext *sched = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    if (type == HSM_EVT_ENTRY) {
        sched->step = 0;
        g_print("\n[调度器] 启动，准备分阶段投递设备连接请求...\n");
        /* 500ms后投递第一个设备的START事件 */
        hsm_schedule_timer(h, 500);
        return TRUE;
    }
    if (type == HSM_EVT_TIMEOUT) {
        sched->step++;
        if (sched->step == 1) {
            g_print("[调度器] 向 Device-001 投递连接请求\n");
            HsmEvent *e = hsm_event_new(HSM_EVT_START, "connect", NULL, "main", 0);
            hsm_post_event(sched->device1, e);
            hsm_schedule_timer(h, 500); // 500ms后投递下一个
        } else if (sched->step == 2) {
            g_print("[调度器] 向 Device-002 投递连接请求\n");
            HsmEvent *e = hsm_event_new(HSM_EVT_START, "connect", NULL, "main", 0);
            hsm_post_event(sched->device2, e);
            hsm_schedule_timer(h, 500); // 500ms后投递下一个
        } else if (sched->step == 3) {
            g_print("[调度器] 向 Device-003 投递连接请求\n");
            HsmEvent *e = hsm_event_new(HSM_EVT_START, "connect", NULL, "main", 0);
            hsm_post_event(sched->device3, e);
            hsm_schedule_timer(h, 14500); // 15秒总时长，已过去1.5秒，剩余13.5秒
        } else if (sched->step == 4) {
            g_print("[调度器] 15秒到，停止主控制器\n");
            hsm_stop(sched->controller);
        }
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    init_console_utf8();

    g_print("╔════════════════════════════════════════════════╗\n");
    g_print("║   HSM 设备连接管理示例                         ║\n");
    g_print("║   演示：多设备并发状态管理                     ║\n");
    g_print("╚════════════════════════════════════════════════╝\n\n");

    /* 创建主控制器 */
    Hsm *controller = hsm_new("controller", FALSE);
    hsm_register_state(controller, "monitoring", controller_handler, NULL);
    hsm_change_state(controller, "monitoring");

    /* 创建设备上下文 */
    DeviceContext *ctx1 = device_context_new("Device-001", "192.168.1.101:8080");
    DeviceContext *ctx2 = device_context_new("Device-002", "192.168.1.102:8080");
    DeviceContext *ctx3 = device_context_new("Device-003", "192.168.1.103:8080");

    /* 创建设备状态机（每个设备独立线程） */
    Hsm *device1 = hsm_new(ctx1->device_id, TRUE);
    Hsm *device2 = hsm_new(ctx2->device_id, TRUE);
    Hsm *device3 = hsm_new(ctx3->device_id, TRUE);

    /* 设置层级关系 */
    hsm_set_parent(device1, controller);
    hsm_set_parent(device2, controller);
    hsm_set_parent(device3, controller);

    /* 注册设备状态 */
    const gchar *states[] = {"disconnected", "connecting", "connected", "reconnecting", "error"};
    HsmStateHandler handlers[] = {
        disconnected_state_handler,
        connecting_state_handler,
        connected_state_handler,
        reconnecting_state_handler,
        error_state_handler
    };

    for (int i = 0; i < 5; i++) {
        hsm_register_state(device1, states[i], handlers[i], ctx1);
        hsm_register_state(device2, states[i], handlers[i], ctx2);
        hsm_register_state(device3, states[i], handlers[i], ctx3);
    }

    /* 设置初始状态 */
    hsm_change_state(device1, "disconnected");
    hsm_change_state(device2, "disconnected");
    hsm_change_state(device3, "disconnected");

    /* 启动设备状态机 */
    hsm_start(device1, TRUE);
    hsm_start(device2, TRUE);
    hsm_start(device3, TRUE);

    /* 启动控制器 */
    hsm_start(controller, FALSE);

    /* 启动调度器（在主控制器线程） */
    SchedulerContext *sched = g_new0(SchedulerContext, 1);
    sched->controller = controller;
    sched->device1 = device1;
    sched->device2 = device2;
    sched->device3 = device3;
    Hsm *scheduler = hsm_new("scheduler", FALSE);
    hsm_register_state(scheduler, "running", scheduler_handler, sched);
    hsm_change_state(scheduler, "running");
    hsm_start(scheduler, FALSE);

    /* 运行主循环 */
    g_print("\n开始演示（将运行 15 秒）...\n\n");
    hsm_run(controller);

    g_print("\n\n╔══════════════════════════════════════════╗\n");
    g_print("║   演示结束                               ║\n");
    g_print("╚══════════════════════════════════════════╝\n");

    /* 清理资源 */
    hsm_destroy(device1);
    hsm_destroy(device2);
    hsm_destroy(device3);
    hsm_destroy(controller);
    hsm_destroy(scheduler);
    g_free(sched);

    device_context_free(ctx1);
    device_context_free(ctx2);
    device_context_free(ctx3);

    return 0;
}
