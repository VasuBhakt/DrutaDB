#ifndef LRU_HPP
#define LRU_HPP

#include "store.hpp"
#include <cstddef>

// Default max memory before eviction (e.g., 64MB)
static constexpr size_t LRU_MAX_MEMORY = 64 * 1024 * 1024;

extern DrutaNode *HEAD;
extern DrutaNode *TAIL;
extern size_t lru_size;
extern size_t current_memory_usage;

// Insert or update. Handles memory tracking and eviction.
DrutaNode *update_lru(const std::string &key, DrutaValue value,
                      DrutaNode *node = nullptr);

void notify_memory_change(size_t old_size, size_t new_size);
void touch_lru(DrutaNode *node);
void remove_from_lru(DrutaNode *node);
void evict_lru();
void clear_lru();

#endif