#ifndef AOF_HPP
#define AOF_HPP

#include <vector>
#include <string>

void flush_aof();
void append_to_aof(const std::vector<std::string> &args);
void replay_aof();
void check_and_rewrite_aof();
void rewrite_aof();
void flush_clear_aof();

#endif
