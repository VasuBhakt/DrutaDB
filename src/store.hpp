#ifndef STORE_HPP
#define STORE_HPP

#include <deque>
#include <map>
#include <string>

enum class ValueType { STRING, LIST };

struct DrutaValue {
  ValueType type;
  std::string data;
  std::deque<std::string> list_data;
  long long expiry_time;

  DrutaValue(ValueType t = ValueType::STRING, std::string d = "",
             std::deque<std::string> l = {}, long long e = 0)
      : type(t), data(std::move(d)), list_data(std::move(l)), expiry_time(e) {}
};

struct R

    extern std::map<std::string, DrutaValue>
        kv_store;

#endif
