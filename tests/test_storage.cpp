#include <gtest/gtest.h>
#include "storage/storage.h"
#include <filesystem>

using namespace flexql::storage;
using namespace flexql::parser;
using namespace flexql;

class StorageTest : public ::testing::Test {
protected:
    std::string test_dir = "data_test";
    std::shared_ptr<Schema> schema;

    void SetUp() override {
        std::filesystem::create_directories(test_dir + "/tables");
        schema = schema_create("test_tbl", {
            {"id", ColumnType::INT},
            {"name", ColumnType::TEXT}
        });
        
        // Remove existing files if any
        std::filesystem::remove(test_dir + "/tables/test_tbl.dat");
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir);
    }
};

TEST_F(StorageTest, InsertAndScan) {
    auto engine = storage_open("test_tbl", test_dir, schema);
    ASSERT_NE(engine, nullptr);
    
    Row r1{{100LL, std::string("Alice")}};
    Row r2{{200LL, std::string("Bob at a distance")}};
    
    engine->insert_row(r1);
    engine->insert_row(r2);
    
    int count = 0;
    engine->scan([&](const Row& row) {
        if (count == 0) {
            EXPECT_EQ(std::get<IntValue>(row.values[0]), 100LL);
            EXPECT_EQ(std::get<TextValue>(row.values[1]), "Alice");
        } else if (count == 1) {
            EXPECT_EQ(std::get<IntValue>(row.values[0]), 200LL);
            EXPECT_EQ(std::get<TextValue>(row.values[1]), "Bob at a distance");
        }
        count++;
        return true; // continue
    });
    
    EXPECT_EQ(count, 2);
}

TEST_F(StorageTest, ReadAtOffsetAndPageBoundary) {
    auto engine = storage_open("test_tbl", test_dir, schema);
    ASSERT_NE(engine, nullptr);
    
    // Insert many large rows to cross 4096-byte page boundary
    std::vector<uint64_t> offsets;
    for (int i = 0; i < 50; ++i) {
        Row r{{(int64_t)i, std::string(100, 'A' + (i % 26))}};
        offsets.push_back(engine->insert_row(r));
    }
    
    // We should have spanned multiple pages since 50 * 108 > 4096
    Row out;
    ASSERT_TRUE(engine->read_row_at_offset(offsets[49], out));
    EXPECT_EQ(std::get<IntValue>(out.values[0]), 49LL);
    EXPECT_EQ(std::get<TextValue>(out.values[1]), std::string(100, 'A' + (49 % 26)));
    
    ASSERT_TRUE(engine->read_row_at_offset(offsets[0], out));
    EXPECT_EQ(std::get<IntValue>(out.values[0]), 0LL);
}

TEST_F(StorageTest, ReloadStorage) {
    uint64_t offset;
    {
        auto engine = storage_open("test_tbl", test_dir, schema);
        offset = engine->insert_row({{(int64_t)999, std::string("ReloadMe")}});
        engine->close(); // flush and close
    }
    
    {
        auto engine2 = storage_open("test_tbl", test_dir, schema);
        Row out;
        ASSERT_TRUE(engine2->read_row_at_offset(offset, out));
        EXPECT_EQ(std::get<IntValue>(out.values[0]), 999LL);
        EXPECT_EQ(std::get<TextValue>(out.values[1]), "ReloadMe");
    }
}
