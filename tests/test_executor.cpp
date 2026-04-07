#include <gtest/gtest.h>
#include "query/executor.h"
#include <filesystem>

using namespace flexql;

TEST(ExecutorTest, E2EQueryPipeline) {
    std::string test_dir = "data_exec_test";
    std::filesystem::remove_all(test_dir);

    // Create a "default" database directory (test_dir/tables/)
    std::filesystem::create_directories(test_dir + "/tables");

    auto sm = std::make_shared<SchemaManager>(test_dir);
    auto wal = std::make_shared<flexql::storage::WAL>(test_dir + "/wal");
    wal->open();
    auto idx = std::make_shared<flexql::index::IndexManager>(test_dir);
    auto lru = std::make_shared<flexql::cache::LRUCache>(100);
    auto mtx = std::make_shared<flexql::concurrency::ConcurrencyManager>();
    
    query::QueryExecutor exec(test_dir, sm, wal, idx, lru, mtx);
    
    std::string sql1 = "CREATE TABLE users (id INT, name VARCHAR)";
    std::string errmsg;
    flexql::ResultSet res;

    // Set up session with active database pointing to "default"
    query::ClientSession session;
    session.active_database = "default";
    session.schema_mgr = std::make_shared<SchemaManager>(test_dir);
    
    auto tokens1 = parser::lexer_tokenize(sql1);
    auto ast1 = parser::parser_parse(tokens1, errmsg);
    ASSERT_NE(ast1, nullptr);
    EXPECT_EQ(exec.executor_run(*ast1, sql1, res, errmsg, session), ErrorCode::OK);
    
    std::string sql2 = "INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob')";
    auto tokens2 = parser::lexer_tokenize(sql2);
    auto ast2 = parser::parser_parse(tokens2, errmsg);
    EXPECT_EQ(exec.executor_run(*ast2, sql2, res, errmsg, session), ErrorCode::OK);
    
    std::string sql3 = "SELECT * FROM users WHERE id = 1";
    auto tokens3 = parser::lexer_tokenize(sql3);
    auto ast3 = parser::parser_parse(tokens3, errmsg);
    EXPECT_EQ(exec.executor_run(*ast3, sql3, res, errmsg, session), ErrorCode::OK);
    
    ASSERT_EQ(res.rows.size(), 1);
    EXPECT_EQ(std::get<TextValue>(res.rows[0].values[1]), "Alice");
    
    std::filesystem::remove_all(test_dir);
}

TEST(ExecutorTest, NoDatabaseSelectedRejectsTableOps) {
    std::string test_dir = "data_exec_nodb_test";
    std::filesystem::remove_all(test_dir);
    std::filesystem::create_directories(test_dir + "/tables");

    auto sm = std::make_shared<SchemaManager>(test_dir);
    auto wal = std::make_shared<flexql::storage::WAL>(test_dir + "/wal");
    wal->open();
    auto idx = std::make_shared<flexql::index::IndexManager>(test_dir);
    auto lru = std::make_shared<flexql::cache::LRUCache>(100);
    auto mtx = std::make_shared<flexql::concurrency::ConcurrencyManager>();

    query::QueryExecutor exec(test_dir, sm, wal, idx, lru, mtx);

    // Session with no active database
    query::ClientSession session;

    std::string errmsg;
    flexql::ResultSet res;

    std::string sql = "CREATE TABLE foo (id INT)";
    auto tokens = parser::lexer_tokenize(sql);
    auto ast = parser::parser_parse(tokens, errmsg);
    ASSERT_NE(ast, nullptr);

    auto code = exec.executor_run(*ast, sql, res, errmsg, session);
    EXPECT_EQ(code, ErrorCode::ERROR);
    EXPECT_EQ(errmsg, "No database selected");

    std::filesystem::remove_all(test_dir);
}
