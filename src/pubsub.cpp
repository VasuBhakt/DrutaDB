#include "pubsub.hpp"
#include "client.hpp"
#include "parser_resp.hpp"
#include <map>
#include <set>
#include <vector>
#include <string>
#include <sys/socket.h>

std::map<std::string, std::set<int>> channels_subscribers;

void pubsub_subscribe(int fd, std::vector<std::string> &args) {
    RespParser parser;
    for(size_t i = 1; i < args.size(); i++) {
        const std::string& channel = args[i];
        bool already_subbed = clients_map[fd].subscribed_channels.contains(channel); 
        if (!already_subbed && clients_map[fd].subscribed_channels.size() >= MAX_SUBSCRIPTIONS_PER_CLIENT) {
            std::string err = "-ERR maximum number of subscriptions reached\r\n";
            send(fd, err.c_str(), err.size(), 0);
            break;
        }
        channels_subscribers[channel].insert(fd);
        clients_map[fd].is_subscriber = true;
        clients_map[fd].subscribed_channels.insert(channel);

        // Redis format: ["subscribe", channel, count]
        std::string res = "*3\r\n";
        parser.resp_bulk_string(res, "subscribe");
        parser.resp_bulk_string(res, channel);
        res += ":" + std::to_string(clients_map[fd].subscribed_channels.size()) + "\r\n";
        send(fd, res.c_str(), res.length(), 0);
    }
}

void pubsub_unsubscribe(int fd, std::vector<std::string> &args) {
    RespParser parser;

    // If no channels provided, unsubscribe from all
    std::vector<std::string> channels_to_remove;
    if (args.size() == 1) {
        for (const auto& chan : clients_map[fd].subscribed_channels) {
            channels_to_remove.push_back(chan);
        }
    } else {
        for (size_t i = 1; i < args.size(); i++) {
            channels_to_remove.push_back(args[i]);
        }
    }

    if (channels_to_remove.empty()) {
        std::string res = "*3\r\n";
        parser.resp_bulk_string(res, "unsubscribe");
        res += "$-1\r\n";
        res += ":0\r\n";
        send(fd, res.c_str(), res.length(), 0);
        return;
    }

    for (const std::string& channel : channels_to_remove) {
        channels_subscribers[channel].erase(fd);
        clients_map[fd].subscribed_channels.erase(channel);

        if (clients_map[fd].subscribed_channels.empty()) {
            clients_map[fd].is_subscriber = false;
        }

        std::string res = "*3\r\n";
        parser.resp_bulk_string(res, "unsubscribe");
        parser.resp_bulk_string(res, channel);
        res += ":" + std::to_string(clients_map[fd].subscribed_channels.size()) + "\r\n";
        send(fd, res.c_str(), res.length(), 0);
    }
}

int pubsub_publish(int fd, std::vector<std::string> &args) {
    if (args.size() < 3) return 0;

    std::string channel = args[1];
    std::string message = args[2];
    int clients_affected = 0;

    // Build the message ONCE: ["message", channel, data]
    std::string res = "*3\r\n";
    RespParser parser;
    parser.resp_bulk_string(res, "message");
    parser.resp_bulk_string(res, channel);
    parser.resp_bulk_string(res, message);

    auto it = channels_subscribers.find(channel);
    if (it == channels_subscribers.end()) return 0;

    std::set<int>& subscribers = it->second;
    std::vector<int> dead_fds;

    for (int clientfd : subscribers) {
        if (clients_map.find(clientfd) == clients_map.end()) {
            dead_fds.push_back(clientfd);
            continue;
        }
        send(clientfd, res.c_str(), res.length(), 0);
        clients_affected++;
    }

    // Lazy cleanup of ghost subscribers
    for (int dead : dead_fds) {
        subscribers.erase(dead);
    }

    return clients_affected;
}

void pubsub_cleanup_client(int fd) {
    if (clients_map.find(fd) == clients_map.end()) return;

    for (const std::string& channel : clients_map[fd].subscribed_channels) {
        channels_subscribers[channel].erase(fd);
    }
}