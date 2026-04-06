#pragma once

#include <string>
#include <vector>
#include <memory>
#include "parser/parser.h"

namespace flexql {
namespace storage {

using flexql::parser::ColumnDef;

struct Schema {
    std::string table_name;
    std::vector<ColumnDef> columns;
};

// Create a new schema in memory
std::shared_ptr<Schema> schema_create(const std::string& table_name, const std::vector<ColumnDef>& columns);

// Save schema to data_dir/tables/table_name.schema
bool schema_save(std::shared_ptr<Schema> schema, const std::string& data_dir);

// Load schema from file or return nullptr
std::shared_ptr<Schema> schema_load(const std::string& table_name, const std::string& data_dir);

// Return pointer to column definition or nullptr if not found
const ColumnDef* schema_get_column(const Schema& schema, const std::string& col_name);

} // namespace storage
} // namespace flexql
