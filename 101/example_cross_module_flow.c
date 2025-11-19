/**
 * 跨模块业务流调度示例
 *
 * 业务流：A1->A2->B1->B2->B3->B4->A3->A4->B5->A5
 * - 模块A、B各自独立线程和HSM，拥有自己的状态流
 * - 主调度器HSM在主线程，负责推进业务流
 * - 所有推进、同步都通过HSM事件异步投递，线程安全
 */
#include "hsm.h"
#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* ===================== 模块A/B状态名 ===================== */
static const gchar *A_STATES[] = {"A1", "A2", "A3", "A4", "A5"};
static const gchar *B_STATES[] = {"B1", "B2", "B3", "B4", "B5"};

/* ===================== 业务流步骤枚举 ===================== */
typedef enum {
    FLOW_A1,
    FLOW_A2,
    FLOW_B1,
    FLOW_B2,
    FLOW_B3,
    FLOW_B4,
    FLOW_A3,
    FLOW_A4,
    FLOW_B5,
    FLOW_A5,
    FLOW_DONE
} FlowStep;

/* ===================== 模块上下文 ===================== */
typedef struct {
    const gchar *name; // "A" or "B"
    gint current;      // 当前状态索引
} ModuleCtx;

typedef struct {
    FlowStep step;       // 当前业务流步骤
    Hsm *modA;
    Hsm *modB;
    gboolean modA_ready; // 模块A是否就绪
    gboolean modB_ready; // 模块B是否就绪
} FlowCtx;

/* ===================== 工具函数 ===================== */
static void init_console_utf8(void)
{
#ifdef G_OS_WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#else
    g_setenv("LANG", "zh_CN.UTF-8", TRUE);
#endif
}

/* ===================== 模块A/B状态处理 ===================== */
static gboolean module_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data)
{
    ModuleCtx *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    if (type == HSM_EVT_ENTRY) {
        g_print("[%s] 进入状态: %s\n", ctx->name, state);
        
        /* 子模块初始化完成后，向主调度器发送就绪通知 */
        if (g_strcmp0(state, "A1") == 0 || g_strcmp0(state, "B1") == 0) {
            Hsm *parent = hsm_get_parent(h);
            if (parent) {
                HsmEvent *ready = hsm_event_new(HSM_EVT_STEP, "module_ready", NULL, ctx->name, 0);
                hsm_post_event(parent, ready);
                g_print("[%s] 已就绪，通知主调度器\n", ctx->name);
            }
        }
        return TRUE;
    }
    if (type == HSM_EVT_START) {
        g_print("[%s] 开始执行: %s\n", ctx->name, state);
        /* 模拟异步工作，0.5秒后完成 */
        hsm_schedule_timer(h, 500);
        return TRUE;
    }
    if (type == HSM_EVT_TIMEOUT) {
        g_print("[%s] 完成: %s\n", ctx->name, state);
        /* 通知主调度器 */
        Hsm *parent = hsm_get_parent(h);
        if (parent) {
            HsmEvent *done = hsm_event_new(HSM_EVT_RESULT_OK, state, NULL, ctx->name, 0);
            hsm_post_event(parent, done);
        }
        return TRUE;
    }
    return FALSE;
}

/* ===================== 主调度器状态处理 ===================== */
static gboolean flow_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data)
{
    FlowCtx *flow = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    if (type == HSM_EVT_ENTRY) {
        g_print("\n[调度器] 进入业务流调度状态: %s\n", state);
        g_print("[调度器] 等待所有子模块就绪...\n");
        return TRUE;
    }
    
    /* 处理子模块就绪通知 */
    if (type == HSM_EVT_STEP) {
        const gchar *event_name = hsm_event_get_name(ev);
        if (g_strcmp0(event_name, "module_ready") == 0) {
            const gchar *source = hsm_event_get_source(ev);
            if (g_strcmp0(source, "A") == 0) {
                flow->modA_ready = TRUE;
                g_print("[调度器] 模块A已就绪\n");
            } else if (g_strcmp0(source, "B") == 0) {
                flow->modB_ready = TRUE;
                g_print("[调度器] 模块B已就绪\n");
            }
            
            /* 所有模块都就绪后，启动业务流 */
            if (flow->modA_ready && flow->modB_ready) {
                g_print("[调度器] 所有模块已就绪，启动业务流\n");
                g_print("[调度器] 启动A1\n");
                HsmEvent *e = hsm_event_new(HSM_EVT_START, "A1", NULL, "flow", 0);
                hsm_post_event(flow->modA, e);
            }
            return TRUE;
        }
    }
    
    if (type == HSM_EVT_RESULT_OK) {
        const gchar *done_state = hsm_event_get_name(ev);
        g_print("[调度器] 收到完成: %s\n", done_state);
        /* 推进业务流 */
        flow->step++;
        switch (flow->step) {
        case FLOW_A2:
            g_print("[调度器] 启动A2\n");
            hsm_change_state(flow->modA, "A2");
            hsm_post_event(flow->modA, hsm_event_new(HSM_EVT_START, "A2", NULL, "flow", 0));
            break;
        case FLOW_B1:
            g_print("[调度器] 启动B1\n");
            hsm_change_state(flow->modB, "B1");
            hsm_post_event(flow->modB, hsm_event_new(HSM_EVT_START, "B1", NULL, "flow", 0));
            break;
        case FLOW_B2:
            g_print("[调度器] 启动B2\n");
            hsm_change_state(flow->modB, "B2");
            hsm_post_event(flow->modB, hsm_event_new(HSM_EVT_START, "B2", NULL, "flow", 0));
            break;
        case FLOW_B3:
            g_print("[调度器] 启动B3\n");
            hsm_change_state(flow->modB, "B3");
            hsm_post_event(flow->modB, hsm_event_new(HSM_EVT_START, "B3", NULL, "flow", 0));
            break;
        case FLOW_B4:
            g_print("[调度器] 启动B4\n");
            hsm_change_state(flow->modB, "B4");
            hsm_post_event(flow->modB, hsm_event_new(HSM_EVT_START, "B4", NULL, "flow", 0));
            break;
        case FLOW_A3:
            g_print("[调度器] 启动A3\n");
            hsm_change_state(flow->modA, "A3");
            hsm_post_event(flow->modA, hsm_event_new(HSM_EVT_START, "A3", NULL, "flow", 0));
            break;
        case FLOW_A4:
            g_print("[调度器] 启动A4\n");
            hsm_change_state(flow->modA, "A4");
            hsm_post_event(flow->modA, hsm_event_new(HSM_EVT_START, "A4", NULL, "flow", 0));
            break;
        case FLOW_B5:
            g_print("[调度器] 启动B5\n");
            hsm_change_state(flow->modB, "B5");
            hsm_post_event(flow->modB, hsm_event_new(HSM_EVT_START, "B5", NULL, "flow", 0));
            break;
        case FLOW_A5:
            g_print("[调度器] 启动A5\n");
            hsm_change_state(flow->modA, "A5");
            hsm_post_event(flow->modA, hsm_event_new(HSM_EVT_START, "A5", NULL, "flow", 0));
            break;
        case FLOW_DONE:
            g_print("[调度器] 业务流全部完成！\n");
            hsm_stop(h);
            break;
        default:
            break;
        }
        return TRUE;
    }
    return FALSE;
}

/* ===================== 主函数 ===================== */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    init_console_utf8();
    g_print("\n╔════════════════════════════════════════════════╗\n");
    g_print("║   跨模块业务流调度示例                         ║\n");
    g_print("╚════════════════════════════════════════════════╝\n\n");
    /* 创建模块A/B上下文和HSM */
    ModuleCtx *a_ctx = g_new0(ModuleCtx, 1);
    a_ctx->name = "A";
    ModuleCtx *b_ctx = g_new0(ModuleCtx, 1);
    b_ctx->name = "B";
    Hsm *modA = hsm_new("modA", TRUE);
    Hsm *modB = hsm_new("modB", TRUE);
    for (int i = 0; i < 5; ++i) {
        hsm_register_state(modA, A_STATES[i], module_handler, a_ctx);
        hsm_register_state(modB, B_STATES[i], module_handler, b_ctx);
    }
    hsm_change_state(modA, "A1");
    hsm_change_state(modB, "B1");
    hsm_start(modA, TRUE);
    hsm_start(modB, TRUE);
    /* 创建主调度器HSM */
    FlowCtx *flow = g_new0(FlowCtx, 1);
    flow->step = FLOW_A1;
    flow->modA = modA;
    flow->modB = modB;
    flow->modA_ready = FALSE;
    flow->modB_ready = FALSE;
    Hsm *scheduler = hsm_new("scheduler", FALSE);
    hsm_register_state(scheduler, "flow", flow_handler, flow);
    hsm_change_state(scheduler, "flow");
    hsm_set_parent(modA, scheduler);
    hsm_set_parent(modB, scheduler);
    hsm_start(scheduler, FALSE);
    hsm_run(scheduler);
    /* 清理资源 */
    hsm_destroy(modA);
    hsm_destroy(modB);
    hsm_destroy(scheduler);
    g_free(a_ctx);
    g_free(b_ctx);
    g_free(flow);
    return 0;
}
