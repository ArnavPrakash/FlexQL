#include "index/index_manager.h"
#include <filesystem>
#include <algorithm>

namespace flexql {
namespace index {

IndexManager::IndexManager(const std::string& data_dir) : data_dir_(data_dir) {
    std::filesystem::create_directories(data_dir + "/indexes");
    for (const auto& entry : std::filesystem::directory_iterator(data_dir + "/indexes")) {
        if (entry.path().extension() == ".idx") {
            std::string table_name = entry.path().stem();
            auto tree = BTree::btree_load(entry.path().string());
            if (tree) {
                indexes_[table_name] = std::move(tree);
            }
        }
    }
}

void IndexManager::merge_flat_buffer(const std::string& table_name) {
    auto it = flat_buffers_.find(table_name);
    if (it == flat_buffers_.end() || it->second.empty()) return;

    if (indexes_.find(table_name) == indexes_.end()) {
        indexes_[table_name] = std::make_unique<BTree>();
    }
    BTree* tree = indexes_[table_name].get();
    for (const auto& [key, offset] : it->second) {
        tree->btree_insert(key, offset);
    }
    it->second.clear();
}

void IndexManager::index_manager_insert(const std::string& table_name, const flexql::ColumnValue& key, uint64_t offset) {
    // Flush any pending flat buffer first so single inserts see consistent state
    merge_flat_buffer(table_name);
    if (indexes_.find(table_name) == indexes_.end()) {
        indexes_[table_name] = std::make_unique<BTree>();
    }
    indexes_[table_name]->btree_insert(key, offset);
}

void IndexManager::index_manager_bulk_insert(const std::string& table_name,
    const std::vector<std::pair<flexql::ColumnValue, uint64_t>>& sorted_pairs) {
    // Append into flat buffer — O(N) copy, no BTree pointer chasing
    auto& buf = flat_buffers_[table_name];
    buf.insert(buf.end(), sorted_pairs.begin(), sorted_pairs.end());
    // Keep buffer sorted for binary-search lookups
    std::inplace_merge(buf.begin(), buf.end() - sorted_pairs.size(), buf.end(),
        [](const auto& a, const auto& b){ return compare_val(a.first, b.first) < 0; });
}

void IndexManager::index_manager_flush(const std::string& table_name) {
    merge_flat_buffer(table_name);
    if (indexes_.find(table_name) != indexes_.end()) {
        indexes_[table_name]->btree_save(data_dir_ + "/indexes/" + table_name + ".idx");
    }
}

bool IndexManager::index_manager_lookup(const std::string& table_name,
    const flexql::ColumnValue& key, uint64_t& offset_out) {
    // Check flat buffer first with binary search — O(log N), no merge needed
    auto bit = flat_buffers_.find(table_name);
    if (bit != flat_buffers_.end() && !bit->second.empty()) {
        const auto& buf = bit->second;
        auto lo = std::lower_bound(buf.begin(), buf.end(), key,
            [](const auto& p, const flexql::ColumnValue& k){ return compare_val(p.first, k) < 0; });
        if (lo != buf.end() && compare_val(lo->first, key) == 0) {
            offset_out = lo->second;
            return true;
        }
    }
    // Fall through to BTree
    auto it = indexes_.find(table_name);
    if (it != indexes_.end()) {
        return it->second->btree_search(key, offset_out);
    }
    return false;
}

void IndexManager::index_manager_rebuild(const std::string& table_name, flexql::storage::StorageEngine* engine) {
    auto tree = std::make_unique<BTree>();
    indexes_[table_name] = std::move(tree);
    indexes_[table_name]->btree_save(data_dir_ + "/indexes/" + table_name + ".idx");
}

} // namespace index
} // namespace flexql
