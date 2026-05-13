#ifndef LRU_HPP
#define LRU_HPP

#include "store.hpp"
#include <cstddef>

// Default max memory before eviction (Demo Mode: 100KB, Actual mode: 64 MB)
static constexpr size_t LRU_MAX_MEMORY = 64 * 1024 * 1024;

extern DrutaNode *HEAD;
extern DrutaNode *TAIL;
extern size_t lru_size;
extern size_t current_memory_usage;

void add_lru(DrutaNode *node);
void update_lru(DrutaNode *node, DrutaValue value);
void notify_memory_change(size_t old_size, size_t new_size);
void touch_lru(DrutaNode *node);
void remove_from_lru(DrutaNode *node);
void evict_lru();
void clear_lru();

#endif