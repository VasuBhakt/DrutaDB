#ifndef COMMANDS_HPP
#define COMMANDS_HPP

#include <vector>
#include <string>

long long get_current_time_ms();
void handle_command(int fd, std::vector<std::string> &args);

#endif
