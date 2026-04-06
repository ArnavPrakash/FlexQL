#include "index/btree.h"
#include <queue>
#include <fstream>

namespace flexql {
namespace index {

BTree::BTree() {
    root_ = std::make_unique<BTreeNode>(true);
}

bool BTree::search_node(const BTreeNode* node, const flexql::ColumnValue& key, uint64_t& offset_out) const {
    int i = 0;
    while (i < node->keys.size() && compare_val(key, node->keys[i]) > 0) {
        i++;
    }
    
    if (node->is_leaf) {
        if (i < node->keys.size() && compare_val(key, node->keys[i]) == 0) {
            offset_out = node->offsets[i];
            return true;
        }
        return false;
    } else {
        if (i < node->keys.size() && compare_val(key, node->keys[i]) == 0) {
            i++; // in B+ tree, router key means look in right child
        }
        return search_node(node->children[i].get(), key, offset_out);
    }
}

bool BTree::btree_search(const flexql::ColumnValue& key, uint64_t& offset_out) const {
    return search_node(root_.get(), key, offset_out);
}

void BTree::split_child(BTreeNode* parent, int i, BTreeNode* child) {
    auto new_node = std::make_unique<BTreeNode>(child->is_leaf);
    
    if (child->is_leaf) {
        // Leave T-1 in left, T in right
        for (int j = 0; j < BTREE_T; j++) {
            new_node->keys.push_back(std::move(child->keys[j + BTREE_T - 1]));
            new_node->offsets.push_back(child->offsets[j + BTREE_T - 1]);
        }
        
        parent->keys.insert(parent->keys.begin() + i, new_node->keys[0]); // Router key
        parent->children.insert(parent->children.begin() + i + 1, std::move(new_node));
        
        child->keys.resize(BTREE_T - 1);
        child->offsets.resize(BTREE_T - 1);
    } else {
        // Internal node split
        // Leave T-1 in left, move middle up, T-1 in right
        for (int j = 0; j < BTREE_T - 1; j++) {
            new_node->keys.push_back(std::move(child->keys[j + BTREE_T]));
            new_node->children.push_back(std::move(child->children[j + BTREE_T]));
        }
        new_node->children.push_back(std::move(child->children[2 * BTREE_T - 1]));
        
        parent->keys.insert(parent->keys.begin() + i, std::move(child->keys[BTREE_T - 1]));
        parent->children.insert(parent->children.begin() + i + 1, std::move(new_node));
        
        child->keys.resize(BTREE_T - 1);
        child->children.resize(BTREE_T);
    }
}

void BTree::insert_non_full(BTreeNode* node, const flexql::ColumnValue& key, uint64_t offset) {
    int i = node->keys.size() - 1;
    
    if (node->is_leaf) {
        node->keys.push_back(key); // temp
        node->offsets.push_back(offset); // temp
        
        while (i >= 0 && compare_val(key, node->keys[i]) < 0) {
            node->keys[i + 1] = node->keys[i];
            node->offsets[i + 1] = node->offsets[i];
            i--;
        }
        
        // Handle updates (replace offset if key exists)
        if (i >= 0 && compare_val(key, node->keys[i]) == 0) {
            node->offsets[i] = offset;
            node->keys.pop_back();
            node->offsets.pop_back();
        } else {
            node->keys[i + 1] = key;
            node->offsets[i + 1] = offset;
        }
    } else {
        while (i >= 0 && compare_val(key, node->keys[i]) < 0) {
            i--;
        }
        i++;
        
        if (node->children[i]->keys.size() == MAX_KEYS) {
            split_child(node, i, node->children[i].get());
            if (compare_val(key, node->keys[i]) >= 0) { // >= because right child has the router key in leaves
                i++;
            }
        }
        insert_non_full(node->children[i].get(), key, offset);
    }
}

void BTree::btree_insert(const flexql::ColumnValue& key, uint64_t offset) {
    if (root_->keys.size() == MAX_KEYS) {
        auto new_root = std::make_unique<BTreeNode>(false);
        new_root->children.push_back(std::move(root_));
        split_child(new_root.get(), 0, new_root->children[0].get());
        root_ = std::move(new_root);
    }
    insert_non_full(root_.get(), key, offset);
}

bool BTree::btree_save(const std::string& filepath) const {
    std::ofstream out(filepath, std::ios::binary);
    if (!out) return false;
    
    std::queue<BTreeNode*> q;
    q.push(root_.get());
    
    while (!q.empty()) {
        BTreeNode* curr = q.front();
        q.pop();
        
        if (curr->is_leaf) {
            for (size_t i = 0; i < curr->keys.size(); ++i) {
                int type = curr->keys[i].index();
                out.write((char*)&type, sizeof(type));
                
                if (type == 0) {
                    int64_t v = std::get<flexql::IntValue>(curr->keys[i]);
                    out.write((char*)&v, sizeof(v));
                } else {
                    const std::string& s = std::get<flexql::TextValue>(curr->keys[i]);
                    uint32_t len = s.size();
                    out.write((char*)&len, sizeof(len));
                    out.write(s.data(), len);
                }
                
                uint64_t off = curr->offsets[i];
                out.write((char*)&off, sizeof(off));
            }
        } else {
            for (auto& child : curr->children) {
                q.push(child.get());
            }
        }
    }
    return true;
}

std::unique_ptr<BTree> BTree::btree_load(const std::string& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return nullptr;
    
    auto tree = std::make_unique<BTree>();
    
    while (in.peek() != EOF) {
        int type;
        if (!in.read((char*)&type, sizeof(type))) break;
        
        flexql::ColumnValue key;
        if (type == 0) {
            int64_t v;
            in.read((char*)&v, sizeof(v));
            key = v;
        } else {
            uint32_t len;
            in.read((char*)&len, sizeof(len));
            std::string s(len, '\0');
            in.read(&s[0], len);
            key = s;
        }
        
        uint64_t off;
        in.read((char*)&off, sizeof(off));
        tree->btree_insert(key, off);
    }
    return tree;
}

} // namespace index
} // namespace flexql
