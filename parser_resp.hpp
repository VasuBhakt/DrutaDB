#ifndef RESP_PARSER_HPP
#define RESP_PARSER_HPP

#include <vector>
#include <string>
#include <deque>
#include <map>
#include <iostream>
#include <chrono>

enum class ParserState {
    IDLE,
    READING_ARRAY_SIZE,
    READING_BULK_SIZE,
    READING_DATA
};

enum class ValueType {
    STRING, 
    LIST
};

struct RedisValue {
    ValueType type;
    std::string data;
    std::deque<std::string> list_data;
    long long expiry_time;

    RedisValue(ValueType t = ValueType::STRING, 
               std::string d = "", 
               std::deque<std::string> l = {}, 
               long long e = 0) 
        : type(t), data(std::move(d)), list_data(std::move(l)), expiry_time(e) {}
};

struct RespParser {
    ParserState state = ParserState::IDLE;
    std::vector<std::string> args;
    int array_count = 0;
    int current_bulk_len = 0;
    std::string current_buffer;
    std::string data_accumulator;
    static std::map<std::string, RedisValue> kv_store;

    void reset() {
        state = ParserState::IDLE;
        args.clear();
        array_count = 0;
        current_bulk_len = 0;
        current_buffer.clear();
        data_accumulator.clear();
    }

    void parse_and_execute(const char* buffer, int length, int fd) {
        for(int i=0;i<length;i++) {
            char c = buffer[i];

            switch(state) {

                case ParserState::IDLE:
                    if(c=='*') {
                        state = ParserState::READING_ARRAY_SIZE;
                        current_buffer.clear();
                    }
                    break;
                
                case ParserState::READING_ARRAY_SIZE:
                    if(c=='\n') {
                        array_count = std::stoi(current_buffer);
                        current_buffer.clear();
                        state = ParserState::READING_BULK_SIZE;
                    } else if(c!='\r') {
                        current_buffer+=c;
                    }
                    break;
                
                case ParserState::READING_BULK_SIZE:
                    if(c=='$') {
                        current_buffer.clear();
                    } else if(c=='\n') {
                        current_bulk_len = std::stoi(current_buffer);
                        current_buffer.clear();
                        data_accumulator.clear();
                        state = ParserState::READING_DATA;
                    } else if(c!='\r') {
                        current_buffer += c;
                    }
                    break;

                case ParserState::READING_DATA:
                    // Read raw bytes based on length, ignore \r\n logic here
                    if((int)data_accumulator.length()<current_bulk_len) {
                        data_accumulator += c;
                    } else {
                        // After data, we expect \r\n
                        if(c=='\n') {
                            args.push_back(data_accumulator);
                            if((int)args.size()==array_count) {
                                handle_command(fd,args);
                                this->reset(); // let the loop finish the buffer
                            } else {
                                state = ParserState::READING_BULK_SIZE;
                            }
                        }
                    }
                    break;
                
            }
        }
    }

    void handle_command(int fd, std::vector<std::string> &args) {
        std::string cmd = args[0];
        for(char &c: cmd) c = toupper((unsigned char)c);
        if(cmd=="PING") {
            send(fd, "+PONG\r\n", 7, 0);
        } else if(cmd=="ECHO" && args.size()>1) {
            std::string res = "$"+std::to_string(args[1].length())+"\r\n"+args[1]+"\r\n";
            send(fd, res.c_str(), res.length(),0); 
        } else if(cmd=="SET") {
            std::string key = args[1];
            std::string val = args[2];
            long long expire_at = 0;
            for(size_t i=3;i<args.size();i++) {
                std::string opt = args[i];
                for(char &c: opt) c = toupper((unsigned char)c);
                if(opt == "EX" && i+1<args.size()) {
                    long long seconds = std::stoll(args[i+1]);
                    expire_at = get_current_time_ms() + (seconds*1000);
                    i++;
                } else if(opt=="PX" && i+1<args.size()) {
                    long long ms = std::stoll(args[i+1]);
                    expire_at = get_current_time_ms() + ms;
                    i++;
                }
            }
            kv_store[key] = RedisValue(ValueType::STRING, val, {}, expire_at);
            send(fd, "+OK\r\n",5,0);
        } else if(cmd=="GET") {
            auto it = kv_store.find(args[1]);
            if(it!=kv_store.end()) {
                long long now = get_current_time_ms();
                if(it->second.expiry_time != 0 && now > it->second.expiry_time) {
                    kv_store.erase(it);
                    send(fd,"$-1\r\n",5,0);
                } else {
                    std::string val=it->second.data;
                    std::string res = "$"+std::to_string(val.length())+"\r\n"+val+"\r\n";
                    send(fd,res.c_str(),res.length(),0); 
                }
            } else {
                send(fd,"$-1\r\n",5,0);
            }
        } else if(cmd=="RPUSH" && args.size()>=3) {
            std::string key = args[1];
            auto it = kv_store.find(key);
            if(it!=kv_store.end()) {
                if(it->second.type != ValueType::LIST) {
                    send(fd,"-WRONGTYPE Operation against a key holding the wrong kind of value\r\n", 70, 0);
                } else {
                    for(size_t i=2;i<args.size();i++) {
                        it->second.list_data.push_back(args[i]);
                    }
                    send_integer(fd, it->second.list_data.size());
                }
            } else {
                RedisValue data_list;
                data_list.type = ValueType::LIST;
                for(size_t i=2;i<args.size();i++) {
                    data_list.list_data.push_back(args[i]);
                }
                kv_store[key] = data_list;
                send_integer(fd,kv_store[key].list_data.size());
            }
        }
    }

    void send_integer(int fd, size_t data) {
        std::string res = ":" + std::to_string(data) + "\r\n";
        send(fd, res.c_str(), res.size(), 0);
    }

    long long get_current_time_ms() {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    }
};

inline std::map<std::string, RedisValue> RespParser::kv_store;

#endif