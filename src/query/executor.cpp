#include "query/executor.h"
#include "index/bulk_index_builder.h"
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
) : data_dir_(data_dir), schema_mgr_(schema_manager), wal_(wal), index_mgr_(index_manager), lru_(lru), concurrency_(concurrency), db_mgr_(data_dir) {}

std::shared_ptr<flexql::storage::StorageEngine> QueryExecutor::get_or_open_engine(const std::string& tname, const std::string& db_path, std::shared_ptr<SchemaManager> schema_mgr) {
    std::string key = db_path + "|" + tname;
    auto it = engines_.find(key);
    if (it != engines_.end()) return it->second;
    
    auto s = schema_mgr->get_schema(tname);
    if (!s) return nullptr;
    
    auto eng = flexql::storage::storage_open(tname, db_path, s);
    if (eng) {
        std::shared_ptr<flexql::storage::StorageEngine> shared_eng = std::move(eng);
        engines_[key] = shared_eng;
        return shared_eng;
    }
    return nullptr;
}

flexql::ErrorCode QueryExecutor::executor_run(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& result_out, std::string& errmsg_out, query::ClientSession& session) {
    switch (ast.type) {
        case flexql::parser::ASTNodeType::CREATE_TABLE:
            return run_create_table(ast, session, errmsg_out);
        case flexql::parser::ASTNodeType::INSERT:
            return run_insert(ast, session, errmsg_out);
        case flexql::parser::ASTNodeType::BATCH_INSERT:
            return run_batch_insert(ast, session, errmsg_out);
        case flexql::parser::ASTNodeType::SELECT:
            return run_select(ast, raw_sql, result_out, session, errmsg_out);
        case flexql::parser::ASTNodeType::SELECT_JOIN:
            return run_select_join(ast, raw_sql, result_out, session, errmsg_out);
        case flexql::parser::ASTNodeType::SHOW_DATABASES:
            return run_show_databases(result_out, errmsg_out);
        case flexql::parser::ASTNodeType::USE_DATABASE:
            return run_use_database(ast, session, errmsg_out);
        case flexql::parser::ASTNodeType::CREATE_DATABASE:
            return run_create_database(ast, errmsg_out);
    }
    errmsg_out = "Unsupported AST node";
    return flexql::ErrorCode::ERROR;
}

flexql::ErrorCode QueryExecutor::run_create_table(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg) {
    if (session.active_database.empty()) {
        errmsg = "No database selected";
        return flexql::ErrorCode::ERROR;
    }
    std::string db_path = db_mgr_.database_path(session.active_database);

    if (!concurrency_->write_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout";
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    if (session.schema_mgr->get_schema(ast.table_name)) {
        concurrency_->global_unlock();
        concurrency_->write_unlock(ast.table_name);
        errmsg = "Table already exists";
        return flexql::ErrorCode::ERROR;
    }
    
    auto s = flexql::storage::schema_create(ast.table_name, ast.columns);
    session.schema_mgr->add_schema(s);
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
    get_or_open_engine(ast.table_name, db_path, session.schema_mgr);
    
    wal_->commit_record(woff);
    lru_->lru_invalidate_table(ast.table_name);
    
    concurrency_->write_unlock(ast.table_name);
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_insert(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg) {
    if (session.active_database.empty()) {
        errmsg = "No database selected";
        return flexql::ErrorCode::ERROR;
    }
    std::string db_path = db_mgr_.database_path(session.active_database);

    if (ast.insert_values.empty()) return flexql::ErrorCode::ERROR;

    if (!concurrency_->write_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout";
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    auto s = session.schema_mgr->get_schema(ast.table_name);
    concurrency_->global_unlock();
    
    if (!s) {
        errmsg = "Table not found";
        concurrency_->write_unlock(ast.table_name);
        return flexql::ErrorCode::NOT_FOUND;
    }
    
    auto eng = get_or_open_engine(ast.table_name, db_path, session.schema_mgr);
    
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

flexql::ErrorCode QueryExecutor::run_batch_insert(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg) {
    if (session.active_database.empty()) {
        errmsg = "No database selected";
        return flexql::ErrorCode::ERROR;
    }
    std::string db_path = db_mgr_.database_path(session.active_database);

    if (!concurrency_->write_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout";
        return flexql::ErrorCode::TIMEOUT;
    }

    concurrency_->global_lock();
    auto s = session.schema_mgr->get_schema(ast.table_name);
    concurrency_->global_unlock();

    if (!s) {
        errmsg = "Table not found";
        lru_->lru_invalidate_table(ast.table_name);
        concurrency_->write_unlock(ast.table_name);
        return flexql::ErrorCode::NOT_FOUND;
    }

    // Validate ALL rows before touching storage
    for (const auto& tuple : ast.insert_values) {
        if (tuple.size() != s->columns.size()) {
            errmsg = "Column count mismatch";
            lru_->lru_invalidate_table(ast.table_name);
            concurrency_->write_unlock(ast.table_name);
            return flexql::ErrorCode::ERROR;
        }
        for (size_t i = 0; i < tuple.size(); i++) {
            if (s->columns[i].type == flexql::ColumnType::INT && !std::holds_alternative<flexql::IntValue>(tuple[i])) {
                errmsg = "Type mismatch in batch insert";
                lru_->lru_invalidate_table(ast.table_name);
                concurrency_->write_unlock(ast.table_name);
                return flexql::ErrorCode::TYPE_MISMATCH;
            }
            if (s->columns[i].type == flexql::ColumnType::TEXT && !std::holds_alternative<flexql::TextValue>(tuple[i])) {
                errmsg = "Type mismatch in batch insert";
                lru_->lru_invalidate_table(ast.table_name);
                concurrency_->write_unlock(ast.table_name);
                return flexql::ErrorCode::TYPE_MISMATCH;
            }
        }
    }

    auto eng = get_or_open_engine(ast.table_name, db_path, session.schema_mgr);

    // Build Row vector — move values directly to avoid copying
    std::vector<flexql::Row> rows;
    rows.reserve(ast.insert_values.size());
    for (const auto& tuple : ast.insert_values) {
        rows.push_back(flexql::Row{tuple});
    }

    // 1. Write WAL batch record (no fsync yet)
    uint64_t woff = wal_->append_batch_record(ast.table_name, rows, s);

    // 2. Bulk write to storage — single sequential page flush
    std::vector<uint64_t> offsets = eng->bulk_insert_rows(rows);

    // 3. Build index in one sorted pass
    flexql::index::BulkIndexBuilder builder;
    builder.reserve(rows.size());
    for (size_t i = 0; i < rows.size(); i++) {
        builder.add(rows[i].values[0], offsets[i]);
    }
    builder.flush_to_index(ast.table_name, *index_mgr_);

    // 4. Single fsync
    wal_->commit_record(woff);

    concurrency_->write_unlock(ast.table_name);

    // 5. LRU invalidation outside the write lock
    lru_->lru_invalidate_table(ast.table_name);

    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_select(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& res, query::ClientSession& session, std::string& errmsg) {
    if (session.active_database.empty()) {
        errmsg = "No database selected";
        return flexql::ErrorCode::ERROR;
    }
    std::string db_path = db_mgr_.database_path(session.active_database);

    concurrency_->global_lock();
    if (lru_->lru_get(raw_sql, res)) {
        concurrency_->global_unlock();
        return flexql::ErrorCode::OK;
    }
    auto s = session.schema_mgr->get_schema(ast.table_name);
    concurrency_->global_unlock();
    
    if (!s) { errmsg = "Table not found"; return flexql::ErrorCode::NOT_FOUND; }
    
    if (!concurrency_->read_lock(ast.table_name, 5000)) {
        errmsg = "Lock timeout"; return flexql::ErrorCode::TIMEOUT;
    }
    
    auto eng = get_or_open_engine(ast.table_name, db_path, session.schema_mgr);
    
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
                } else if (ast.where_clause->op == flexql::parser::Operator::LT) {
                    if (flexql::index::compare_val(cell, cond_val) >= 0) return true;
                } else if (ast.where_clause->op == flexql::parser::Operator::GTE) {
                    if (flexql::index::compare_val(cell, cond_val) < 0) return true;
                } else if (ast.where_clause->op == flexql::parser::Operator::LTE) {
                    if (flexql::index::compare_val(cell, cond_val) > 0) return true;
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

flexql::ErrorCode QueryExecutor::run_select_join(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& res, query::ClientSession& session, std::string& errmsg) {
    if (session.active_database.empty()) {
        errmsg = "No database selected";
        return flexql::ErrorCode::ERROR;
    }
    std::string db_path = db_mgr_.database_path(session.active_database);

    if (!concurrency_->read_lock(ast.table_name, 5000)) return flexql::ErrorCode::TIMEOUT;
    if (!concurrency_->read_lock(ast.join_table_name, 5000)) {
        concurrency_->read_unlock(ast.table_name);
        return flexql::ErrorCode::TIMEOUT;
    }
    
    concurrency_->global_lock();
    auto s1 = session.schema_mgr->get_schema(ast.table_name);
    auto s2 = session.schema_mgr->get_schema(ast.join_table_name);
    concurrency_->global_unlock();
    
    if (!s1 || !s2) {
        concurrency_->read_unlock(ast.join_table_name);
        concurrency_->read_unlock(ast.table_name);
        errmsg = "Join tables not found in schema";
        return flexql::ErrorCode::NOT_FOUND;
    }
    
    auto eng1 = get_or_open_engine(ast.table_name, db_path, session.schema_mgr);
    auto eng2 = get_or_open_engine(ast.join_table_name, db_path, session.schema_mgr);
    
    // Find join indexes
    int j_col1 = -1, j_col2 = -1;
    for(size_t i=0; i<s1->columns.size(); i++) if(s1->columns[i].name == ast.join_on_left.column_name) j_col1 = i;
    for(size_t i=0; i<s2->columns.size(); i++) if(s2->columns[i].name == ast.join_on_right.column_name) j_col2 = i;
    
    if (j_col1 == -1 || j_col2 == -1) { errmsg = "Join columns missing"; return flexql::ErrorCode::ERROR; }

    // Populate column names for the result set
    if (ast.select_star) {
        for (const auto& col : s1->columns) res.column_names.push_back(ast.table_name + "." + col.name);
        for (const auto& col : s2->columns) res.column_names.push_back(ast.join_table_name + "." + col.name);
    } else {
        for (const auto& ref : ast.selected_columns) {
            res.column_names.push_back(ref.column_name);
        }
    }
    
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
                    if (ast.where_clause->op == flexql::parser::Operator::LT && flexql::index::compare_val(cmp_val, ast.where_clause->value) >= 0) match = false;
                    if (ast.where_clause->op == flexql::parser::Operator::GTE && flexql::index::compare_val(cmp_val, ast.where_clause->value) < 0) match = false;
                    if (ast.where_clause->op == flexql::parser::Operator::LTE && flexql::index::compare_val(cmp_val, ast.where_clause->value) > 0) match = false;
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

flexql::ErrorCode QueryExecutor::run_show_databases(flexql::ResultSet& res, std::string& errmsg) {
    // Check if the root data directory is accessible before listing
    std::error_code ec;
    bool root_accessible = std::filesystem::is_directory(data_dir_, ec);
    if (!root_accessible || ec) {
        errmsg = "Cannot access data directory";
        return flexql::ErrorCode::ERROR;
    }

    auto databases = db_mgr_.list_databases();

    res.column_names = {"Database"};
    for (const auto& name : databases) {
        flexql::Row row;
        row.values.push_back(flexql::TextValue{name});
        res.rows.push_back(row);
    }
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_use_database(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg) {
    if (!db_mgr_.database_exists(ast.table_name)) {
        errmsg = "Unknown database: " + ast.table_name;
        return flexql::ErrorCode::ERROR;
    }
    session.active_database = ast.table_name;
    session.schema_mgr = std::make_shared<SchemaManager>(
        db_mgr_.database_path(ast.table_name));
    return flexql::ErrorCode::OK;
}

flexql::ErrorCode QueryExecutor::run_create_database(const flexql::parser::ASTNode& ast, std::string& errmsg) {
    if (ast.table_name.empty()) {
        errmsg = "Database name cannot be empty";
        return flexql::ErrorCode::ERROR;
    }
    std::string db_path = data_dir_ + "/" + ast.table_name + "/tables";
    std::error_code ec;
    std::filesystem::create_directories(db_path, ec);
    if (ec) {
        errmsg = "Cannot create database: " + ec.message();
        return flexql::ErrorCode::ERROR;
    }
    return flexql::ErrorCode::OK;
}

} // namespace query
} // namespace flexql
