#ifndef DRUTAHASH_HPP
#define DRUTAHASH_HPP

#include <variant>
#include <map>
#include <vector>
#include <utility>
#include <string>


enum class HashType {
    MAP, VECTOR
};

// Precise overhead estimations for memory tracking
static constexpr size_t MAP_NODE_OVERHEAD = 4 * sizeof(void*); // 3 pointers + color/padding
static constexpr size_t STRING_OBJ_OVERHEAD = sizeof(std::string);
static constexpr size_t HASH_ENTRY_OVERHEAD_MAP = (STRING_OBJ_OVERHEAD * 2) + MAP_NODE_OVERHEAD;
static constexpr size_t HASH_ENTRY_OVERHEAD_VECTOR = (STRING_OBJ_OVERHEAD * 2);

struct DrutaHash {
    HashType type;
    std::variant<std::map<std::string, std::string>, std::vector<std::pair<std::string, std::string>>> data;
    long long expiry_time;
    size_t memory_usage;

    DrutaHash(std::map<std::string, std::string> h, long long e = 0)
        : type(HashType::MAP), data(std::move(h)), expiry_time(e) {
        memory_usage = 0;
        const auto &hash = std::get<std::map<std::string, std::string>>(data);
        for (const auto &pair : hash) {
            memory_usage += pair.first.capacity() + pair.second.capacity() + HASH_ENTRY_OVERHEAD_MAP;
        }
    }

    DrutaHash(std::vector<std::pair<std::string, std::string>> v, long long e = 0)
        : type(HashType::VECTOR), data(std::move(v)), expiry_time(e) {
        memory_usage = 0;
        const auto &vec = std::get<std::vector<std::pair<std::string, std::string>>>(data);
        for (const auto &pair : vec) {
            memory_usage += pair.first.capacity() + pair.second.capacity() + HASH_ENTRY_OVERHEAD_VECTOR;
        }
    }

    std::string* get(const std::string &field);
    bool set(const std::string &field, const std::string &value);
    bool del(const std::string &field);
    size_t size() const;
    void convert_to_map();
};
static constexpr size_t DRUTA_MAX_KEY = 64;

#endif