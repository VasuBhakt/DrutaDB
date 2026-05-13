#include "lru.hpp"
#include "store.hpp"
#include <iostream>

DrutaNode *HEAD = nullptr;
DrutaNode *TAIL = nullptr;
size_t lru_size = 0;
size_t current_memory_usage = 0;

static void detach(DrutaNode *node) {
  if (!node)
    return;

  DrutaNode *prev_node = node->prev;
  DrutaNode *next_node = node->next;

  if (prev_node)
    prev_node->next = next_node;
  else
    TAIL = next_node;

  if (next_node)
    next_node->prev = prev_node;
  else
    HEAD = prev_node;

  node->prev = nullptr;
  node->next = nullptr;
}

static void attach_head(DrutaNode *node) {
  if (!HEAD) {
    HEAD = TAIL = node;
  } else {
    node->prev = HEAD;
    HEAD->next = node;
    HEAD = node;
  }
}

void add_lru(DrutaNode *node) {
  if (!node) return;

  // Add to global memory usage
  current_memory_usage += node->get_memory_usage();
  attach_head(node);
  lru_size++;

  // Evict until we are under the memory limit
  while (current_memory_usage > LRU_MAX_MEMORY && TAIL) {
    evict_lru();
  }
}

void update_lru(DrutaNode *node, DrutaValue value) {
  if (!node) return;

  size_t old_size = node->get_memory_usage();
  node->value = std::move(value);
  size_t new_size = node->get_memory_usage();

  notify_memory_change(old_size, new_size);
  touch_lru(node);
}

void notify_memory_change(size_t old_size, size_t new_size) {
  if (new_size > old_size) {
    current_memory_usage += (new_size - old_size);
  } else {
    size_t diff = old_size - new_size;
    if (current_memory_usage > diff) {
      current_memory_usage -= diff;
    } else {
      current_memory_usage = 0;
    }
  }

  while (current_memory_usage > LRU_MAX_MEMORY && TAIL) {
    evict_lru();
  }
}

void touch_lru(DrutaNode *node) {
  if (!node || node == HEAD)
    return;
  detach(node);
  attach_head(node);
}

void remove_from_lru(DrutaNode *node) {
  if (!node)
    return;
  current_memory_usage -= node->get_memory_usage();
  detach(node);
  lru_size--;
}

void evict_lru() {
  if (!TAIL)
    return;

  DrutaNode *least_used_node = TAIL;
  std::cout << "[LRU] Evicting key: " << least_used_node->key
            << " (Size: " << least_used_node->get_memory_usage() << " bytes)"
            << std::endl;

  current_memory_usage -= least_used_node->get_memory_usage();
  detach(least_used_node);
  kv_store.erase(least_used_node->key);
  lru_size--;
}

void clear_lru() {
  // Nodes are owned by kv_store unique_ptr, so we just reset the LRU pointers.
  HEAD = nullptr;
  TAIL = nullptr;
  lru_size = 0;
  current_memory_usage = 0;
}
