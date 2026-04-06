#pragma once

#include <vector>
#include <memory>
#include <string>
#include "common/flexql_types.h"

namespace flexql {
namespace index {

constexpr int BTREE_T = 50; 
constexpr int MAX_KEYS = 2 * BTREE_T - 1; 

// Helper to compare ColumnValue
inline int compare_val(const flexql::ColumnValue& a, const flexql::ColumnValue& b) {
    if (a.index() != b.index()) {
        return a.index() < b.index() ? -1 : 1;
    }
    if (std::holds_alternative<flexql::IntValue>(a)) {
        auto v1 = std::get<flexql::IntValue>(a);
        auto v2 = std::get<flexql::IntValue>(b);
        return (v1 < v2) ? -1 : ((v1 > v2) ? 1 : 0);
    } else {
        auto& s1 = std::get<flexql::TextValue>(a);
        auto& s2 = std::get<flexql::TextValue>(b);
        return s1.compare(s2);
    }
}

class BTreeNode {
public:
    bool is_leaf;
    std::vector<flexql::ColumnValue> keys;
    std::vector<uint64_t> offsets; 
    std::vector<std::unique_ptr<BTreeNode>> children; 

    explicit BTreeNode(bool leaf) : is_leaf(leaf) {}
};

class BTree {
public:
    BTree();
    
    void btree_insert(const flexql::ColumnValue& key, uint64_t offset);
    bool btree_search(const flexql::ColumnValue& key, uint64_t& offset_out) const;
    
    bool btree_save(const std::string& filepath) const;
    static std::unique_ptr<BTree> btree_load(const std::string& filepath);

private:
    std::unique_ptr<BTreeNode> root_;
    
    void split_child(BTreeNode* parent, int i, BTreeNode* child);
    void insert_non_full(BTreeNode* node, const flexql::ColumnValue& key, uint64_t offset);
    bool search_node(const BTreeNode* node, const flexql::ColumnValue& key, uint64_t& offset_out) const;
};

} // namespace index
} // namespace flexql
