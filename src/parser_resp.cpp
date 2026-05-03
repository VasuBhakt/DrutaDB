#include "parser_resp.hpp"
#include "commands.hpp"
#include <string>

void RespParser::reset() {
  state = ParserState::IDLE;
  args.clear();
  array_count = 0;
  current_bulk_len = 0;
  current_buffer.clear();
  data_accumulator.clear();
}

void RespParser::parse_and_execute(const char *buffer, int length, int fd) {
  for (int i = 0; i < length; i++) {
    char c = buffer[i];
    switch (state) {
    case ParserState::IDLE:
      if (c == '*') {
        state = ParserState::READING_ARRAY_SIZE;
        current_buffer.clear();
      }
      break;

    case ParserState::READING_ARRAY_SIZE:
      if (c == '\n') {
        array_count = std::stoi(current_buffer);
        current_buffer.clear();
        state = ParserState::READING_BULK_SIZE;
      } else if (c != '\r') {
        current_buffer += c;
      }
      break;

    case ParserState::READING_BULK_SIZE:
      if (c == '$') {
        current_buffer.clear();
      } else if (c == '\n') {
        if (!current_buffer.empty()) {
          try {
            current_bulk_len = std::stoi(current_buffer);
            current_buffer.clear();
            data_accumulator.clear();
            state = ParserState::READING_DATA;
          } catch (...) {
            this->reset();
          }
        }
      } else if (c != '\r') {
        current_buffer += c;
      }
      break;

    case ParserState::READING_DATA:
      if ((int)data_accumulator.length() < current_bulk_len) {
        data_accumulator += c;
      } else {
        if (c == '\n') {
          args.push_back(data_accumulator);
          if ((int)args.size() == array_count) {
            handle_command(fd, args);
            this->reset(); // let the loop finish the buffer
          } else {
            current_buffer.clear();
            state = ParserState::READING_BULK_SIZE;
          }
        }
      }
      break;
    }
  }
}

void RespParser::resp_bulk_string(std::string& res, const std::string& val) {
  res += "$";
  res += std::to_string(val.length());
  res += "\r\n";
  res += val;
  res += "\r\n";
  return ;
}
