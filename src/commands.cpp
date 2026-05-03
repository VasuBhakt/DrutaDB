#include "commands.hpp"
#include "store.hpp"
#include "aof.hpp"
#include "parser_resp.hpp"
#include <algorithm>
#include <chrono>
#include <string>
#include <sys/socket.h>

// check that send doesn't send any fd = -1
#undef send
#define send(fd, buf, len, flags) do { if((fd) != -1) ::send((fd), (buf), (len), (flags)); } while(0)

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

void command_ping(int fd) {
  send(fd, "+PONG\r\n", 7, 0);
}

void command_echo(int fd, std::vector<std::string> &args) {
  std::string res;
  res.reserve(args[1].length()+64);
  RespParser parser;
  parser.resp_bulk_string(res,args[1]);
  send(fd, res.c_str(), res.length(), 0);
}

void command_set(int fd, std::vector<std::string> &args) {
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
        } else if (opt == "PXAT" && i+1<args.size()) {
          long long ms = std::stoll(args[i+1]);
          expire_at = ms;
          i++;
        }
      }
      kv_store[key] = RedisValue(ValueType::STRING, val, {}, expire_at);
      send(fd, "+OK\r\n", 5, 0);
    } catch (...) {
      std::string err = "-ERR value is not an integer or out of range\r\n";
      send(fd, err.c_str(), err.size(), 0);
    }
}

void command_get(int fd, std::vector<std::string> &args) {
    auto it = kv_store.find(args[1]);
    if (it != kv_store.end()) {
      if (it->second.type != ValueType::STRING) {
        std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        send(fd, err.c_str(), err.size(), 0);
      } else {
        long long now = get_current_time_ms();
        if (it->second.expiry_time != 0 && now > it->second.expiry_time) {
          kv_store.erase(it);
          send(fd, "$-1\r\n", 5, 0);
        } else {
          std::string val = it->second.data;
          std::string res;
          res.reserve(val.length()+64);
          RespParser parser;
          parser.resp_bulk_string(res,val);
          send(fd, res.c_str(), res.length(), 0);
        }
      }
    } else {
      send(fd, "$-1\r\n", 5, 0);
    }
}

void command_rpush(int fd, std::vector<std::string> &args) {
    std::string key = args[1];
    auto it = kv_store.find(key);
    if (it != kv_store.end()) {
      if (it->second.type != ValueType::LIST) {
        std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        send(fd, err.c_str(), err.size(), 0);
      } else {
        for (size_t i = 2; i < args.size(); i++) {
          it->second.list_data.push_back(args[i]);
        }
        send_integer(fd, it->second.list_data.size());
      }
    } else {
      RedisValue data_list;
      data_list.type = ValueType::LIST;
      for (size_t i = 2; i < args.size(); i++) {
        data_list.list_data.push_back(args[i]);
      }
      kv_store[key] = data_list;
      send_integer(fd, kv_store[key].list_data.size());
    }
}

void command_lpush(int fd, std::vector<std::string> &args) {
  std::string key = args[1];
  auto it = kv_store.find(key);
  if (it != kv_store.end()) {
    if (it->second.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      for (size_t i = 2; i < args.size(); i++) {
        it->second.list_data.push_front(args[i]);
      }
      send_integer(fd, it->second.list_data.size());
    }
  } else {
    RedisValue data_list;
    data_list.type = ValueType::LIST;
    for (size_t i = 2; i < args.size(); i++) {
      data_list.list_data.push_front(args[i]);
    }
    kv_store[key] = data_list;
    send_integer(fd, kv_store[key].list_data.size());
  }
}

void command_lrange(int fd, std::vector<std::string> &args) {
    std::string list_key = args[1];
    auto it = kv_store.find(list_key);
    if (it != kv_store.end()) {
      if (it->second.type != ValueType::LIST) {
        std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
        send(fd, err.c_str(), err.size(), 0);
      } else {
        try {
          int li = std::stoi(args[2]), ri = std::stoi(args[3]);
          int size = (int)(it->second.list_data.size());
          if (li < 0)
            li = li + size;
          if (ri < 0)
            ri = ri + size;
          li = std::max(li, 0);
          ri = std::min(ri, size - 1);
          if (li >= size) {
            send(fd, "*0\r\n", 4, 0);
          } else if (li > ri) {
            send(fd, "*0\r\n", 4, 0);
          } else if (0 <= li && ri <= size - 1 && li <= ri) {
            size_t estimated_size = 16;
            for (int i = 0; i <= ri - li; i++) {
              estimated_size += it->second.list_data[i + li].size() + 16;
            }
            std::string res;
            res.reserve(estimated_size + 64);
            res += "*";
            res += std::to_string(ri - li + 1);
            res += "\r\n";
            for (int i = 0; i <= ri - li; i++) {
              const std::string &val = it->second.list_data[i + li];
              RespParser parser;
              parser.resp_bulk_string(res,val);
            }
            send(fd, res.c_str(), res.size(), 0);
          } else {
            send(fd, "*0\r\n", 4, 0);
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

void command_lpop(int fd, std::vector<std::string> &args) {
  int count_pop = 1;
  if(args.size() == 3) {
    try {
      count_pop = std::stoi(args[2]);
    } catch (...) {
      count_pop = 1;
    }
  } else if(args.size() > 3) {
    std::string res = "-ERR Invalid number of arguments\r\n";
    send(fd, res.c_str(), res.size(), 0);
    return ;
  }
  std::string list_key = args[1];
  auto it = kv_store.find(list_key);
  if (it != kv_store.end()) {
    if (it->second.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      int size = (int)it->second.list_data.size();
      count_pop = std::min(count_pop, size);
      size_t estimated_size = 16;
      for (int i = 0; i < count_pop; i++) {
        estimated_size += it->second.list_data[i].size() + 16;
      }
      std::string res; res.reserve(estimated_size + 64);
      res += "*";
      res += std::to_string(count_pop);
      res += "\r\n";
      while(count_pop) {
        std::string val = it->second.list_data.front();
        it->second.list_data.pop_front();
        RespParser parser;
        parser.resp_bulk_string(res,val);
        count_pop--;
      }
      send(fd, res.c_str(), res.size(), 0);
    }
  } else {
    send(fd, "*0\r\n", 4, 0);
  }
}

void command_rpop(int fd, std::vector<std::string> &args) {
  int count_pop = 1;
  if(args.size() == 3) {
    try {
      count_pop = std::stoi(args[2]);
    } catch (...) {
      count_pop = 1;
    }
  } else if(args.size() > 3) {
    std::string res = "-ERR Invalid number of arguments\r\n";
    send(fd, res.c_str(), res.size(), 0);
    return ;
  }
  std::string list_key = args[1];
  auto it = kv_store.find(list_key);
  if (it != kv_store.end()) {
    if (it->second.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      int size = (int)it->second.list_data.size();
      count_pop = std::min(count_pop, size);
      size_t estimated_size = 16;
      for (int i = 0; i < count_pop; i++) {
        estimated_size += it->second.list_data[size-1-i].size() + 16;
      }
      std::string res; res.reserve(estimated_size + 64);
      res += "*";
      res += std::to_string(count_pop);
      res += "\r\n";
      while(count_pop) {
        std::string val = it->second.list_data.back();
        it->second.list_data.pop_back();
        RespParser parser;
        parser.resp_bulk_string(res,val);
        count_pop--;
      }
      send(fd, res.c_str(), res.size(), 0);
    }
  } else {
    send(fd, "*0\r\n", 4, 0);
  }
}

void command_llen(int fd, std::vector<std::string> &args) {
  auto it = kv_store.find(args[1]);
  if (it != kv_store.end()) {
    if(it->second.type != ValueType::LIST) {
      std::string err = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
      send(fd, err.c_str(), err.size(), 0);
    } else {
      send_integer(fd, it->second.list_data.size());
    }
  } else {
    send(fd, "*0\r\n", 4, 0);
  }
}

void command_del(int fd, std::vector<std::string> &args) {
  int del_count = 0;
  for(int i=1;i<args.size();i++) {
    auto it = kv_store.find(args[i]);
    if (it != kv_store.end()) {
      del_count++;
      kv_store.erase(args[i]);
    }
  }
  send_integer(fd, del_count);
}

void handle_command(int fd, std::vector<std::string> &args) {
  
  std::string cmd = args[0];
  for (char &c : cmd)
    c = toupper((unsigned char)c);
  if (cmd == "PING") {
    command_ping(fd);
  } else if (cmd == "ECHO" && args.size() > 1) {
    command_echo(fd, args);
  } else if (cmd == "SET" && args.size() >= 3) {
    command_set(fd, args);
  } else if (cmd == "GET" && args.size() == 2) {
    command_get(fd, args);
  } else if (cmd == "RPUSH" && args.size() >= 3) {
    command_rpush(fd, args);
  } else if(cmd == "LPUSH" && args.size() >= 3) {
    command_lpush(fd, args);
  } else if (cmd == "LRANGE" && args.size() == 4) {
    command_lrange(fd, args);
  } else if (cmd == "LPOP" && args.size() >= 2) {
    command_lpop(fd, args);
  } else if (cmd == "RPOP" && args.size() >= 2) {
    command_rpop(fd, args);
  } else if (cmd == "DEL" && args.size() >= 2) {
    command_del(fd, args);
  } else if (cmd == "LLEN" && args.size() == 2) {
    command_llen(fd, args);
  } else if (cmd == "COMMAND") {
    send(fd, "*0\r\n", 4, 0);
  } else {
    std::string res = "-ERR unknown command or incorrect number of arguments for '" + cmd + "'\r\n";
    send(fd, res.c_str(), res.length(), 0);
  }
  // If client fd is valid, append args
  if(fd != -1) {
    append_to_aof(args);
    check_and_rewrite_aof();
  }
}

