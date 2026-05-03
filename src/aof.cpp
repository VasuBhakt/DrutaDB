#include "aof.hpp"
#include "store.hpp"
#include "commands.hpp"
#include "parser_resp.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <algorithm>
#include <array>
#include <string_view>
#include <filesystem>

namespace fs = std::filesystem;

static fs::path get_aof_path(bool is_temp = false) {
    fs::path p = fs::current_path();
  
    // Go up if we are inside ANY folder that looks like a build directory
    std::string folder = p.filename().string();
    if (folder == "build" || folder == "cmake-build-debug" || folder == "bin") {
      p = p.parent_path();
    }
  
    fs::create_directory(p / "data");
    return p / "data" / (is_temp ? "drutadb.aof.tmp" : "drutadb.aof");
}

static std::ofstream aof_file(get_aof_path(), std::ios::app);
static uintmax_t last_rewrite_size = 0; // Tracks size after last cleanup

// check for commands which modify state of kv_store
// implemented with atd:;array + binary_search
bool is_valid(std::string_view cmd) {
    // Must be sorted alphabetically for binary_search to work!
    static constexpr std::array<std::string_view, 6> cmds = {
        "DEL", "LPOP", "LPUSH", "RPOP", "RPUSH", "SET"
    };

    return std::binary_search(cmds.begin(), cmds.end(), cmd);
}

// ARGS -> RESP
std::string format_resp_command(const std::vector<std::string> &args) {
    std::string res;
    int estimated_size = 16;
    for(const std::string& arg : args) {
        estimated_size += arg.size() + 16;
    }
    res.reserve(estimated_size + 64);
    res += "*";
    res += std::to_string(args.size());
    res += "\r\n";
    RespParser parser;
    for (const std::string &arg: args) {
        parser.resp_bulk_string(res, arg);
    }
    return res;
}

void append_to_aof(const std::vector<std::string>& args) {
    if (args.empty()) return ;

    std::string cmd = args[0];
    for (char &c : cmd) c = toupper((unsigned char)c);

    if (!is_valid(cmd)) return;

    std::string resp = format_resp_command(args);

    if (aof_file.is_open()) {
        aof_file << resp;
        aof_file.flush();
    }
}

void replay_aof() {
    fs::path p = get_aof_path();
    std::ifstream file(p);
    if (!file.is_open()) return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string data = buffer.str();

    RespParser parser;
    // Pass fd = -1 to tell handle_command it's a replay
    parser.parse_and_execute(data.c_str(), data.length(), -1);

    // Initialize base size after replay
    last_rewrite_size = fs::file_size(p);
}

void rewrite_aof() {
    std::cout << "[AOF] Starting background AOF rewrite..." << std::endl;

    fs::path main_path = get_aof_path();
    fs::path temp_path = get_aof_path(true);

    std::ofstream temp_aof(temp_path, std::ios::out | std::ios::trunc);

    if (!temp_aof.is_open()) {
        std::cerr << "[AOF Error] Could not open temporary file for rewrite." << std::endl;
        return;
    }

    long long current_time = get_current_time_ms();

    for (const auto& [key, value] : kv_store) {
        if (value.expiry_time > 0 && current_time > value.expiry_time) {
            continue; 
        }

        if (value.type == ValueType::STRING) {
            std::vector<std::string> cmd = {"SET", key, value.data};
            if (value.expiry_time > 0) {
                cmd.push_back("PXAT"); // absolute time expiry
                cmd.push_back(std::to_string(value.expiry_time));
            }
            temp_aof << format_resp_command(cmd);

        } else if (value.type == ValueType::LIST) {
            if (!value.list_data.empty()) {
                std::vector<std::string> cmd = {"RPUSH", key};
                for (const std::string& item : value.list_data) {
                    cmd.push_back(item);
                }
                temp_aof << format_resp_command(cmd);
            }
        }
    }

    temp_aof.flush();
    temp_aof.close();

    try {
        if (aof_file.is_open()) aof_file.close(); // close main aof file
        std::filesystem::rename(temp_path, main_path); // remain temp file to main aof file
        aof_file.open(main_path, std::ios::app); // reopen aof file in append mode
        last_rewrite_size = fs::file_size(main_path); // Update base size
        std::cout << "[AOF] Rewrite complete. File compressed successfully." << std::endl;
    } catch (std::filesystem::filesystem_error& e) {
        std::cerr << "[AOF Error] Failed to rename temporary AOF file: " << e.what() << std::endl;
        aof_file.open("drutadb.aof", std::ios::app); // keep aof file
    }
}

void check_and_rewrite_aof() {
    try {
      fs::path p = get_aof_path();
  
      // 1. Check if the file exists
      if (fs::exists(p)) {
        // 2. Get the actual size on disk
        uintmax_t size = fs::file_size(p);
  
        // 1. File > 1KB (for testing, should be ideally >10kb)
        // 2. File has doubled (grown by 100%) since last rewrite
        if (size > 256 && size > last_rewrite_size * 2) {
          rewrite_aof();
        }
      }
    } catch (const fs::filesystem_error &e) {
      // Silently ignore if file is busy or locked
    }
  }
  