#include <gtest/gtest.h>
#include "storage/wal.h"
#include <filesystem>
#include <cstring>

using namespace flexql::storage;

class WALTest : public ::testing::Test {
protected:
    std::string test_dir = "data_wal_test";

    void SetUp() override {
        std::filesystem::remove_all(test_dir);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(WALTest, AppendAndRecover) {
    uint64_t offset_committed;
    uint64_t offset_uncommitted;
    
    {
        WAL wal(test_dir);
        ASSERT_TRUE(wal.open());
        
        WALRecord r1;
        r1.record_id = 1; r1.operation_type = WALOpType::INSERT;
        strcpy(r1.table_name, "t1");
        r1.payload = {0xAA, 0xBB}; r1.payload_len = 2; r1.committed_flag = 0;
        
        WALRecord r2;
        r2.record_id = 2; r2.operation_type = WALOpType::INSERT;
        strcpy(r2.table_name, "t2");
        r2.payload = {0xCC}; r2.payload_len = 1; r2.committed_flag = 0;
        
        offset_committed = wal.append_record(r1);
        offset_uncommitted = wal.append_record(r2);
        
        wal.commit_record(offset_committed);
    }
    
    {
        WAL wal2(test_dir);
        ASSERT_TRUE(wal2.open());
        
        auto uncommitted = wal2.recover();
        ASSERT_EQ(uncommitted.size(), 1);
        EXPECT_EQ(uncommitted[0].record_id, 2);
        EXPECT_STREQ(uncommitted[0].table_name, "t2");
        EXPECT_EQ(uncommitted[0].payload[0], 0xCC);
    }
}
