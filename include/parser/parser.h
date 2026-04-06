#pragma once

#include "parser/lexer.h"
#include "common/flexql_types.h"
#include <memory>
#include <optional>

namespace flexql {
namespace parser {

enum class ASTNodeType {
    CREATE_TABLE,
    INSERT,
    BATCH_INSERT,
    SELECT,
    SELECT_JOIN
};

enum class Operator { EQUALS, GT };

struct ColumnDef {
    std::string name;
    flexql::ColumnType type;
};

struct ColumnRef {
    std::string table_name; // Empty if unqualified
    std::string column_name;
};

struct WhereCondition {
    ColumnRef column;
    Operator op;
    flexql::ColumnValue value;
};

struct ASTNode {
    ASTNodeType type;
    std::string table_name;
    
    // For CREATE TABLE
    std::vector<ColumnDef> columns;
    
    // For INSERT / BATCH_INSERT
    std::vector<std::vector<flexql::ColumnValue>> insert_values;
    
    // For SELECT / SELECT_JOIN
    std::string join_table_name;
    ColumnRef join_on_left;
    ColumnRef join_on_right;
    
    bool select_star = false;
    std::vector<ColumnRef> selected_columns;
    std::unique_ptr<WhereCondition> where_clause;
};

// Parse a list of tokens into an AST
// Returns nullptr and populates errmsg on error
std::unique_ptr<ASTNode> parser_parse(const std::vector<Token>& tokens, std::string& errmsg_out);

} // namespace parser
} // namespace flexql
