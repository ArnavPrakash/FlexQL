#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include "parser/parser.h"
#include "storage/storage.h"
#include "storage/schema.h"
#include "storage/wal.h"
#include "index/index_manager.h"
#include "concurrency/concurrency.h"
#include "cache/lru_cache.h"
#include "common/flexql_types.h"
#include "query/database_manager.h"
#include "query/client_session.h"

namespace flexql {

class SchemaManager {
public:
    explicit SchemaManager(const std::string& data_dir);
    void add_schema(std::shared_ptr<flexql::storage::Schema> s);
    std::shared_ptr<flexql::storage::Schema> get_schema(const std::string& tname);
    
private:
    std::string dir_;
    std::unordered_map<std::string, std::shared_ptr<flexql::storage::Schema>> schemas_;
};

namespace query {

class QueryExecutor {
public:
    QueryExecutor(
        const std::string& data_dir,
        std::shared_ptr<SchemaManager> schema_manager,
        std::shared_ptr<flexql::storage::WAL> wal,
        std::shared_ptr<flexql::index::IndexManager> index_manager,
        std::shared_ptr<flexql::cache::LRUCache> lru,
        std::shared_ptr<flexql::concurrency::ConcurrencyManager> concurrency
    );

    flexql::ErrorCode executor_run(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& result_out, std::string& errmsg_out, query::ClientSession& session);

    std::shared_ptr<flexql::storage::StorageEngine> get_or_open_engine(const std::string& tname, const std::string& db_path, std::shared_ptr<SchemaManager> schema_mgr);

private:
    std::string data_dir_;
    std::shared_ptr<SchemaManager> schema_mgr_;
    std::shared_ptr<flexql::storage::WAL> wal_;
    std::shared_ptr<flexql::index::IndexManager> index_mgr_;
    std::shared_ptr<flexql::cache::LRUCache> lru_;
    std::shared_ptr<flexql::concurrency::ConcurrencyManager> concurrency_;
    DatabaseManager db_mgr_;

    std::unordered_map<std::string, std::shared_ptr<flexql::storage::StorageEngine>> engines_;

    flexql::ErrorCode run_create_table(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg);
    flexql::ErrorCode run_insert(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg);
    flexql::ErrorCode run_batch_insert(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg);
    flexql::ErrorCode run_select(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& res, query::ClientSession& session, std::string& errmsg);
    flexql::ErrorCode run_select_join(const flexql::parser::ASTNode& ast, const std::string& raw_sql, flexql::ResultSet& res, query::ClientSession& session, std::string& errmsg);
    flexql::ErrorCode run_show_databases(flexql::ResultSet& res, std::string& errmsg);
    flexql::ErrorCode run_use_database(const flexql::parser::ASTNode& ast, query::ClientSession& session, std::string& errmsg);
    flexql::ErrorCode run_create_database(const flexql::parser::ASTNode& ast, std::string& errmsg);
};

} // namespace query
} // namespace flexql
