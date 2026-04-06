#include <gtest/gtest.h>
#include "storage/schema.h"
#include <filesystem>

using namespace flexql::storage;
using namespace flexql::parser;

TEST(SchemaTest, CreateAndGetColumn) {
    std::vector<ColumnDef> cols = {
        {"id", flexql::ColumnType::INT},
        {"name", flexql::ColumnType::TEXT}
    };
    auto schema = schema_create("users", cols);
    
    ASSERT_NE(schema, nullptr);
    EXPECT_EQ(schema->table_name, "users");
    
    const ColumnDef* c = schema_get_column(*schema, "name");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->type, flexql::ColumnType::TEXT);
    
    EXPECT_EQ(schema_get_column(*schema, "unknown"), nullptr);
}

TEST(SchemaTest, SaveAndLoad) {
    std::filesystem::create_directories("data_test/tables");
    
    std::vector<ColumnDef> cols = {
        {"id", flexql::ColumnType::INT},
        {"balance", flexql::ColumnType::INT}
    };
    auto schema = schema_create("accounts", cols);
    
    ASSERT_TRUE(schema_save(schema, "data_test"));
    
    auto loaded = schema_load("accounts", "data_test");
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->table_name, "accounts");
    ASSERT_EQ(loaded->columns.size(), 2);
    EXPECT_EQ(loaded->columns[0].name, "id");
    EXPECT_EQ(loaded->columns[1].name, "balance");
    
    // Load non-existent
    EXPECT_EQ(schema_load("missing", "data_test"), nullptr);
}
