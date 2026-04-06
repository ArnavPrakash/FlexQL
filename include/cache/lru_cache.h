#pragma once

#include <string>
#include <list>
#include <unordered_map>
#include <memory>
#include "common/flexql_types.h"

namespace flexql {
namespace cache {

class LRUCache {
public:
    explicit LRUCache(size_t max_entries);
    
    // Get a cached result set, returns true if hit (and promotes it), false if miss
    bool lru_get(const std::string& sql_key, flexql::ResultSet& result_out);
    
    // Insert or update an entry; evicts if at capacity
    void lru_put(const std::string& sql_key, const flexql::ResultSet& result_set);
    
    // Invalidate any cache entries where the sql_key contains the table_name
    void lru_invalidate_table(const std::string& table_name);

private:
    struct CacheNode {
        std::string key;
        flexql::ResultSet result;
    };
    
    size_t capacity_;
    std::list<CacheNode> list_;
    std::unordered_map<std::string, std::list<CacheNode>::iterator> map_;
};

} // namespace cache
} // namespace flexql
