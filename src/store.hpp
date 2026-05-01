#ifndef STORE_HPP
#define STORE_HPP

#include <string>
#include <deque>
#include <map>

enum class ValueType { STRING, LIST };

struct RedisValue {
  ValueType type;
  std::string data;
  std::deque<std::string> list_data;
  long long expiry_time;

  RedisValue(ValueType t = ValueType::STRING, std::string d = "",
             std::deque<std::string> l = {}, long long e = 0)
      : type(t), data(std::move(d)), list_data(std::move(l)), expiry_time(e) {}
};

extern std::map<std::string, RedisValue> kv_store;

#endif
