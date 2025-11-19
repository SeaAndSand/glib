#include <glib.h>
#include "common.h"

// 题目：用GHashTable实现LRU缓存的核心数据结构。
typedef struct {
    GHashTable *table;
    GList *lru_list;
    int capacity;
} LRUCache;

LRUCache* lru_cache_create(int capacity) {
    LRUCache *cache = g_new0(LRUCache, 1);
    cache->table = g_hash_table_new(g_str_hash, g_str_equal);
    cache->lru_list = NULL;
    cache->capacity = capacity;
    return cache;
}

void lru_cache_put(LRUCache *cache, const char *key, gpointer value) {
    if (g_hash_table_contains(cache->table, key)) {
        // 更新已有的键值对
        g_hash_table_replace(cache->table, g_strdup(key), value);
        // 更新LRU列表
        cache->lru_list = g_list_remove(cache->lru_list, key);
        cache->lru_list = g_list_prepend(cache->lru_list, g_strdup(key));
    } else {
        // 插入新的键值对
        if (g_hash_table_size(cache->table) >= cache->capacity) {
            // 移除最久未使用的键值对
            GList *lru_node = g_list_last(cache->lru_list);
            char *lru_key = lru_node->data;
            g_hash_table_remove(cache->table, lru_key);
            cache->lru_list = g_list_remove(cache->lru_list, lru_key);
            g_free(lru_key);
        }
        g_hash_table_insert(cache->table, g_strdup(key), value);
        cache->lru_list = g_list_prepend(cache->lru_list, g_strdup(key));
    }
}

gpointer lru_cache_get(LRUCache *cache, const char *key) {
    gpointer value = g_hash_table_lookup(cache->table, key);
    if (value != NULL) {
        // 更新LRU列表
        cache->lru_list = g_list_remove(cache->lru_list, key);
        cache->lru_list = g_list_prepend(cache->lru_list, g_strdup(key));
    }
    return value;
}

void lru_cache_destroy(LRUCache *cache) {
    GList *iter = cache->lru_list;
    while (iter != NULL) {
        char *key = iter->data;
        g_free(key);
        iter = iter->next;
    }
    g_list_free(cache->lru_list);
    g_hash_table_destroy(cache->table);
    g_free(cache);
}

int main() {
    init_console_utf8();
    
    LRUCache *cache = lru_cache_create(3);
    lru_cache_put(cache, "one", GINT_TO_POINTER(1));
    lru_cache_put(cache, "two", GINT_TO_POINTER(2));
    lru_cache_put(cache, "three", GINT_TO_POINTER(3));
    lru_cache_put(cache, "four", GINT_TO_POINTER(4)); // 这会移除键 "one"
    // 遍历打印内容
    GList *iter = cache->lru_list;
    while (iter != NULL) {
        char *key = iter->data;
        gpointer value = g_hash_table_lookup(cache->table, key);
        g_print("Key: %s, Value: %d\n", key, GPOINTER_TO_INT(value));
        iter = iter->next;
    }

    // lru_cache_destroy(cache);

    return 0;
}