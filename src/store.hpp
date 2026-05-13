#ifndef STORE_HPP
#define STORE_HPP

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include "drutahash.hpp"

enum class ValueType { STRING, LIST, HASH, SET };

struct DrutaValue {
  ValueType type;
  std::variant<std::string, std::deque<std::string>, DrutaHash> data;
  long long expiry_time;
  size_t memory_usage;

  static size_t calc_string_usage(const std::string &s) { return s.capacity(); }
  // DrutaValue()
  //     : type(ValueType::STRING), data(std::string("")), expiry_time(0),
  //       memory_usage(0) {}

  // Constructor for STRING
  DrutaValue(std::string d, long long e = 0)
      : type(ValueType::STRING), data(std::move(d)), expiry_time(e) {
    memory_usage = sizeof(std::string) + calc_string_usage(d);
  }

  // Constructor for LIST
  DrutaValue(std::deque<std::string> l, long long e = 0)
      : type(ValueType::LIST), data(std::move(l)), expiry_time(e) {
    memory_usage = 0;
    const auto &list = std::get<std::deque<std::string>>(data);
    for (const auto &s : list) {
      // We add 8 bytes for the deque's internal pointer tracking
      memory_usage += calc_string_usage(s) + sizeof(std::string) + sizeof(void*);
    }
  }

  // Constructor for SET
  DrutaValue(std::set<std::string> s, long long e = 0)
      : type(ValueType::SET), data(std::move(s)), expiry_time(e) {
    memory_usage = 0;
    const auto &set = std::get<std::set<std::string>>(data);
    for (const auto &str : set) {
      memory_usage += str.capacity() + sizeof(std::string) + 8*(sizeof(void*)); // hash entry overhead
    }
  }

  // Explicit constructor for creating empty types
  explicit DrutaValue(ValueType t) : type(t), expiry_time(0), memory_usage(0) {
    if (type == ValueType::STRING)
      data = std::string("");
    else if (type == ValueType::LIST)
      data = std::deque<std::string>{};
    else if (type == ValueType::HASH)
      data = DrutaHash(std::vector<std::pair<std::string, std::string>>{});
    else if (type == ValueType::SET)
      data = std::set<std::string>{};
  }
};

struct DrutaNode {
  std::string key;
  DrutaNode *next = nullptr;
  DrutaNode *prev = nullptr;
  DrutaValue value;

  DrutaNode(std::string k, DrutaValue v)
      : key(std::move(k)), value(std::move(v)) {}

  size_t get_memory_usage() const {
    return sizeof(DrutaNode) + key.capacity() + value.memory_usage;
  }
};

extern std::map<std::string, std::unique_ptr<DrutaNode>> kv_store;

#endif
