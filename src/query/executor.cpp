#include "query/executor.h"
#include <filesystem>
#include <cstring>
#include <algorithm>

namespace flexql {

SchemaManager::SchemaManager(const std::string& data_dir) : dir_(data_dir) {
    std::filesystem::create_directories(dir_ + "/tables");
    for (const auto& entry : std::filesystem::directory_iterator(dir_ + "/tables")) {
        if (entry.path().extension() == ".schema") {
            auto s = flexql::storage::schema_load(entry.path().stem(), dir_);
            if (s) schemas_[s->table_name] = s;
        }
    }
}

void SchemaManager::add_schema(std::shared_ptr<flexql::storage::Schema> s) {
    schemas_[s->table_name] = s;
    flexql::storage::schema_save(s, dir_);
}

std::shared_ptr<flexql::storage::Schema> SchemaManager::get_schema(const std::string& tname) {
    auto it = schemas_.find(tname);
    if (it != schemas_.end()) return it->second;
    return nullptr;
}

namespace query {

QueryExecutor::QueryExecutor(
    const std::string& data_dir,
    std::shared_ptr<SchemaManager> schema_manager,
    std::shared_ptr<flexql::storage::WAL> wal,
    std::shared_ptr<flexql::index::IndexManager> index_manager,
    std::shared_ptr<flexql::cache::LRUCache> lru,
    std::shared_ptr<flexql::concurrency::ConcurrencyManager> concurrency
) : data_dir_(data_dir), schema_mgr_(schema_manager), wal_(wal), index_mgr_(index_manager), lru_(lru), concurrency_(concurrency) {}

std::shared_ptr<flexql::storage::StorageEngine> QueryExecutor::get_or_open_engine(const std::string& tname) {
    auto it = engines_.find(tname);
    if (it != engines_.end()) return it->second;
    
    auto s = schema_mgr_->get_schema(tname);
    if (!s) return nullptr;
    
    auto eng = flexql::storage::storage_open(tname, data_dir_, s);
    if (eng) {
        std::shared_ptr<flexql::storage::StorageEngine> shared_eng = std::move(eng);
        engines_[tname] = shared_eng;
        return shared_eng;
    }
    return nullptr;
}

flexql::ErrorCode QueryExecutor::executor_run(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& result_out, std::string& errmsg_out) {
    switch (ast.type) {
        case flexql::parser::ASTNodeType::CREATE_TABLE:
            return run_create_table(ast, errmsg_out);
        case flexql::parser::ASTNodeType::INSERT:
            return run_insert(ast, errmsg_out);
        case flexql::parser::ASTNodeType::BATCH_INSERT:
            return run_batch_insert(ast, errmsg_out);
        case flexql::parser::ASTNodeType::SELECT:
            return run_select(ast, raw_sql, result_out, errmsg_out);
        case flexql::parser::ASTNodeType::SELECT_JOIN:
            return run_select_join(ast, raw_sql, result_out, errmsg_out);
    }
    errmsg_out = "Unsupported AST node";
    return flexql::ErrorCode::ERROR;
}

flexql::ErrorCode QueryExecutor::run_create_table(const flexql::parser::ASTNode& ast, std::string& errmsg) {
    if (!concurrency_->write_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout";
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    if (schema_mgr_->get_schema(ast.table_name)) {
        concurrency_->global_unlock();
        concurrency_->write_unlock(ast.table_name);
        errmsg = "Table already exists";
        return flexql::ErrorCode::ERROR;
    }
    
    auto s = flexql::storage::schema_create(ast.table_name, ast.columns);
    schema_mgr_->add_schema(s);
    concurrency_->global_unlock();
    
    // WAL Append
    flexql::storage::WALRecord rec;
    rec.record_id = 0; // Handled by WAL or we just don't care
    rec.operation_type = flexql::storage::WALOpType::CREATE_TABLE;
    strncpy(rec.table_name, ast.table_name.c_str(), 63);
    rec.table_name[63] = '\0';
    rec.payload_len = 0;
    rec.committed_flag = 0;
    
    uint64_t woff = wal_->append_record(rec);
    
    // Make sure storage file is created
    get_or_open_engine(ast.table_name);
    
    wal_->commit_record(woff);
    lru_->lru_invalidate_table(ast.table_name);
    
    concurrency_->write_unlock(ast.table_name);
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_insert(const flexql::parser::ASTNode& ast, std::string& errmsg) {
    if (ast.insert_values.empty()) return flexql::ErrorCode::ERROR;

    if (!concurrency_->write_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout";
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    auto s = schema_mgr_->get_schema(ast.table_name);
    concurrency_->global_unlock();
    
    if (!s) {
        errmsg = "Table not found";
        concurrency_->write_unlock(ast.table_name);
        return flexql::ErrorCode::NOT_FOUND;
    }
    
    auto eng = get_or_open_engine(ast.table_name);
    
    const auto& tuple = ast.insert_values[0];
    if (tuple.size() != s->columns.size()) {
        errmsg = "Column count mismatch";
        concurrency_->write_unlock(ast.table_name);
        return flexql::ErrorCode::ERROR;
    }
    
    // Type checking
    for (size_t i = 0; i < tuple.size(); i++) {
        if (s->columns[i].type == flexql::ColumnType::INT && !std::holds_alternative<flexql::IntValue>(tuple[i])) {
            errmsg = "Type mismatch: Expected INT";
            concurrency_->write_unlock(ast.table_name);
            return flexql::ErrorCode::TYPE_MISMATCH;
        }
        if (s->columns[i].type == flexql::ColumnType::TEXT && !std::holds_alternative<flexql::TextValue>(tuple[i])) {
            errmsg = "Type mismatch: Expected TEXT";
            concurrency_->write_unlock(ast.table_name);
            return flexql::ErrorCode::TYPE_MISMATCH;
        }
    }
    
    flexql::Row row;
    row.values = tuple;
    
    flexql::storage::WALRecord rec;
    rec.operation_type = flexql::storage::WALOpType::INSERT;
    strncpy(rec.table_name, ast.table_name.c_str(), 63);
    rec.table_name[63] = '\0';
    rec.payload_len = 0; // Simple dummy for WAL payload due to complexity limit
    rec.committed_flag = 0;
    
    uint64_t woff = wal_->append_record(rec);
    
    uint64_t disk_off = eng->insert_row(row);
    index_mgr_->index_manager_insert(ast.table_name, tuple[0], disk_off);
    
    wal_->commit_record(woff);
    
    
    
    concurrency_->global_lock();
    lru_->lru_invalidate_table(ast.table_name);
    concurrency_->global_unlock();
    
    concurrency_->write_unlock(ast.table_name);
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_batch_insert(const flexql::parser::ASTNode& ast, std::string& errmsg) {
    if (!concurrency_->write_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout";
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    auto s = schema_mgr_->get_schema(ast.table_name);
    concurrency_->global_unlock();
    
    if (!s) {
        errmsg = "Table not found";
        concurrency_->write_unlock(ast.table_name);
        return flexql::ErrorCode::NOT_FOUND;
    }
    
    auto eng = get_or_open_engine(ast.table_name);
    
    for (const auto& tuple : ast.insert_values) {
        if (tuple.size() != s->columns.size()) {
            errmsg = "Column count mismatch";
            concurrency_->write_unlock(ast.table_name);
            return flexql::ErrorCode::ERROR;
        }
        for (size_t i = 0; i < tuple.size(); i++) {
            if (s->columns[i].type == flexql::ColumnType::INT && !std::holds_alternative<flexql::IntValue>(tuple[i])) {
                errmsg = "Type mismatch in batch insert";
                concurrency_->write_unlock(ast.table_name);
                return flexql::ErrorCode::TYPE_MISMATCH;
            }
        }
        
        flexql::Row row; row.values = tuple;
        uint64_t disk_off = eng->insert_row(row);
        index_mgr_->index_manager_insert(ast.table_name, tuple[0], disk_off);
    }
    
    flexql::storage::WALRecord rec;
    rec.operation_type = flexql::storage::WALOpType::INSERT;
    strncpy(rec.table_name, ast.table_name.c_str(), 63); rec.table_name[63] = '\0';
    rec.payload_len = 0; rec.committed_flag = 0;
    uint64_t woff = wal_->append_record(rec);
    wal_->commit_record(woff);
    
    
    
    concurrency_->global_lock();
    lru_->lru_invalidate_table(ast.table_name);
    concurrency_->global_unlock();
    
    concurrency_->write_unlock(ast.table_name);
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_select(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& res, std::string& errmsg) {
    concurrency_->global_lock();
    if (lru_->lru_get(raw_sql, res)) {
        concurrency_->global_unlock();
        return flexql::ErrorCode::OK;
    }
    auto s = schema_mgr_->get_schema(ast.table_name);
    concurrency_->global_unlock();
    
    if (!s) { errmsg = "Table not found"; return flexql::ErrorCode::NOT_FOUND; }
    
    if (!concurrency_->read_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout"; return flexql::ErrorCode::TIMEOUT;
    }
    
    auto eng = get_or_open_engine(ast.table_name);
    
    std::vector<int> col_indices;
    if (ast.select_star) {
        for (size_t i = 0; i < s->columns.size(); i++) {
            col_indices.push_back(i);
            res.column_names.push_back(s->columns[i].name);
        }
    } else {
        for (const auto& ref : ast.selected_columns) {
            bool found = false;
            for (size_t i = 0; i < s->columns.size(); i++) {
                if (s->columns[i].name == ref.column_name) {
                    col_indices.push_back(i);
                    res.column_names.push_back(ref.column_name);
                    found = true; break;
                }
            }
            if (!found) {
                errmsg = "Column not found";
                concurrency_->read_unlock(ast.table_name);
                return flexql::ErrorCode::ERROR;
            }
        }
    }
    
    int filter_col_idx = -1;
    if (ast.where_clause) {
        for (size_t i = 0; i < s->columns.size(); i++) {
            if (s->columns[i].name == ast.where_clause->column.column_name) {
                filter_col_idx = i; break;
            }
        }
        if (filter_col_idx == -1) {
            errmsg = "WHERE filter column not found";
            concurrency_->read_unlock(ast.table_name);
            return flexql::ErrorCode::ERROR;
        }
    }
    
    // Check if we can use index
    if (ast.where_clause && ast.where_clause->op == flexql::parser::Operator::EQUALS && filter_col_idx == 0) {
        uint64_t disk_off;
        if (index_mgr_->index_manager_lookup(ast.table_name, ast.where_clause->value, disk_off)) {
            flexql::Row r;
            if (eng->read_row_at_offset(disk_off, r)) {
                flexql::Row proj;
                for (int idx : col_indices) proj.values.push_back(r.values[idx]);
                res.rows.push_back(proj);
            }
        }
    } else {
        // Table Scan
        eng->scan([&](const flexql::Row& r){
            if (ast.where_clause) {
                const auto& cell = r.values[filter_col_idx];
                const auto& cond_val = ast.where_clause->value;
                if (ast.where_clause->op == flexql::parser::Operator::EQUALS) {
                    if (flexql::index::compare_val(cell, cond_val) != 0) return true;
                } else if (ast.where_clause->op == flexql::parser::Operator::GT) {
                    if (flexql::index::compare_val(cell, cond_val) <= 0) return true;
                }
            }
            flexql::Row proj;
            for (int idx : col_indices) proj.values.push_back(r.values[idx]);
            res.rows.push_back(proj);
            return true;
        });
    }
    
    concurrency_->read_unlock(ast.table_name);
    
    concurrency_->global_lock();
    lru_->lru_put(raw_sql, res);
    concurrency_->global_unlock();
    
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_select_join(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& res, std::string& errmsg) {
    if (!concurrency_->read_lock(ast.table_name, 5000)) return flexql::ErrorCode::TIMEOUT;
    if (!concurrency_->read_lock(ast.join_table_name, 5000)) {
        concurrency_->read_unlock(ast.table_name);
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    auto s1 = schema_mgr_->get_schema(ast.table_name);
    auto s2 = schema_mgr_->get_schema(ast.join_table_name);
    concurrency_->global_unlock();
    
    if (!s1 || !s2) {
        concurrency_->read_unlock(ast.join_table_name);
        concurrency_->read_unlock(ast.table_name);
        errmsg = "Join tables not found in schema";
        return flexql::ErrorCode::NOT_FOUND;
    }
    
    auto eng1 = get_or_open_engine(ast.table_name);
    auto eng2 = get_or_open_engine(ast.join_table_name);
    
    // Find join indexes
    int j_col1 = -1, j_col2 = -1;
    for(size_t i=0; i<s1->columns.size(); i++) if(s1->columns[i].name == ast.join_on_left.column_name) j_col1 = i;
    for(size_t i=0; i<s2->columns.size(); i++) if(s2->columns[i].name == ast.join_on_right.column_name) j_col2 = i;
    
    if (j_col1 == -1 || j_col2 == -1) { errmsg = "Join columns missing"; return flexql::ErrorCode::ERROR; }
    
    // Find selected columns and where clause resolving
    // In naive nested loop join, we just stream. We append s1 then s2 columns.
    
    std::vector<flexql::Row> all_joined;
    
    eng1->scan([&](const flexql::Row& r1){
        eng2->scan([&](const flexql::Row& r2){
            if (flexql::index::compare_val(r1.values[j_col1], r2.values[j_col2]) == 0) {
                // Verify where clause manually post-join
                bool match = true;
                if (ast.where_clause) {
                    flexql::ColumnValue cmp_val;
                    if (ast.where_clause->column.table_name == ast.table_name) {
                        for(size_t i=0; i<s1->columns.size(); i++) {
                            if (s1->columns[i].name == ast.where_clause->column.column_name) cmp_val = r1.values[i];
                        }
                    } else {
                        for(size_t i=0; i<s2->columns.size(); i++) {
                            if (s2->columns[i].name == ast.where_clause->column.column_name) cmp_val = r2.values[i];
                        }
                    }
                    if (ast.where_clause->op == flexql::parser::Operator::GT && flexql::index::compare_val(cmp_val, ast.where_clause->value) <= 0) match = false;
                    if (ast.where_clause->op == flexql::parser::Operator::EQUALS && flexql::index::compare_val(cmp_val, ast.where_clause->value) != 0) match = false;
                }
                
                if (match) {
                    flexql::Row r_join;
                    // Projections... (Simplified to handle * or explicitly named refs ignoring fully qualified names mappings for brevity)
                    if (ast.select_star) {
                        for(const auto& v : r1.values) r_join.values.push_back(v);
                        for(const auto& v : r2.values) r_join.values.push_back(v);
                    } else {
                        for(const auto& col : ast.selected_columns) {
                            if (col.table_name == ast.table_name) {
                                for(size_t i=0; i<s1->columns.size(); i++) if(s1->columns[i].name == col.column_name) r_join.values.push_back(r1.values[i]);
                            } else {
                                for(size_t i=0; i<s2->columns.size(); i++) if(s2->columns[i].name == col.column_name) r_join.values.push_back(r2.values[i]);
                            }
                        }
                    }
                    all_joined.push_back(r_join);
                }
            }
            return true;
        });
        return true;
    });
    
    res.rows = std::move(all_joined);
    
    concurrency_->read_unlock(ast.join_table_name);
    concurrency_->read_unlock(ast.table_name);
    return flexql::ErrorCode::OK;
}

} // namespace query
} // namespace flexql
