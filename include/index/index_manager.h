#pragma once

#include <map>
#include <memory>
#include "index/btree.h"
#include "storage/storage.h"

namespace flexql {
namespace index {

class IndexManager {
public:
    explicit IndexManager(const std::string& data_dir);
    
    void index_manager_insert(const std::string& table_name, const flexql::ColumnValue& key, uint64_t offset);
    void index_manager_flush(const std::string& table_name);
    bool index_manager_lookup(const std::string& table_name, const flexql::ColumnValue& key, uint64_t& offset_out);
    
    void index_manager_rebuild(const std::string& table_name, flexql::storage::StorageEngine* engine);

private:
    std::string data_dir_;
    std::map<std::string, std::unique_ptr<BTree>> indexes_;
};

} // namespace index
} // namespace flexql
