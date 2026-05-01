#include "commands.hpp"
#include "store.hpp"
#include <sys/socket.h>
#include <chrono>
#include <string>
#include <algorithm>

long long get_current_time_ms() {
  auto now = std::chrono::system_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             now.time_since_epoch())
      .count();
}

void send_integer(int fd, size_t data) {
  std::string res = ":" + std::to_string(data) + "\r\n";
  send(fd, res.c_str(), res.size(), 0);
}

void handle_command(int fd, std::vector<std::string> &args) {
  std::string cmd = args[0];
  for (char &c : cmd)
    c = toupper((unsigned char)c);
  if (cmd == "PING") {
    send(fd, "+PONG\r\n", 7, 0);
  } else if (cmd == "ECHO" && args.size() > 1) {
    std::string res =
        "$" + std::to_string(args[1].length()) + "\r\n" + args[1] + "\r\n";
    send(fd, res.c_str(), res.length(), 0);
  } else if (cmd == "SET") {
    std::string key = args[1];
    std::string val = args[2];
    long long expire_at = 0;
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
      }
    }
    kv_store[key] = RedisValue(ValueType::STRING, val, {}, expire_at);
    send(fd, "+OK\r\n", 5, 0);
  } else if (cmd == "GET") {
    auto it = kv_store.find(args[1]);
    if (it != kv_store.end()) {
      long long now = get_current_time_ms();
      if (it->second.expiry_time != 0 && now > it->second.expiry_time) {
        kv_store.erase(it);
        send(fd, "$-1\r\n", 5, 0);
      } else {
        std::string val = it->second.data;
        std::string res =
            "$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
        send(fd, res.c_str(), res.length(), 0);
      }
    } else {
      send(fd, "$-1\r\n", 5, 0);
    }
  } else if (cmd == "RPUSH" && args.size() >= 3) {
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
  } else if (cmd == "LRANGE" && args.size() == 4) {
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
            size_t estimated_size = 10;
            for (int i = 0; i <= ri - li; i++) {
              estimated_size += it->second.list_data[i + li].size() + 10;
            }
            std::string res;
            res.reserve(estimated_size + 50);
            res += "*";
            res += std::to_string(ri - li + 1);
            res += "\r\n";
            for (int i = 0; i <= ri - li; i++) {
              const std::string &val = it->second.list_data[i + li];
              res += "$";
              res += std::to_string(val.size());
              res += "\r\n";
              res += val;
              res += "\r\n";
            }
            send(fd, res.c_str(), res.size(), 0);
          } else {
            send(fd, "*0\r\n", 4, 0);
          }
        } catch (...) {
          std::string res =
              "-WRONGTYPE Non-numeric values given for range\r\n";
          send(fd, res.c_str(), res.size(), 0);
        }
      }
    } else {
      send(fd, "*0\r\n", 4, 0);
    }
  } else if (cmd == "COMMAND") {
    send(fd, "*0\r\n", 4, 0);
  } else {
    std::string res = "-ERR unknown command or incorrect number of arguments for '" + cmd + "'\r\n";
    send(fd, res.c_str(), res.length(), 0);
  }
}
