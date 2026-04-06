#include <gtest/gtest.h>
#include "cache/lru_cache.h"

using namespace flexql::cache;
using namespace flexql;

TEST(LRUCacheTest, PutAndGet) {
    LRUCache cache(2);
    ResultSet rs1; rs1.column_names = {"id"};
    ResultSet rs2; rs2.column_names = {"name"};
    
    cache.lru_put("SELECT * FROM t1", rs1);
    
    ResultSet out;
    ASSERT_TRUE(cache.lru_get("SELECT * FROM t1", out));
    EXPECT_EQ(out.column_names[0], "id");
    
    // Miss
    EXPECT_FALSE(cache.lru_get("SELECT * FROM t2", out));
}

TEST(LRUCacheTest, Eviction) {
    LRUCache cache(2);
    ResultSet rs;
    
    cache.lru_put("Q1", rs);
    cache.lru_put("Q2", rs);
    cache.lru_put("Q3", rs); // Should evict Q1
    
    ResultSet out;
    EXPECT_FALSE(cache.lru_get("Q1", out));
    EXPECT_TRUE(cache.lru_get("Q2", out));
    EXPECT_TRUE(cache.lru_get("Q3", out));
}

TEST(LRUCacheTest, Invalidation) {
    LRUCache cache(10);
    ResultSet rs;
    
    cache.lru_put("SELECT * FROM users", rs);
    cache.lru_put("SELECT name FROM accounts", rs);
    cache.lru_put("SELECT id FROM users WHERE id=1", rs);
    
    cache.lru_invalidate_table("users");
    
    ResultSet out;
    EXPECT_FALSE(cache.lru_get("SELECT * FROM users", out));
    EXPECT_FALSE(cache.lru_get("SELECT id FROM users WHERE id=1", out));
    
    // Accounts should still be there
    EXPECT_TRUE(cache.lru_get("SELECT name FROM accounts", out));
}
