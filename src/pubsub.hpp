#ifndef PUBSUB_HPP
#define PUBSUB_HPP

#include <map>
#include <string>
#include <set>
#include <vector>

static constexpr int MAX_SUBSCRIPTIONS_PER_CLIENT = 128;

extern std::map<std::string, std::set<int>> channels_subscribers;

void pubsub_subscribe(int fd, std::vector<std::string> &args);
void pubsub_unsubscribe(int fd, std::vector<std::string> &args);
int pubsub_publish(int fd, std::vector<std::string> &args); 
void pubsub_cleanup_client(int fd);

#endif
