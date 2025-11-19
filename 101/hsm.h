#ifndef HSM_H
#define HSM_H

#include <glib.h>

G_BEGIN_DECLS

/* ============================================================================
 * 类型定义
 * ========================================================================== */

/* HSM 状态机实例（不透明类型） */
typedef struct Hsm Hsm;

/* HSM 事件对象（不透明类型） */
typedef struct HsmEvent HsmEvent;

/* ============================================================================
 * 枚举定义
 * ========================================================================== */

/* HSM 事件类型枚举 */
typedef enum {
    HSM_EVT_START,            /* 启动/开始动作 */
    HSM_EVT_STEP,             /* 逐步推进（示例事件） */
    HSM_EVT_RESULT_OK,        /* 结果成功 */
    HSM_EVT_RESULT_ERROR,     /* 结果失败 */
    HSM_EVT_TIMEOUT,          /* 定时器超时事件（由 HSM 定时器发出） */
    HSM_EVT_TIMEOUT_HANDLED,  /* 超时处理完成的确认事件（示例） */
    HSM_EVT_CANCEL,           /* 取消 */
    HSM_EVT_ENTRY,            /* 进入状态 */
    HSM_EVT_EXIT,             /* 退出状态 */
} HsmEventType;

/* ============================================================================
 * 回调函数类型定义
 * ========================================================================== */

/**
 * 状态处理函数原型
 * @param h 当前 HSM 实例
 * @param state 当前状态名（与注册时一致）
 * @param ev 到达该状态的事件
 * @param user_data 注册状态时提供的用户数据
 * @return TRUE 表示事件已被该状态消费，不再向父 HSM 冒泡；FALSE 表示未处理，若存在父 HSM 将向其冒泡
 */
typedef gboolean (*HsmStateHandler)(Hsm *h, const gchar *state, HsmEvent *ev, gpointer user_data);

/* ============================================================================
 * HSM 生命周期管理
 * ========================================================================== */

/**
 * 创建 HSM 实例
 * @param name HSM 名称（用于日志/调试）
 * @param use_own_context 若为 TRUE，为该 HSM 创建独立的 GMainContext 与 GMainLoop；
 *                        若为 FALSE，共享默认主上下文（default context）
 * @return 新创建的 HSM 实例，需要通过 hsm_destroy() 释放
 */
Hsm *hsm_new(const gchar *name, gboolean use_own_context);

/**
 * 销毁 HSM 实例
 * @param h 要销毁的 HSM 实例
 */
void hsm_destroy(Hsm *h);

/* ============================================================================
 * HSM 层级结构管理
 * ========================================================================== */

/**
 * 设定父 HSM（层级结构）
 * @param h 子 HSM 实例
 * @param parent 父 HSM 实例
 * @note 子 HSM 未消费的事件（返回 FALSE）将自动冒泡到父 HSM 处理
 */
void hsm_set_parent(Hsm *h, Hsm *parent);

/**
 * 获取父 HSM
 * @param h HSM 实例
 * @return 父 HSM 实例（可能为 NULL，调用方只借用指针，不持有所有权）
 */
Hsm *hsm_get_parent(Hsm *h);

/**
 * 获取 HSM 名称
 * @param h HSM 实例
 * @return HSM 名称（只读指针，生命周期由 HSM 管理）
 */
const gchar *hsm_get_name(Hsm *h);

/* ============================================================================
 * HSM 状态管理
 * ========================================================================== */

/**
 * 注册状态与处理函数
 * @param h HSM 实例
 * @param state_name 状态名（字符串）
 * @param handler 该状态的事件处理函数
 * @param user_data 透传给 handler 的自定义指针
 */
void hsm_register_state(Hsm *h, const gchar *state_name, HsmStateHandler handler, gpointer user_data);

/**
 * 状态切换（触发 EXIT/ENTRY 回调）
 * @param h HSM 实例
 * @param new_state 新状态名
 * @note 线程语义：建议在 HSM 对应的上下文线程中调用；
 *       若不在该上下文中，函数会自动将切换请求投递到 HSM 上下文中异步执行
 */
void hsm_change_state(Hsm *h, const gchar *new_state);

/**
 * 异步状态切换
 * @param h HSM 实例
 * @param new_state 新状态名
 */
void hsm_post_change_state(Hsm *h, const gchar *new_state);

/* ============================================================================
 * HSM 事件循环管理
 * ========================================================================== */

/**
 * 启动事件循环
 * @param h HSM 实例
 * @param run_in_thread 若为 TRUE，在独立线程中运行事件循环；
 *                      若为 FALSE，不创建线程，调用方后续需自行调用 hsm_run(h) 在当前线程阻塞运行
 */
void hsm_start(Hsm *h, gboolean run_in_thread);

/**
 * 停止事件循环
 * @param h HSM 实例
 */
void hsm_stop(Hsm *h);

/**
 * 在调用方线程运行循环（阻塞）
 * @param h HSM 实例
 */
void hsm_run(Hsm *h);

/* ============================================================================
 * HSM 事件管理
 * ========================================================================== */

/**
 * 创建事件对象
 * @param type 事件类型
 * @param name 可选的事件名（字符串，内部会拷贝）
 * @param data 事件携带的数据指针（由调用方自行管理生命周期）
 * @param source 事件来源（字符串，可选，内部会拷贝）
 * @param seq 序列号/标识
 * @return 新创建的事件对象
 * @note 所有权规则：调用 hsm_post_event() 后，事件指针的所有权转移给 HSM，调用方不再需要释放
 */
HsmEvent *hsm_event_new(HsmEventType type, const gchar *name, gpointer data, const gchar *source, gint seq);

/**
 * 释放事件对象
 * @param ev 要释放的事件对象
 */
void hsm_event_free(HsmEvent *ev);

/**
 * 获取事件类型
 * @param ev 事件对象
 * @return 事件类型
 */
HsmEventType hsm_event_get_type(const HsmEvent *ev);

/**
 * 获取事件名称
 * @param ev 事件对象
 * @return 事件名称（返回的字符串指针由事件管理，调用方勿释放）
 */
const gchar *hsm_event_get_name(const HsmEvent *ev);

/**
 * 获取事件来源
 * @param ev 事件对象
 * @return 事件来源（返回的字符串指针由事件管理，调用方勿释放）
 */
const gchar *hsm_event_get_source(const HsmEvent *ev);

/**
 * 获取事件序列号
 * @param ev 事件对象
 * @return 事件序列号
 */
gint hsm_event_get_seq(const HsmEvent *ev);

/**
 * 获取事件数据
 * @param ev 事件对象
 * @return 事件数据指针
 */
gpointer hsm_event_get_data(const HsmEvent *ev);

/**
 * 将事件异步投递到 HSM
 * @param h HSM 实例
 * @param ev 事件对象（本函数接管 ev 的所有权）
 */
void hsm_post_event(Hsm *h, HsmEvent *ev);

/* ============================================================================
 * HSM 定时器管理
 * ========================================================================== */

/**
 * 安排一次性定时器
 * @param h HSM 实例
 * @param ms 超时时间（毫秒）
 * @return 正整数定时器 ID，可用于取消
 * @note 在 HSM 的 GMainContext 上安排一次性定时器，到期后向 HSM 投递 HSM_EVT_TIMEOUT
 */
gint hsm_schedule_timer(Hsm *h, guint ms);

/**
 * 取消定时器
 * @param h HSM 实例
 * @param timer_id 定时器 ID
 * @return 取消成功返回 TRUE，否则返回 FALSE
 */
gboolean hsm_cancel_timer(Hsm *h, gint timer_id);

/* ============================================================================
 * HSM 状态查询
 * ========================================================================== */

/**
 * 获取当前状态（线程安全：内部复制字符串）
 * @param h HSM 实例
 * @return 当前状态字符串（需要调用方使用 g_free() 释放）
 */
gchar *hsm_get_state_copy(Hsm *h);

G_END_DECLS

#endif /* HSM_H */
