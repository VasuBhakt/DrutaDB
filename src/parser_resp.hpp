#ifndef RESP_PARSER_HPP
#define RESP_PARSER_HPP

#include <string>
#include <vector>

enum class ParserState {
  IDLE,
  READING_ARRAY_SIZE,
  READING_BULK_SIZE,
  READING_DATA
};

struct RespParser {
  ParserState state = ParserState::IDLE;
  std::vector<std::string> args;
  int array_count = 0;
  int current_bulk_len = 0;
  std::string current_buffer;
  std::string data_accumulator;

  void reset();
  void parse_and_execute(const char *buffer, int length, int fd);
};

#endif