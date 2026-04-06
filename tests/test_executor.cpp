#include <gtest/gtest.h>
#include "query/executor.h"
#include <filesystem>

using namespace flexql;

TEST(ExecutorTest, E2EQueryPipeline) {
    std::string test_dir = "data_exec_test";
    std::filesystem::remove_all(test_dir);
    
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
    
    auto tokens1 = parser::lexer_tokenize(sql1);
    auto ast1 = parser::parser_parse(tokens1, errmsg);
    ASSERT_NE(ast1, nullptr);
    EXPECT_EQ(exec.executor_run(*ast1, sql1, res, errmsg), ErrorCode::OK);
    
    std::string sql2 = "INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob')";
    auto tokens2 = parser::lexer_tokenize(sql2);
    auto ast2 = parser::parser_parse(tokens2, errmsg);
    EXPECT_EQ(exec.executor_run(*ast2, sql2, res, errmsg), ErrorCode::OK);
    
    std::string sql3 = "SELECT * FROM users WHERE id = 1";
    auto tokens3 = parser::lexer_tokenize(sql3);
    auto ast3 = parser::parser_parse(tokens3, errmsg);
    EXPECT_EQ(exec.executor_run(*ast3, sql3, res, errmsg), ErrorCode::OK);
    
    ASSERT_EQ(res.rows.size(), 1);
    EXPECT_EQ(std::get<TextValue>(res.rows[0].values[1]), "Alice");
    
    std::filesystem::remove_all(test_dir);
}
