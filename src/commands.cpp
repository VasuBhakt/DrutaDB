#include "commands.hpp"
#include "aof.hpp"
#include "lru.hpp"
#include "parser_resp.hpp"
#include "store.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <sys/socket.h>
#include <variant>

// check that send doesn't send any fd = -1
#undef send
#define send(fd, buf, len, flags)                                              \
  do {                                                                         \
    if ((fd) != -1)                                                            \
      ::send((fd), (buf), (len), (flags));                                     \
  } while (0)

long long get_current_time_ms() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch())
      .count();
}

void send_integer(int fd, int data) {
  std::string res = ":" + std::to_string(data) + "\r\n";
  send(fd, res.c_str(), res.size(), 0);
}

void command_ping(int fd) { send(fd, "+PONG\r\n", 7, 0); }

void command_echo(int fd, std::vector<std::string> &args) {
  std::string res;
  res.reserve(args[1].length() + 64);
  RespParser parser;
  parser.resp_bulk_string(res, args[1]);
  send(fd, res.c_str(), res.length(), 0);
}

bool command_set(int fd, std::vector<std::string> &args) {
  std::string key = args[1];
  std::string val = args[2];
  long long expire_at = 0;
  try {
    for (size_t i = 3; i < args.size(); i++) {
      std::string opt = args[i];
      for (char &c : opt)
        c = toupper((unsigned char)c);
      if (opt == "EX" && i + 1 < args.size()) {
        long long seconds = std::stoll(args[i + 1]);
        expire_at = get_current_time_ms() + (seconds * 1000);
        i++;
      } else if (opt == "PX" && i + 1 < args.size()) {
        long long ms = std::stoll(args[i + 1]);
        expire_at = get_current_time_ms() + ms;
        i++;
      } else if (opt == "PXAT" && i + 1 < args.size()) {
        long long ms = std::stoll(args[i + 1]);
        expire_at = ms;
        i++;
      }
    }
    auto it = kv_store.find(key);
    DrutaNode *existing = (it != kv_store.end()) ? it->second.get() : nullptr;
    DrutaNode *node = update_lru(key, DrutaValue(val, expire_at), existing);
    if (!existing) {
      kv_store[key] = std::unique_ptr<DrutaNode>(node);
    }
    send(fd, "+OK\r\n", 5, 0);
    return true;
  } catch (...) {
    std::string err = "-ERR value is not an integer or out of range\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return false;
  }
}

bool command_get(int fd, std::vector<std::string> &args) {
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::STRING) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong "
                        "kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      long long now = get_current_time_ms();
      if (node->value.expiry_time != 0 && now > node->value.expiry_time) {
        remove_from_lru(node);
        kv_store.erase(it);
        send(fd, "$-1\r\n", 5, 0);
      } else {
        touch_lru(node);
        const std::string &val = std::get<std::string>(node->value.data);
        std::string res;
        res.reserve(val.length() + 64);
        RespParser parser;
        parser.resp_bulk_string(res, val);
        send(fd, res.c_str(), res.length(), 0);
      }
    }
  } else {
    send(fd, "$-1\r\n", 5, 0);
  }
  return false; // GET doesn't modify state for AOF
}

static bool generic_push(int fd, std::vector<std::string> &args,
                         bool at_front) {
  std::string key = args[1];
  auto it = kv_store.find(key);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong "
                        "kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    } else {
      auto &list = std::get<std::deque<std::string>>(node->value.data);
      size_t delta = 0;
      for (size_t i = 2; i < args.size(); i++) {
        if (at_front)
          list.push_front(args[i]);
        else
          list.push_back(args[i]);
        delta += DrutaValue::calc_string_usage(args[i]) + sizeof(void *);
      }
      node->value.memory_usage += delta;
      notify_memory_change(0, delta);
      touch_lru(node);
      send_integer(fd, list.size());
      return true;
    }
  } else {
    DrutaValue data_list(ValueType::LIST);
    auto &list = std::get<std::deque<std::string>>(data_list.data);
    size_t delta = 0;
    for (size_t i = 2; i < args.size(); i++) {
      if (at_front)
        list.push_front(args[i]);
      else
        list.push_back(args[i]);
      delta += DrutaValue::calc_string_usage(args[i]) + sizeof(void *);
    }
    data_list.memory_usage += delta;
    size_t list_size = list.size();

    // The key doesn't exist, so notify_memory_change for the base size and
    // initial list
    DrutaNode *node = update_lru(key, std::move(data_list));
    kv_store[key] = std::unique_ptr<DrutaNode>(node);
    send_integer(fd, list_size);
    return true;
  }
}

bool command_rpush(int fd, std::vector<std::string> &args) {
  return generic_push(fd, args, false);
}

bool command_lpush(int fd, std::vector<std::string> &args) {
  return generic_push(fd, args, true);
}

void command_lrange(int fd, std::vector<std::string> &args) {
  std::string list_key = args[1];
  auto it = kv_store.find(list_key);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong "
                        "kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      touch_lru(node);
      const auto &list = std::get<std::deque<std::string>>(node->value.data);
      try {
        int li = std::stoi(args[2]), ri = std::stoi(args[3]);
        int size = (int)(list.size());
        if (li < 0)
          li = li + size;
        if (ri < 0)
          ri = ri + size;
        li = std::max(li, 0);
        ri = std::min(ri, size - 1);
        if (li >= size || li > ri) {
          send(fd, "*0\r\n", 4, 0);
        } else {
          size_t estimated_size = 16;
          for (int i = 0; i <= ri - li; i++) {
            estimated_size += list[i + li].size() + 16;
          }
          std::string res;
          res.reserve(estimated_size + 64);
          res += "*";
          res += std::to_string(ri - li + 1);
          res += "\r\n";
          for (int i = 0; i <= ri - li; i++) {
            const std::string &val = list[i + li];
            RespParser parser;
            parser.resp_bulk_string(res, val);
          }
          send(fd, res.c_str(), res.size(), 0);
        }
      } catch (...) {
        std::string res = "-WRONGTYPE Non-numeric values given for range\r\n";
        send(fd, res.c_str(), res.size(), 0);
      }
    }
  } else {
    send(fd, "*0\r\n", 4, 0);
  }
}

static bool generic_pop(int fd, std::vector<std::string> &args,
                        bool from_front) {
  int count_pop = 1;
  if (args.size() == 3) {
    try {
      count_pop = std::stoi(args[2]);
    } catch (...) {
      count_pop = 1;
    }
  } else if (args.size() > 3) {
    std::string res = "-ERR Invalid number of arguments\r\n";
    send(fd, res.c_str(), res.size(), 0);
    return false;
  }
  std::string list_key = args[1];
  auto it = kv_store.find(list_key);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong "
                        "kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    } else {
      touch_lru(node);
      auto &list = std::get<std::deque<std::string>>(node->value.data);
      int size = (int)list.size();
      count_pop = std::min(count_pop, size);
      size_t estimated_size = 16;
      for (int i = 0; i < count_pop; i++) {
        int idx = from_front ? i : size - 1 - i;
        estimated_size += list[idx].size() + 16;
      }
      std::string res;
      res.reserve(estimated_size + 64);
      res += "*";
      res += std::to_string(count_pop);
      res += "\r\n";

      size_t delta = 0;
      int actual_pop = count_pop;
      while (actual_pop) {
        std::string &val_ref = from_front ? list.front() : list.back();
        size_t usage = DrutaValue::calc_string_usage(val_ref) + sizeof(void *);
        delta += usage;
        std::string val = std::move(val_ref);
        if (from_front)
          list.pop_front();
        else
          list.pop_back();

        RespParser parser;
        parser.resp_bulk_string(res, val);
        actual_pop--;
      }
      node->value.memory_usage -= delta;
      notify_memory_change(delta, 0);
      send(fd, res.c_str(), res.size(), 0);
      return true;
    }
  } else {
    send(fd, "*0\r\n", 4, 0);
    return false;
  }
}

bool command_lpop(int fd, std::vector<std::string> &args) {
  return generic_pop(fd, args, true);
}

bool command_rpop(int fd, std::vector<std::string> &args) {
  return generic_pop(fd, args, false);
}

void command_llen(int fd, std::vector<std::string> &args) {
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong "
                        "kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      touch_lru(node);
      const auto &list = std::get<std::deque<std::string>>(node->value.data);
      send_integer(fd, list.size());
    }
  } else {
    send(fd, ":0\r\n", 4, 0);
  }
}

bool command_del(int fd, std::vector<std::string> &args) {
  int del_count = 0;
  for (size_t i = 1; i < args.size(); i++) {
    auto it = kv_store.find(args[i]);
    if (it != kv_store.end()) {
      del_count++;
      DrutaNode *node = it->second.get();
      remove_from_lru(node);
      kv_store.erase(it);
    }
  }
  send_integer(fd, del_count);
  return del_count > 0;
}

bool command_flushdb(int fd, std::vector<std::string> &args) {
  if (args.size() < 2 || args[1] != "--sure") {
    std::string err = "-ERR add --sure flag to flush db\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return false;
  }
  try {
    clear_lru();
    kv_store.clear();
    flush_clear_aof();
    std::string res = "+SUCCESS Database flushed\r\n";
    send(fd, res.c_str(), res.size(), 0);
    return true;
  } catch (...) {
    std::string err = "-ERR error flushing database\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return false;
  }
}

void handle_command(int fd, std::vector<std::string> &args) {

  std::string cmd = args[0];
  for (char &c : cmd)
    c = toupper((unsigned char)c);

  bool success = false;
  if (cmd == "PING") {
    command_ping(fd);
  } else if (cmd == "ECHO" && args.size() > 1) {
    command_echo(fd, args);
  } else if (cmd == "SET" && args.size() >= 3) {
    success = command_set(fd, args);
  } else if (cmd == "GET" && args.size() == 2) {
    command_get(fd, args);
  } else if (cmd == "RPUSH" && args.size() >= 3) {
    success = command_rpush(fd, args);
  } else if (cmd == "LPUSH" && args.size() >= 3) {
    success = command_lpush(fd, args);
  } else if (cmd == "LRANGE" && args.size() == 4) {
    command_lrange(fd, args);
  } else if (cmd == "LPOP" && args.size() >= 2) {
    success = command_lpop(fd, args);
  } else if (cmd == "RPOP" && args.size() >= 2) {
    success = command_rpop(fd, args);
  } else if (cmd == "DEL" && args.size() >= 2) {
    success = command_del(fd, args);
  } else if (cmd == "LLEN" && args.size() == 2) {
    command_llen(fd, args);
  } else if (cmd == "FLUSHDB") {
    success = command_flushdb(fd, args);
  } else if (cmd == "COMMAND") {
    send(fd, "*0\r\n", 4, 0);
  } else {
    std::string res =
        "-ERR unknown command or incorrect number of arguments for '" + cmd +
        "'\r\n";
    send(fd, res.c_str(), res.length(), 0);
  }

  // If client fd is valid, and command succeeded, append args
  if (fd != -1 && success) {
    append_to_aof(args);
    check_and_rewrite_aof();
  }
}
