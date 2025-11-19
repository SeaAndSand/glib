/**
 * 工作流引擎示例
 * 
 * 场景：一个数据处理工作流，包含多个步骤，每个步骤可能失败并需要重试
 * 
 * 工作流程：
 * 1. 初始化阶段：准备资源
 * 2. 加载数据阶段：从文件/网络加载数据（可能超时）
 * 3. 验证数据阶段：检查数据完整性
 * 4. 处理数据阶段：执行实际处理（可能很耗时）
 * 5. 保存结果阶段：保存到数据库（可能失败需要重试）
 * 6. 清理阶段：释放资源
 * 
 * 特点：
 * - 每个阶段有超时机制
 * - 支持重试策略
 * - 支持暂停/恢复
 * - 错误处理和回滚
 */

#include "hsm.h"

#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* ============================================================================
 * 工作流数据结构
 * ========================================================================== */

typedef struct {
    gint total_steps;      /* 总步骤数 */
    gint current_step;     /* 当前步骤 */
    gint retry_count;      /* 当前步骤重试次数 */
    gint max_retries;      /* 最大重试次数 */
    gboolean paused;       /* 是否暂停 */
    gchar *data;           /* 模拟的数据 */
    guint64 start_time;    /* 开始时间 */
} WorkflowContext;

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

static WorkflowContext *workflow_context_new(void) {
    WorkflowContext *ctx = g_new0(WorkflowContext, 1);
    ctx->total_steps = 6;
    ctx->current_step = 0;
    ctx->retry_count = 0;
    ctx->max_retries = 3;
    ctx->paused = FALSE;
    ctx->data = NULL;
    ctx->start_time = g_get_monotonic_time();
    return ctx;
}

static void workflow_context_free(WorkflowContext *ctx) {
    if (!ctx) return;
    g_free(ctx->data);
    g_free(ctx);
}

/* ============================================================================
 * 状态处理函数
 * ========================================================================== */

/**
 * 空闲状态：等待启动命令
 */
static gboolean idle_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        g_print("\n╔══════════════════════════════════════════╗\n");
        g_print("║   工作流引擎 - 空闲状态                  ║\n");
        g_print("╚══════════════════════════════════════════╝\n");
        return TRUE;
    }
    
    if (type == HSM_EVT_START) {
        g_print("✓ 收到启动命令，开始工作流...\n");
        ctx->current_step = 0;
        ctx->retry_count = 0;
        ctx->start_time = g_get_monotonic_time();
        hsm_change_state(h, "initializing");
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 初始化状态：准备资源
 */
static gboolean initializing_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->current_step = 1;
        g_print("\n[步骤 %d/%d] 初始化阶段\n", ctx->current_step, ctx->total_steps);
        g_print("  → 分配内存...\n");
        g_print("  → 初始化配置...\n");
        
        /* 模拟异步初始化，1秒后完成 */
        hsm_schedule_timer(h, 1000);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        g_print("  ✓ 初始化完成\n");
        hsm_change_state(h, "loading");
        return TRUE;
    }
    
    if (type == HSM_EVT_CANCEL) {
        g_print("  ✗ 初始化被取消\n");
        hsm_change_state(h, "cleanup");
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 加载数据状态：从文件/网络加载数据
 */
static gboolean loading_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    /* 两个定时器：超时定时器与模拟成功定时器 */
    static gint timeout_timer = 0;
    static gint success_timer = 0;
    
    if (type == HSM_EVT_ENTRY) {
        ctx->current_step = 2;
        g_print("\n[步骤 %d/%d] 加载数据阶段\n", ctx->current_step, ctx->total_steps);
        g_print("  → 连接数据源...\n");
        
        /* 设置 3 秒超时 */
        timeout_timer = hsm_schedule_timer(h, 3000);
        
        /* 模拟随机行为：70% 成功；30% 延迟导致超时 */
        if (g_random_int_range(0, 10) < 7) {
            /* 1.5 秒后模拟成功：这里用一个本地定时器，然后在超时回调中转发 RESULT_OK。
             * 我们复用当前状态的 HSM 定时器机制：约定 success_timer 触发时，
             * 当前状态收到 HSM_EVT_TIMEOUT 后自行判定来源并转发 RESULT_OK。 */
            success_timer = hsm_schedule_timer(h, 1500);
            g_print("  → 正在加载数据（预计 1.5s）...\n");
        } else {
            g_print("  ⚠ 模拟网络延迟，可能超时...\n");
        }
        
        return TRUE;
    }
    
    if (type == HSM_EVT_RESULT_OK) {
        if (timeout_timer > 0) {
            hsm_cancel_timer(h, timeout_timer);
            timeout_timer = 0;
        }
        if (success_timer > 0) {
            hsm_cancel_timer(h, success_timer);
            success_timer = 0;
        }
        
        ctx->data = g_strdup("Sample Data [1234567890]");
        g_print("  ✓ 数据加载成功: %s\n", ctx->data);
        ctx->retry_count = 0;  /* 重置重试计数 */
        hsm_change_state(h, "validating");
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        /* 判断是哪一个定时器触发：
         * 简化处理：若 success_timer 仍然有效，则认为这是“成功”定时器触发，
         * 我们转换为 RESULT_OK 事件处理；否则认为是“总超时”触发。 */
        if (success_timer > 0) {
            /* 清理 success_timer，自行投递 RESULT_OK */
            hsm_cancel_timer(h, success_timer);
            success_timer = 0;
            HsmEvent *ok = hsm_event_new(HSM_EVT_RESULT_OK, "load_complete", NULL, "loader", 0);
            hsm_post_event(h, ok);
            return TRUE;
        }
        
        g_print("  ✗ 数据加载超时\n");
        
        if (ctx->retry_count < ctx->max_retries) {
            ctx->retry_count++;
            g_print("  ↻ 重试 %d/%d...\n", ctx->retry_count, ctx->max_retries);
            hsm_change_state(h, "loading");  /* 重新进入加载状态 */
        } else {
            g_print("  ✗ 达到最大重试次数，失败\n");
            hsm_change_state(h, "error");
        }
        return TRUE;
    }
    
    if (type == HSM_EVT_EXIT) {
        if (timeout_timer > 0) {
            hsm_cancel_timer(h, timeout_timer);
            timeout_timer = 0;
        }
        if (success_timer > 0) {
            hsm_cancel_timer(h, success_timer);
            success_timer = 0;
        }
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 验证数据状态
 */
static gboolean validating_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->current_step = 3;
        g_print("\n[步骤 %d/%d] 验证数据阶段\n", ctx->current_step, ctx->total_steps);
        g_print("  → 检查数据完整性...\n");
        g_print("  → 验证数据格式...\n");
        
        /* 0.5秒后完成验证 */
        hsm_schedule_timer(h, 500);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        if (ctx->data && g_utf8_strlen(ctx->data, -1) > 0) {
            g_print("  ✓ 数据验证通过\n");
            hsm_change_state(h, "processing");
        } else {
            g_print("  ✗ 数据验证失败\n");
            hsm_change_state(h, "error");
        }
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 处理数据状态
 */
static gboolean processing_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    static gint progress = 0;
    static gint progress_timer = 0;
    
    if (type == HSM_EVT_ENTRY) {
        ctx->current_step = 4;
        g_print("\n[步骤 %d/%d] 处理数据阶段\n", ctx->current_step, ctx->total_steps);
        g_print("  → 开始数据处理...\n");
        
        progress = 0;
        /* 每0.5秒更新一次进度 */
        progress_timer = hsm_schedule_timer(h, 500);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        progress += 25;
        g_print("  → 处理进度: %d%%\n", progress);
        
        if (progress >= 100) {
            g_print("  ✓ 数据处理完成\n");
            hsm_change_state(h, "saving");
        } else {
            /* 继续下一个进度更新 */
            progress_timer = hsm_schedule_timer(h, 500);
        }
        return TRUE;
    }
    
    /* 支持暂停 */
    if (type == HSM_EVT_STEP && hsm_event_get_data(ev)) {
        const gchar *cmd = hsm_event_get_data(ev);
        if (g_strcmp0(cmd, "pause") == 0) {
            g_print("  ⏸ 处理已暂停\n");
            hsm_cancel_timer(h, progress_timer);
            ctx->paused = TRUE;
            return TRUE;
        } else if (g_strcmp0(cmd, "resume") == 0) {
            g_print("  ▶ 处理已恢复\n");
            ctx->paused = FALSE;
            progress_timer = hsm_schedule_timer(h, 500);
            return TRUE;
        }
    }
    
    if (type == HSM_EVT_EXIT) {
        if (progress_timer > 0) {
            hsm_cancel_timer(h, progress_timer);
            progress_timer = 0;
        }
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 保存结果状态
 */
static gboolean saving_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->current_step = 5;
        g_print("\n[步骤 %d/%d] 保存结果阶段\n", ctx->current_step, ctx->total_steps);
        g_print("  → 连接数据库...\n");
        g_print("  → 写入结果...\n");
        
        /* 1秒后保存完成 */
        hsm_schedule_timer(h, 1000);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        g_print("  ✓ 结果保存成功\n");
        hsm_change_state(h, "cleanup");
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 清理状态
 */
static gboolean cleanup_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        ctx->current_step = 6;
        g_print("\n[步骤 %d/%d] 清理阶段\n", ctx->current_step, ctx->total_steps);
        g_print("  → 释放资源...\n");
        g_print("  → 关闭连接...\n");
        
        hsm_schedule_timer(h, 500);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
    guint64 elapsed = (g_get_monotonic_time() - ctx->start_time) / 1000000;
        
        g_print("  ✓ 清理完成\n");
        g_print("\n╔══════════════════════════════════════════╗\n");
        g_print("║   工作流执行完成！                       ║\n");
    g_print("║   总耗时: %" G_GUINT64_FORMAT " 秒                        ║\n", elapsed);
        g_print("╚══════════════════════════════════════════╝\n\n");
        
        /* 停止事件循环 */
        hsm_stop(h);
        return TRUE;
    }
    
    return FALSE;
}

/**
 * 错误状态
 */
static gboolean error_state_handler(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data) {
    WorkflowContext *ctx = user_data;
    HsmEventType type = hsm_event_get_type(ev);
    
    if (type == HSM_EVT_ENTRY) {
        g_print("\n╔══════════════════════════════════════════╗\n");
        g_print("║   ✗ 工作流执行失败                       ║\n");
        g_print("║   失败步骤: %d/%d                        ║\n", ctx->current_step, ctx->total_steps);
        g_print("║   重试次数: %d/%d                        ║\n", ctx->retry_count, ctx->max_retries);
        g_print("╚══════════════════════════════════════════╝\n\n");
        
        /* 清理并退出 */
        hsm_schedule_timer(h, 1000);
        return TRUE;
    }
    
    if (type == HSM_EVT_TIMEOUT) {
        hsm_change_state(h, "cleanup");
        return TRUE;
    }
    
    return FALSE;
}

/* ============================================================================
 * 主函数
 * ========================================================================== */

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    init_console_utf8();
    
    g_print("╔════════════════════════════════════════════════╗\n");
    g_print("║   HSM 工作流引擎示例                           ║\n");
    g_print("║   演示：复杂多步骤工作流的状态管理             ║\n");
    g_print("╚════════════════════════════════════════════════╝\n");
    
    /* 创建工作流上下文 */
    WorkflowContext *ctx = workflow_context_new();
    
    /* 创建状态机 */
    Hsm *workflow = hsm_new("workflow", FALSE);
    
    /* 注册所有状态 */
    hsm_register_state(workflow, "idle", idle_state_handler, ctx);
    hsm_register_state(workflow, "initializing", initializing_state_handler, ctx);
    hsm_register_state(workflow, "loading", loading_state_handler, ctx);
    hsm_register_state(workflow, "validating", validating_state_handler, ctx);
    hsm_register_state(workflow, "processing", processing_state_handler, ctx);
    hsm_register_state(workflow, "saving", saving_state_handler, ctx);
    hsm_register_state(workflow, "cleanup", cleanup_state_handler, ctx);
    hsm_register_state(workflow, "error", error_state_handler, ctx);
    
    /* 设置初始状态 */
    hsm_change_state(workflow, "idle");
    
    /* 启动状态机 */
    hsm_start(workflow, FALSE);
    
    /* 投递启动事件 */
    HsmEvent *start_ev = hsm_event_new(HSM_EVT_START, "workflow_start", NULL, "main", 0);
    hsm_post_event(workflow, start_ev);
    
    /* 运行主循环（阻塞直到工作流完成） */
    hsm_run(workflow);
    
    /* 清理资源 */
    hsm_destroy(workflow);
    workflow_context_free(ctx);
    
    return 0;
}
