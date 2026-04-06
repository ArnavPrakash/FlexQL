#include "storage/schema.h"
#include <fstream>
#include <sstream>

namespace flexql {
namespace storage {

std::shared_ptr<Schema> schema_create(const std::string& table_name, const std::vector<ColumnDef>& columns) {
    auto s = std::make_shared<Schema>();
    s->table_name = table_name;
    s->columns = columns;
    return s;
}

bool schema_save(std::shared_ptr<Schema> schema, const std::string& data_dir) {
    if (!schema) return false;
    std::string path = data_dir + "/tables/" + schema->table_name + ".schema";
    std::ofstream out(path);
    if (!out) return false;
    
    out << schema->table_name << "\n";
    out << schema->columns.size() << "\n";
    for (const auto& col : schema->columns) {
        out << col.name << " " << static_cast<int>(col.type) << "\n";
    }
    return true;
}

std::shared_ptr<Schema> schema_load(const std::string& table_name, const std::string& data_dir) {
    std::string path = data_dir + "/tables/" + table_name + ".schema";
    std::ifstream in(path);
    if (!in) return nullptr;
    
    auto s = std::make_shared<Schema>();
    if (!(in >> s->table_name)) return nullptr;
    
    size_t col_count;
    if (!(in >> col_count)) return nullptr;
    
    for (size_t i = 0; i < col_count; ++i) {
        ColumnDef col;
        int type_int;
        if (!(in >> col.name >> type_int)) return nullptr;
        col.type = static_cast<flexql::ColumnType>(type_int);
        s->columns.push_back(col);
    }
    return s;
}

const ColumnDef* schema_get_column(const Schema& schema, const std::string& col_name) {
    for (const auto& col : schema.columns) {
        if (col.name == col_name) return &col;
    }
    return nullptr;
}

} // namespace storage
} // namespace flexql
