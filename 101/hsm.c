/**
 * 轻量层级状态机（HSM）实现，基于 GLib 主循环/上下文
 * 
 * 特性：
 * - 支持在独立或共享的 GMainContext 上运行
 * - 支持状态注册、状态切换（ENTRY/EXIT）、事件投递与冒泡、一次性定时器
 * - 线程语义：事件投递与状态切换都在 HSM 的 GMainContext 线程中执行
 */
#include "hsm.h"

/* ============================================================================
 * 内部数据结构定义
 * ========================================================================== */

/**
 * 事件对象内部结构（对外不透明）
 */
struct HsmEvent {
    /* 事件属性 */
    HsmEventType type;      /* 事件类型 */
    gchar *name;            /* 事件名称（可选） */
    gchar *source;          /* 事件来源（可选） */
    gint seq;               /* 序列号 */
    
    /* 事件数据 */
    gpointer data;          /* 事件携带的数据指针 */
};

/**
 * 状态对象内部结构
 */
struct HsmState {
    /* 状态标识 */
    gchar *name;            /* 状态名称 */
    
    /* 状态处理 */
    HsmStateHandler handler; /* 状态处理函数 */
    gpointer user_data;     /* 用户数据 */
};

/**
 * HSM 核心结构体
 */
struct Hsm {
    /* 基本信息 */
    gchar *name;            /* HSM 名称 */
    gchar *current_state;   /* 当前状态名 */
    
    /* 事件循环相关 */
    GMainContext *context;  /* GLib 主上下文 */
    GMainLoop *loop;        /* GLib 主循环 */
    GThread *thread;        /* 运行线程（若独立运行） */
    gboolean running;       /* 运行状态标志 */
    
    /* 层级结构 */
    Hsm *parent;            /* 父 HSM 实例 */
    
    /* 状态管理 */
    GHashTable *states;     /* 状态表：状态名 -> HsmState* */
    
    /* 定时器管理 */
    GHashTable *timers;     /* 定时器表：timer_id -> GSource* */
    gint next_timer_id;     /* 下一个定时器 ID */
    
    /* 线程安全 */
    GMutex lock;            /* 互斥锁 */
};

typedef struct {
    Hsm *h;
    gchar *ns;
} ChangeReq;

/* ============================================================================
 * 内部辅助函数声明
 * ========================================================================== */

/* 状态对象管理 */
static struct HsmState *hsm_state_new(const gchar *name, HsmStateHandler handler, gpointer user_data);
static void hsm_state_free(gpointer p);

/* 状态切换内部实现 */
static void hsm_change_state_internal(Hsm *h, const gchar *new_state);
static gboolean hsm_change_state_invoke_cb(gpointer data);

/* 事件处理 */
static gboolean hsm_process_post(gpointer data);

/* 定时器回调 */
static gboolean hsm_timer_source_cb(gpointer user_data);

/* 线程函数 */
static gpointer hsm_thread_func(gpointer data);

/* ============================================================================
 * 内部辅助函数实现
 * ========================================================================== */

/**
 * 创建状态对象
 */
static struct HsmState *hsm_state_new(const gchar *name, HsmStateHandler handler, gpointer user_data) {
    struct HsmState *s = g_new0(struct HsmState, 1);
    s->name = g_strdup(name);
    s->handler = handler;
    s->user_data = user_data;
    return s;
}

/**
 * 释放状态对象
 */
static void hsm_state_free(gpointer p) {
    struct HsmState *s = p;
    if (!s) return;
    // g_free(s->name);
    g_free(s);
}

/* ============================================================================
 * 事件对象 API 实现
 * ========================================================================== */

HsmEvent *hsm_event_new(HsmEventType type, const gchar *name, gpointer data, const gchar *source, gint seq) {
    HsmEvent *e = g_new0(HsmEvent, 1);
    e->type = type;
    e->name = name ? g_strdup(name) : NULL;
    e->data = data;
    e->seq = seq;
    e->source = source ? g_strdup(source) : NULL;
    return e;
}

void hsm_event_free(HsmEvent *ev) {
    if (!ev) return;
    g_free(ev->name);
    g_free(ev->source);
    /* 不释放 ev->data，由调用方自行管理 */
    g_free(ev);
}

HsmEventType hsm_event_get_type(const HsmEvent *ev) {
    return ev ? ev->type : 0;
}

const gchar *hsm_event_get_name(const HsmEvent *ev) {
    return ev ? ev->name : NULL;
}

const gchar *hsm_event_get_source(const HsmEvent *ev) {
    return ev ? ev->source : NULL;
}

gint hsm_event_get_seq(const HsmEvent *ev) {
    return ev ? ev->seq : 0;
}

gpointer hsm_event_get_data(const HsmEvent *ev) {
    return ev ? ev->data : NULL;
}

/* ============================================================================
 * HSM 生命周期管理 API 实现
 * ========================================================================== */

Hsm *hsm_new(const gchar *name, gboolean use_own_context) {
    Hsm *h = g_new0(Hsm, 1);
    
    /* 初始化基本信息 */
    h->name = g_strdup(name);
    h->current_state = NULL;
    
    /* 初始化事件循环 */
    if (use_own_context) {
        /* 创建独立上下文 */
        h->context = g_main_context_new();
    } else {
        /* 共享默认上下文：获取 default context 并引用一次 */
        GMainContext *def = g_main_context_default();
        if (def) {
            g_main_context_ref(def);
        }
        h->context = def;
    }
    h->loop = g_main_loop_new(h->context, FALSE);
    h->thread = NULL;
    h->running = FALSE;
    
    /* 初始化层级结构 */
    h->parent = NULL;
    
    /* 初始化状态表 */
    h->states = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, hsm_state_free);
    
    /* 初始化定时器表 */
    h->timers = g_hash_table_new(g_direct_hash, g_direct_equal);
    h->next_timer_id = 1;
    
    /* 初始化线程安全 */
    g_mutex_init(&h->lock);
    
    return h;
}

void hsm_destroy(Hsm *h) {
    if (!h) {
        return;
    }
    
    /* 先停止事件循环和线程 */
    hsm_stop(h);
    if (h->thread) {
        g_thread_join(h->thread);
        h->thread = NULL;
    }
    
    /* 清理事件循环资源 */
    if (h->loop) {
        g_main_loop_unref(h->loop);
        h->loop = NULL;
    }
    if (h->context) {
        g_main_context_unref(h->context);
        h->context = NULL;
    }
    
    /* 清理状态表和定时器表 */
    g_hash_table_destroy(h->states);
    g_hash_table_destroy(h->timers);
    
    /* 清理字符串资源 */
    g_free(h->current_state);
    g_free(h->name);
    
    /* 清理互斥锁 */
    g_mutex_clear(&h->lock);
    
    /* 释放 HSM 结构体 */
    g_free(h);
}

/* ============================================================================
 * HSM 层级结构管理 API 实现
 * ========================================================================== */

void hsm_set_parent(Hsm *h, Hsm *parent) {
    if (h) {
        h->parent = parent;
    }
}

Hsm *hsm_get_parent(Hsm *h) {
    return h ? h->parent : NULL;
}

const gchar *hsm_get_name(Hsm *h) {
    return h ? h->name : NULL;
}

/* ============================================================================
 * HSM 状态管理 API 实现
 * ========================================================================== */

void hsm_register_state(Hsm *h, const gchar *state_name, HsmStateHandler handler, gpointer user_data) {
    if (!h || !state_name) {
        return;
    }
    
    struct HsmState *s = hsm_state_new(state_name, handler, user_data);
    g_hash_table_insert(h->states, s->name, s);
}

/**
 * 状态切换内部实现（必须在 HSM 上下文线程中执行）
 */
static void hsm_change_state_internal(Hsm *h, const gchar *new_state) {
    /* 如果状态相同，不需要切换 */
    if (g_strcmp0(h->current_state, new_state) == 0) {
        return;
    }
    
    /* 退出旧状态 */
    if (h->current_state) {
        struct HsmState *old = g_hash_table_lookup(h->states, h->current_state);
        if (old && old->handler) {
            HsmEvent *ev = hsm_event_new(HSM_EVT_EXIT, NULL, NULL, h->name, 0);
            old->handler(h, h->current_state, ev, old->user_data);
            hsm_event_free(ev);
        }
    }
    
    /* 切换到新状态 */
    g_free(h->current_state);
    h->current_state = g_strdup(new_state);
    
    /* 进入新状态 */
    struct HsmState *newst = g_hash_table_lookup(h->states, h->current_state);
    if (newst && newst->handler) {
        HsmEvent *ev = hsm_event_new(HSM_EVT_ENTRY, NULL, NULL, h->name, 0);
        newst->handler(h, h->current_state, ev, newst->user_data);
        hsm_event_free(ev);
    }
}

void hsm_change_state(Hsm *h, const gchar *new_state) {
    if (!h || !new_state) {
        return;
    }
    
    /* 若当前线程绑定的默认上下文就是目标 context，则直接执行；否则投递到目标 context */
    if (g_main_context_get_thread_default() == h->context) {
        hsm_change_state_internal(h, new_state);
        return;
    }
    
    /* 异步投递状态切换 */
    
    ChangeReq *req = g_new0(ChangeReq, 1);
    req->h = h;
    req->ns = g_strdup(new_state);
    g_main_context_invoke(h->context, hsm_change_state_invoke_cb, req);
}

void hsm_post_change_state(Hsm *h, const gchar *new_state) {
    hsm_change_state(h, new_state);
}

/**
 * 在 HSM 上下文线程中执行状态切换的回调
 */
static gboolean hsm_change_state_invoke_cb(gpointer data) {    
    ChangeReq *r = (ChangeReq*)data;
    hsm_change_state_internal(r->h, r->ns);
    g_free(r->ns);
    g_free(r);
    return G_SOURCE_REMOVE;
}

/* ============================================================================
 * HSM 事件循环管理 API 实现
 * ========================================================================== */

/**
 * HSM 线程函数
 */
static gpointer hsm_thread_func(gpointer data) {
    Hsm *h = data;
    g_main_context_push_thread_default(h->context);
    h->running = TRUE;
    g_main_loop_run(h->loop);
    h->running = FALSE;
    g_main_context_pop_thread_default(h->context);
    return NULL;
}

void hsm_start(Hsm *h, gboolean run_in_thread) {
    if (!h) {
        return;
    }
    
    if (run_in_thread) {
        if (!h->thread) {
            h->thread = g_thread_new(h->name, hsm_thread_func, h);
        }
    } else {
        /* 调用方后续需自行调用 hsm_run(h) */
    }
}

void hsm_run(Hsm *h) {
    if (!h) {
        return;
    }
    
    /* 在调用方线程运行主循环（阻塞直到 hsm_stop 调用） */
    g_main_context_push_thread_default(h->context);
    h->running = TRUE;
    g_main_loop_run(h->loop);
    h->running = FALSE;
    g_main_context_pop_thread_default(h->context);
}

void hsm_stop(Hsm *h) {
    if (!h) {
        return;
    }
    
    if (h->loop && g_main_loop_is_running(h->loop)) {
        g_main_loop_quit(h->loop);
    }
}

/* ============================================================================
 * HSM 事件管理 API 实现
 * ========================================================================== */

/**
 * 事件投递内部结构
 */
typedef struct {
    Hsm *h;
    HsmEvent *ev;
} HsmPost;

/**
 * 在目标上下文中处理已投递事件
 */
static gboolean hsm_process_post(gpointer data) {
    HsmPost *p = data;
    Hsm *h = p->h;
    HsmEvent *ev = p->ev;

    /* 查找当前状态的处理函数 */
    struct HsmState *st = NULL;
    if (h->current_state) {
        st = g_hash_table_lookup(h->states, h->current_state);
    }

    /* 调用状态处理函数 */
    gboolean handled = FALSE;
    if (st && st->handler) {
        handled = st->handler(h, h->current_state, ev, st->user_data);
    }

    if (!handled && h->parent) {
        /* 未处理则冒泡到父 HSM：复制事件并投递到父 HSM 上下文 */
        HsmEvent *copy = hsm_event_new(ev->type, ev->name, ev->data, ev->source, ev->seq);
        HsmPost *pp = g_new0(HsmPost, 1);
        pp->h = h->parent;
        pp->ev = copy;
        g_main_context_invoke(h->parent->context, hsm_process_post, pp);
        hsm_event_free(ev);
    } else {
        /* 已消费或无父 HSM 可冒泡 */
        hsm_event_free(ev);
    }

    g_free(p);
    return G_SOURCE_REMOVE;
}

void hsm_post_event(Hsm *h, HsmEvent *ev) {
    if (!h || !ev) {
        return;
    }
    
    HsmPost *p = g_new0(HsmPost, 1);
    p->h = h;
    p->ev = ev;
    g_main_context_invoke(h->context, hsm_process_post, p);
}

/* ============================================================================
 * HSM 定时器管理 API 实现
 * ========================================================================== */

/**
 * 定时器上下文结构
 */
typedef struct {
    Hsm *h;
    gint timer_id;
} TimerCtx;

/**
 * 定时器超时回调函数
 */
static gboolean hsm_timer_source_cb(gpointer user_data) {
    TimerCtx *t = user_data;
    
    /* 创建超时事件并投递到 HSM */
    HsmEvent *ev = hsm_event_new(HSM_EVT_TIMEOUT, "TIMER_EXPIRED", NULL, t->h->name, t->timer_id);
    hsm_post_event(t->h, ev);
    
    /* 定时器触发后，移除 ID 与 Source 的映射 */
    g_hash_table_remove(t->h->timers, GINT_TO_POINTER(t->timer_id));
    
    g_free(t);
    return G_SOURCE_REMOVE;
}

gint hsm_schedule_timer(Hsm *h, guint ms) {
    if (!h) {
        return -1;
    }
    
    /* 创建 timeout GSource 并绑定到 HSM 的 context */
    gint tid = ++h->next_timer_id;
    TimerCtx *t = g_new0(TimerCtx, 1);
    t->h = h;
    t->timer_id = tid;
    
    GSource *src = g_timeout_source_new(ms);
    g_source_set_callback(src, hsm_timer_source_cb, t, NULL);
    g_source_attach(src, h->context);
    g_hash_table_insert(h->timers, GINT_TO_POINTER(tid), src);
    g_source_unref(src); /* 哈希表持有引用 */
    
    return tid;
}

gboolean hsm_cancel_timer(Hsm *h, gint timer_id) {
    if (!h) {
        return FALSE;
    }
    
    gpointer v = g_hash_table_lookup(h->timers, GINT_TO_POINTER(timer_id));
    if (!v) {
        return FALSE;
    }
    
    GSource *src = v;
    g_source_destroy(src);
    g_hash_table_remove(h->timers, GINT_TO_POINTER(timer_id));
    
    return TRUE;
}

/* ============================================================================
 * HSM 状态查询 API 实现
 * ========================================================================== */

gchar *hsm_get_state_copy(Hsm *h) {
    if (!h) {
        return g_strdup("");
    }
    
    g_mutex_lock(&h->lock);
    gchar *c = g_strdup(h->current_state ? h->current_state : "");
    g_mutex_unlock(&h->lock);
    
    return c;
}