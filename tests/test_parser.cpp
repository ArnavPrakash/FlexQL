#include <gtest/gtest.h>
#include "parser/lexer.h"
#include "parser/parser.h"
#include "parser/printer.h"

using namespace flexql::parser;

TEST(ParserTest, LexerBasic) {
    auto tokens = lexer_tokenize("SELECT * FROM t;");
    ASSERT_EQ(tokens.size(), 6); // SELECT, *, FROM, t, ;, EOF
    EXPECT_EQ(tokens[0].type, TokenType::KW_SELECT);
    EXPECT_EQ(tokens[1].type, TokenType::PUNCT_STAR);
    EXPECT_EQ(tokens[2].type, TokenType::KW_FROM);
    EXPECT_EQ(tokens[3].type, TokenType::IDENTIFIER);
    EXPECT_EQ(tokens[4].type, TokenType::PUNCT_SEMI);
    EXPECT_EQ(tokens[5].type, TokenType::END_OF_FILE);
}

TEST(ParserTest, ParseCreateTable) {
    std::string sql = "CREATE TABLE users (id INT, name VARCHAR(64), balance DECIMAL)";
    auto tokens = lexer_tokenize(sql);
    std::string errmsg;
    auto ast = parser_parse(tokens, errmsg);
    
    ASSERT_NE(ast, nullptr) << errmsg;
    EXPECT_EQ(ast->type, ASTNodeType::CREATE_TABLE);
    EXPECT_EQ(ast->table_name, "users");
    ASSERT_EQ(ast->columns.size(), 3);
    EXPECT_EQ(ast->columns[0].name, "id");
    EXPECT_EQ(ast->columns[0].type, flexql::ColumnType::INT);
    EXPECT_EQ(ast->columns[1].name, "name");
    EXPECT_EQ(ast->columns[1].type, flexql::ColumnType::TEXT);
    EXPECT_EQ(ast->columns[2].name, "balance");
    EXPECT_EQ(ast->columns[2].type, flexql::ColumnType::INT);
}

TEST(ParserTest, ParseInsert) {
    std::string sql = "INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob');";
    auto tokens = lexer_tokenize(sql);
    std::string errmsg;
    auto ast = parser_parse(tokens, errmsg);
    
    ASSERT_NE(ast, nullptr) << errmsg;
    EXPECT_EQ(ast->type, ASTNodeType::BATCH_INSERT);
    EXPECT_EQ(ast->table_name, "users");
    ASSERT_EQ(ast->insert_values.size(), 2);
    
    EXPECT_EQ(std::get<flexql::IntValue>(ast->insert_values[0][0]), 1);
    EXPECT_EQ(std::get<flexql::TextValue>(ast->insert_values[0][1]), "Alice");
    
    EXPECT_EQ(std::get<flexql::IntValue>(ast->insert_values[1][0]), 2);
    EXPECT_EQ(std::get<flexql::TextValue>(ast->insert_values[1][1]), "Bob");
}

TEST(ParserTest, ParseSelectJoin) {
    std::string sql = "SELECT t1.id, t2.amount FROM t1 INNER JOIN t2 ON t1.id = t2.user_id WHERE t2.amount > 100";
    auto tokens = lexer_tokenize(sql);
    std::string errmsg;
    auto ast = parser_parse(tokens, errmsg);
    
    ASSERT_NE(ast, nullptr) << errmsg;
    EXPECT_EQ(ast->type, ASTNodeType::SELECT_JOIN);
    EXPECT_EQ(ast->table_name, "t1");
    EXPECT_EQ(ast->join_table_name, "t2");
    EXPECT_EQ(ast->join_on_left.column_name, "id");
    EXPECT_EQ(ast->join_on_left.table_name, "t1");
    EXPECT_EQ(ast->join_on_right.column_name, "user_id");
    EXPECT_EQ(ast->join_on_right.table_name, "t2");
    
    ASSERT_TRUE(ast->where_clause != nullptr);
    EXPECT_EQ(ast->where_clause->column.table_name, "t2");
    EXPECT_EQ(ast->where_clause->column.column_name, "amount");
    EXPECT_EQ(ast->where_clause->op, Operator::GT);
    EXPECT_EQ(std::get<flexql::IntValue>(ast->where_clause->value), 100);
}

TEST(ParserTest, RoundTrip) {
    std::string sql = "SELECT a, b FROM t WHERE a = 5";
    auto tokens = lexer_tokenize(sql);
    std::string errmsg;
    auto ast = parser_parse(tokens, errmsg);
    ASSERT_NE(ast, nullptr) << errmsg;
    
    std::string printed = ast_print(*ast);
    EXPECT_EQ(printed, "SELECT a, b FROM t WHERE a = 5");
}
