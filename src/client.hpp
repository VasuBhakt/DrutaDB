#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>
#include <set>
#include <map>
#include "parser_resp.hpp"


struct Client{
    RespParser parser;
    bool is_subscriber = false;
    std::set<std::string> subscribed_channels;
};

extern std::map<int, Client> clients_map;

#endif