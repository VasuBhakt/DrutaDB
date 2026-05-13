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
    if (it != kv_store.end()) {
      update_lru(it->second.get(), DrutaValue(val, expire_at));
    } else {
      auto new_node = std::make_unique<DrutaNode>(key, DrutaValue(val, expire_at));
      DrutaNode *node = new_node.get();
      kv_store[key] = std::move(new_node);
      add_lru(node);
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
        delta += DrutaValue::calc_string_usage(args[i]) + sizeof(std::string) + 8;
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
      delta += DrutaValue::calc_string_usage(args[i]) + sizeof(std::string) + 8;
    }
    data_list.memory_usage += delta;
    size_t list_size = list.size();

    auto new_node = std::make_unique<DrutaNode>(key, std::move(data_list));
    DrutaNode *node = new_node.get();
    kv_store[key] = std::move(new_node);
    add_lru(node);
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
        size_t usage = DrutaValue::calc_string_usage(val_ref) + sizeof(std::string) + 8;
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

bool command_hset(int fd, std::vector<std::string> &args) {
  if (args.size() < 4 || args.size() % 2 != 0) {
    std::string err = "-ERR wrong number of arguments for 'hset' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return false;
  }
  std::string key = args[1];
  auto it = kv_store.find(key);
  DrutaHash *hash_ptr = nullptr;
  DrutaNode *node = nullptr;

  if (it != kv_store.end()) {
    node = it->second.get();
    if (node->value.type != ValueType::HASH) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    hash_ptr = &std::get<DrutaHash>(node->value.data);
  } else {
    DrutaValue val(ValueType::HASH);
    auto new_node = std::make_unique<DrutaNode>(key, std::move(val));
    node = new_node.get();
    kv_store[key] = std::move(new_node);
    add_lru(node);
    hash_ptr = &std::get<DrutaHash>(node->value.data);
  }

  int created = 0;
  size_t old_mem = hash_ptr->memory_usage;
  for (size_t i = 2; i < args.size(); i += 2) {
    if (hash_ptr->set_hash(args[i], args[i + 1])) {
      created++;
    }
  }
  size_t new_mem = hash_ptr->memory_usage;
  if (new_mem > old_mem) {
    node->value.memory_usage += (new_mem - old_mem);
  } else if (old_mem > new_mem) {
    node->value.memory_usage -= (old_mem - new_mem);
  }
  notify_memory_change(old_mem, new_mem);

  touch_lru(node);
  send_integer(fd, created);
  return created > 0;
}

void command_hget(int fd, std::vector<std::string> &args) {
  if (args.size() != 3) {
    std::string err = "-ERR wrong number of arguments for 'hget' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::HASH) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return;
    }
    DrutaHash &hash = std::get<DrutaHash>(node->value.data);
    std::string *val = hash.get_hash(args[2]);
    if (val) {
      RespParser parser;
      std::string res;
      res.reserve(val->length() + 64);
      parser.resp_bulk_string(res, *val);
      send(fd, res.c_str(), res.size(), 0);
      touch_lru(node);
    } else {
      send(fd, "$-1\r\n", 5, 0);
    }
  } else {
    send(fd, "$-1\r\n", 5, 0);
  }
}

void command_hgetall(int fd, std::vector<std::string> &args) {
  if (args.size() != 2) {
    std::string err = "-ERR wrong number of arguments for 'hgetall' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::HASH) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return;
    }
    DrutaHash &hash = std::get<DrutaHash>(node->value.data);
    touch_lru(node);

    std::vector<std::pair<std::string, std::string>> all_data;
    if (hash.type == HashType::VECTOR) {
      all_data = std::get<std::vector<std::pair<std::string, std::string>>>(hash.data);
    } else {
      auto &m = std::get<std::map<std::string, std::string>>(hash.data);
      for (auto &p : m) all_data.push_back(p);
    }
    std::string res;
    res.reserve(1024 + hash.memory_usage);
    res+="*";
    res+=std::to_string(all_data.size() * 2);
    res+="\r\n";
    RespParser parser;
    for (auto &p : all_data) {
      parser.resp_bulk_string(res, p.first);
      parser.resp_bulk_string(res, p.second);
    }
    send(fd, res.c_str(), res.size(), 0);
  } else {
    send(fd, "*0\r\n", 4, 0);
  }
}

bool command_hdel(int fd, std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::string err = "-ERR wrong number of arguments for 'hdel' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return false;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::HASH) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    DrutaHash &hash = std::get<DrutaHash>(node->value.data);
    int deleted = 0;
    size_t old_mem = hash.memory_usage;
    for (size_t i = 2; i < args.size(); i++) {
      if (hash.del_hash(args[i])) {
        deleted++;
      }
    }
    size_t new_mem = hash.memory_usage;
    if (old_mem > new_mem) {
        node->value.memory_usage -= (old_mem - new_mem);
        notify_memory_change(old_mem, new_mem);
    }
    touch_lru(node);
    send_integer(fd, deleted);
    return deleted > 0;
  } else {
    send_integer(fd, 0);
    return false;
  }
}

void command_hlen(int fd, std::vector<std::string> &args) {
  if (args.size() != 2) {
    std::string err = "-ERR wrong number of arguments for 'hlen' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::HASH) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return;
    }
    DrutaHash &hash = std::get<DrutaHash>(node->value.data);
    send_integer(fd, hash.size());
  } else {
    send_integer(fd, 0);
  }
}

bool command_sadd(int fd, std::vector<std::string> &args) {
  std::string key = args[1];
  auto it = kv_store.find(key);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    auto &set = std::get<std::set<std::string>>(node->value.data);
    int created = 0;
    int delta = 0;
    for (size_t i = 2; i < args.size(); i++) {
      if (set.insert(args[i]).second) {
        delta += args[i].capacity() + sizeof(std::string) + 8 * sizeof(void*);
        created++;
      }
    }
    node->value.memory_usage += delta;
    notify_memory_change(0, delta);
    touch_lru(node);
    send_integer(fd, created);
    return created > 0;
  } else {
    DrutaValue val(ValueType::SET);
    auto &set = std::get<std::set<std::string>>(val.data);
    int created = 0;
    for (size_t i = 2; i < args.size(); i++) {
      if (set.insert(args[i]).second) {
        val.memory_usage += args[i].capacity() + sizeof(std::string) + 8 * sizeof(void*);
        created++;
      }
    }
    auto new_node = std::make_unique<DrutaNode>(key, std::move(val));
    DrutaNode *ptr = new_node.get();
    kv_store[key] = std::move(new_node);
    add_lru(ptr);
    send_integer(fd, created);
    return created > 0;
  }
}

void command_sismember(int fd, std::vector<std::string> &args) {
  if (args.size() != 3) {
    std::string err = "-ERR wrong number of arguments for 'sismember' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    auto &set = std::get<std::set<std::string>>(node->value.data);
    send_integer(fd, set.count(args[2]));
  } else {
    send_integer(fd, 0);
  }
}

void command_smismember(int fd, std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::string err = "-ERR wrong number of arguments for 'sismember' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    auto &set = std::get<std::set<std::string>>(node->value.data);
    int present = 0;
    for (size_t i = 2; i < args.size(); i++) {
      if (set.count(args[i])) {
        present++;
      }
    }
    send_integer(fd, present); 
  } else {
    send_integer(fd, 0);
  }
}

void command_scard(int fd, std::vector<std::string> &args) {
  if (args.size() != 2) {
    std::string err = "-ERR wrong number of arguments for 'scard' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    auto &set = std::get<std::set<std::string>>(node->value.data);
    send_integer(fd, set.size());
  } else {
    send_integer(fd, 0);
  }
}

void command_smembers(int fd, std::vector<std::string> &args) {
  if (args.size() != 2) {
    std::string err = "-ERR wrong number of arguments for 'smembers' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    auto &set = std::get<std::set<std::string>>(node->value.data);
    std::string res; 
    res.reserve(1024 + set.memory_usage);
    res += "*";
    res += std::to_string(set.size());
    res += "\r\n";
    RespParser parser;
    for (std::string &s: set) {
      parser.resp_bulk_string(res, s);
    } 
    send(fd, res.c_str(), res.size(), 0);
  } else {
    send(fd, "*0\r\n", 4, 0);
  }
}

void command_sdiff(int fd, std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::string err = "-ERR wrong number of arguments for 'sismember' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  std::set<std::string> diff_set;
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    diff_set = std::get<std::set<std::string>>(node->value.data); 
  } 
  for(size_t i=2;i<args.size();i++) {
    auto it = kv_store.find(args[i]);
    if (it != kv_store.end()) {
      DrutaNode *node = it->second.get();
      if (node->value.type != ValueType::SET) {
        continue;
      }
      auto &set = std::get<std::set<std::string>>(node->value.data);
      for (std::string &s: set) {
        if (diff_set.count(s)) {
          diff_set.erase(s);
        }
      }
    } 
  }
  std::string res;
  size_t estimated_size = 0;
  for (std::string &s : diff_set) {
    estimated_size += s.size();
  }
  res.reserve(16 + estimated_size);
  res += "*";
  res += std::to_string(diff_set.size());
  res += "\r\n";
  RespParser parser;
  for(std::string &s: diff_set) {
    parser.resp_bulk_string(res,s);
  }
  send(fd, res.c_str(), res.size(), 0);
}

void command_sinter(int fd, std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::string err = "-ERR wrong number of arguments for 'sismember' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  auto it = kv_store.find(args[1]);
  std::set<std::string> inter_set;
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    inter_set = std::get<std::set<std::string>>(node->value.data); 
  } 
  for (size_t i = 2; i < args.size(); i++) {
    auto it_other = kv_store.find(args[i]);
    if (it_other == kv_store.end()) {
      inter_set.clear();
      break;
    }
    DrutaNode *node_other = it_other->second.get();
    if (node_other->value.type != ValueType::SET) {
      continue;
    }
    auto &set_other = std::get<std::set<std::string>>(node_other->value.data);
    
    std::set<std::string> next_inter;
    for (const std::string &s : inter_set) {
      if (set_other.count(s)) {
        next_inter.insert(s);
      }
    }
    inter_set = std::move(next_inter);
    if (inter_set.empty()) break;
  }
  std::string res;
  size_t estimated_size = 0;
  for (std::string &s : inter_set) {
    estimated_size += s.size();
  }
  res.reserve(16 + estimated_size);
  res += "*";
  res += std::to_string(inter_set.size());
  res += "\r\n";
  RespParser parser;
  for(std::string &s: inter_set) {
    parser.resp_bulk_string(res,s);
  }
  send(fd, res.c_str(), res.size(), 0);
}

void command_sunion(int fd, std::vector<std::string> &args) {
  if (args.size() < 3) {
    std::string err = "-ERR wrong number of arguments for 'sismember' command\r\n";
    send(fd, err.c_str(), err.size(), 0);
    return;
  }
  std::set<std::string> union_set;
  for(size_t i=1;i<args.size();i++) {
    auto it = kv_store.find(args[i]);
    if (it != kv_store.end()) {
      DrutaNode *node = it->second.get();
      if (node->value.type != ValueType::SET) {
        continue;
      }
      auto &set = std::get<std::set<std::string>>(node->value.data);
      for (std::string &s: set) {
        union_set.insert(s);
      }
    } 
  }
  std::string res;
  size_t estimated_size = 0;
  for (std::string &s : union_set) {
    estimated_size += s.size();
  }
  res.reserve(16 + estimated_size);
  res += "*";
  res += std::to_string(union_set.size());
  res += "\r\n";
  RespParser parser;
  for(std::string &s: union_set) {
    parser.resp_bulk_string(res,s);
  }
  send(fd, res.c_str(), res.size(), 0);
}

bool command_sdel(int fd, std::vector<std::string> &args) {
  std::string key = args[1];
  auto it = kv_store.find(key);
  if (it != kv_store.end()) {
    DrutaNode *node = it->second.get();
    if (node->value.type != ValueType::SET) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
      return false;
    }
    auto &set = std::get<std::set<std::string>>(node->value.data);
    int deleted = 0, delta = 0;
    for (size_t i = 2; i < args.size(); i++) {
      auto it_erase = set.find(args[i]);
      if (it_erase != set.end()) {
        delta += (it_erase->capacity() + sizeof(std::string) + 8 * sizeof(void*));
        set.erase(it_erase);
        deleted++;
      }
    }
    node->value.memory_usage -= delta;
    notify_memory_change(delta, 0);
    touch_lru(node);
    send_integer(fd, deleted);
    return deleted > 0;
  } else {
    std::string err = "-ERR no such key";
    send(fd, err.c_str(), err.size(), 0);
    return false;
  }
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
  } else if (cmd == "HSET" && args.size() >= 4) {
    success = command_hset(fd, args);
  } else if (cmd == "HGET" && args.size() == 3) {
    command_hget(fd, args);
  } else if (cmd == "HGETALL" && args.size() == 2) {
    command_hgetall(fd, args);
  } else if (cmd == "HDEL" && args.size() >= 3) {
    success = command_hdel(fd, args);
  } else if (cmd == "HLEN" && args.size() == 2) {
    command_hlen(fd, args);
  } else if (cmd == "SADD" && args.size() >= 3) {
    success = command_sadd(fd, args);
  } else if (cmd == "SCARD" && args.size() == 2) {
    command_scard(fd, args);
  } else if (cmd == "SISMEMBER" && args.size() == 3) {
    command_sismember(fd, args);
  } else if (cmd == "SMEMBERS" && args.size() == 2) {
    command_smembers(fd, args);
  } else if (cmd == "SMISMEMBER" && args.size() >= 3) {
    command_smismember(fd, args);
  } else if (cmd == "SINTER" && args.size() >= 3) {
    command_sinter(fd, args);
  } else if (cmd == "SUNION" && args.size() >= 3) {
    command_sunion(fd, args);
  } else if (cmd == "SDIFF" && args.size() >= 3) {
    command_sdiff(fd, args);
  } else if (cmd == "SDEL" && args.size() >= 3) {
    success = command_sdel(fd, args);
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
