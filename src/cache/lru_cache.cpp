#include "cache/lru_cache.h"
#include <algorithm>
#include <regex>

namespace flexql {
namespace cache {

LRUCache::LRUCache(size_t max_entries) : capacity_(max_entries) {
}

bool LRUCache::lru_get(const std::string& sql_key, flexql::ResultSet& result_out) {
    auto it = map_.find(sql_key);
    if (it == map_.end()) return false;
    
    // Move to front
    list_.splice(list_.begin(), list_, it->second);
    
    result_out = it->second->result;
    return true;
}

void LRUCache::lru_put(const std::string& sql_key, const flexql::ResultSet& result_set) {
    auto it = map_.find(sql_key);
    if (it != map_.end()) {
        it->second->result = result_set;
        list_.splice(list_.begin(), list_, it->second);
        return;
    }
    
    if (list_.size() >= capacity_ && capacity_ > 0) {
        // Evict tail
        auto tail = list_.back();
        map_.erase(tail.key);
        list_.pop_back();
    }
    
    list_.push_front({sql_key, result_set});
    map_[sql_key] = list_.begin();
}

void LRUCache::lru_invalidate_table(const std::string& table_name) {
    // Rough invalidation matching table name as a whole word in SQL
    // We can use a simple substring check or regex to be safe.
    // std::regex might be heavy for each invalidation, but tasks text said "contains the table name as a word"
    std::string boundary_pattern = "\\b" + table_name + "\\b";
    std::regex rx(boundary_pattern, std::regex_constants::icase);
    
    auto it = list_.begin();
    while (it != list_.end()) {
        if (std::regex_search(it->key, rx)) {
            map_.erase(it->key);
            it = list_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace cache
} // namespace flexql
