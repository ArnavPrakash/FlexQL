#include <gtest/gtest.h>
#include "index/btree.h"
#include <filesystem>
#include <random>

using namespace flexql::index;
using namespace flexql;

TEST(BTreeTest, InsertAndSearch) {
    BTree tree;
    
    for (int i = 0; i < 1000; i++) {
        tree.btree_insert((int64_t)i, i * 100);
    }
    
    uint64_t offset;
    for (int i = 0; i < 1000; i++) {
        ASSERT_TRUE(tree.btree_search((int64_t)i, offset));
        EXPECT_EQ(offset, i * 100);
    }
    
    EXPECT_FALSE(tree.btree_search((int64_t)1000, offset));
}

TEST(BTreeTest, SaveAndLoad) {
    std::string path = "data_test_idx.idx";
    {
        BTree tree;
        for (int i = 0; i < 500; i++) {
            tree.btree_insert((int64_t)i, i * 10);
        }
        tree.btree_save(path);
    }
    
    {
        auto tree = BTree::btree_load(path);
        ASSERT_NE(tree, nullptr);
        
        uint64_t offset;
        for (int i = 0; i < 500; i++) {
            ASSERT_TRUE(tree->btree_search((int64_t)i, offset));
            EXPECT_EQ(offset, i * 10);
        }
    }
    
    std::filesystem::remove(path);
}

TEST(BTreeTest, StringKeys) {
    BTree tree;
    tree.btree_insert(std::string("Alice"), 500);
    tree.btree_insert(std::string("Bob"), 600);
    tree.btree_insert(std::string("Charlie"), 700);
    
    uint64_t offset;
    ASSERT_TRUE(tree.btree_search(std::string("Bob"), offset));
    EXPECT_EQ(offset, 600);
}
