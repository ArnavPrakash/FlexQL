#pragma once

#include <map>
#include <vector>
#include <memory>
#include "index/btree.h"
#include "storage/storage.h"

namespace flexql {
namespace index {

class IndexManager {
public:
    explicit IndexManager(const std::string& data_dir);
    
    void index_manager_insert(const std::string& table_name, const flexql::ColumnValue& key, uint64_t offset);

    // Bulk-load sorted pairs directly into a flat vector — O(N), no BTree overhead.
    // The flat buffer is merged into the BTree lazily on first lookup or explicit flush.
    void index_manager_bulk_insert(const std::string& table_name, const std::vector<std::pair<flexql::ColumnValue, uint64_t>>& sorted_pairs);

    void index_manager_flush(const std::string& table_name);
    bool index_manager_lookup(const std::string& table_name, const flexql::ColumnValue& key, uint64_t& offset_out);
    
    void index_manager_rebuild(const std::string& table_name, flexql::storage::StorageEngine* engine);

private:
    std::string data_dir_;
    std::map<std::string, std::unique_ptr<BTree>> indexes_;

    // Flat sorted buffer accumulated during bulk inserts.
    // Merged into the BTree on first lookup or explicit flush.
    std::map<std::string, std::vector<std::pair<flexql::ColumnValue, uint64_t>>> flat_buffers_;

    void merge_flat_buffer(const std::string& table_name);
};

} // namespace index
} // namespace flexql
