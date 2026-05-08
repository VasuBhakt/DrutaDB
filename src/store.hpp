#ifndef STORE_HPP
#define STORE_HPP

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <variant>

enum class ValueType { STRING, LIST };

struct DrutaValue {
  ValueType type;
  std::variant<std::string, std::deque<std::string>> data;
  long long expiry_time;
  size_t memory_usage;

  static size_t calc_string_usage(const std::string &s) { return s.capacity(); }
  // DrutaValue()
  //     : type(ValueType::STRING), data(std::string("")), expiry_time(0),
  //       memory_usage(0) {}

  // Constructor for STRING
  DrutaValue(std::string d, long long e = 0)
      : type(ValueType::STRING), data(std::move(d)), expiry_time(e) {
    memory_usage = std::get<std::string>(data).capacity();
  }

  // Constructor for LIST
  DrutaValue(std::deque<std::string> l, long long e = 0)
      : type(ValueType::LIST), data(std::move(l)), expiry_time(e) {
    memory_usage = 0;
    const auto &list = std::get<std::deque<std::string>>(data);
    for (const auto &s : list) {
      memory_usage += calc_string_usage(s) + sizeof(void *);
    }
  }

  // Explicit constructor for creating empty types
  explicit DrutaValue(ValueType t) : type(t), expiry_time(0), memory_usage(0) {
    if (type == ValueType::STRING)
      data = std::string("");
    else
      data = std::deque<std::string>{};
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
