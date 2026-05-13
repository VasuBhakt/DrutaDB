#include "drutahash.hpp"

std::string* DrutaHash::get_hash(const std::string &field) {
    if (type == HashType::VECTOR) {
        auto &vec = std::get<std::vector<std::pair<std::string, std::string>>>(data);
        for (auto &p : vec) {
            if (p.first == field) return &p.second;
        }
    } else {
        auto &m = std::get<std::map<std::string, std::string>>(data);
        auto it = m.find(field);
        if (it != m.end()) return &it->second;
    }
    return nullptr;
}

bool DrutaHash::set_hash(const std::string &field, const std::string &value) {
    if (type == HashType::VECTOR) {
        auto &vec = std::get<std::vector<std::pair<std::string, std::string>>>(data);
        for (auto &p : vec) {
            if (p.first == field) {
                memory_usage -= p.second.capacity();
                p.second = value;
                memory_usage += p.second.capacity();
                return false;
            }
        }
        vec.push_back({field, value});
        memory_usage += field.capacity() + value.capacity() + HASH_ENTRY_OVERHEAD_VECTOR;
        if (vec.size() > DRUTA_MAX_KEY) {
            convert_to_map();
        }
        return true;
    } else {
        auto &m = std::get<std::map<std::string, std::string>>(data);
        auto it = m.find(field);
        if (it != m.end()) {
            memory_usage -= it->second.capacity();
            it->second = value;
            memory_usage += it->second.capacity();
            return false;
        } else {
            m[field] = value;
            memory_usage += field.capacity() + value.capacity() + HASH_ENTRY_OVERHEAD_MAP;
            return true;
        }
    }
}

bool DrutaHash::del_hash(const std::string &field) {
    if (type == HashType::VECTOR) {
        auto &vec = std::get<std::vector<std::pair<std::string, std::string>>>(data);
        for (auto it = vec.begin(); it != vec.end(); ++it) {
            if (it->first == field) {
                memory_usage -= (it->first.capacity() + it->second.capacity() + HASH_ENTRY_OVERHEAD_VECTOR);
                vec.erase(it);
                return true;
            }
        }
    } else {
        auto &m = std::get<std::map<std::string, std::string>>(data);
        auto it = m.find(field);
        if (it != m.end()) {
            memory_usage -= (it->first.capacity() + it->second.capacity() + HASH_ENTRY_OVERHEAD_MAP);
            m.erase(it);
            return true;
        }
    }
    return false;
}

size_t DrutaHash::size() const {
    if (type == HashType::VECTOR) {
        return std::get<std::vector<std::pair<std::string, std::string>>>(data).size();
    } else {
        return std::get<std::map<std::string, std::string>>(data).size();
    }
}

void DrutaHash::convert_to_map() {
    if (type == HashType::MAP) return;
    auto vec = std::move(std::get<std::vector<std::pair<std::string, std::string>>>(data));
    std::map<std::string, std::string> m;
    for (auto &p : vec) {
        m[p.first] = std::move(p.second);
    }
    size_t count = m.size();
    data = std::move(m);
    type = HashType::MAP;
    memory_usage += (count * MAP_NODE_OVERHEAD);
}
