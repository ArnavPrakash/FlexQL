#include "parser/printer.h"
#include <sstream>

namespace flexql {
namespace parser {

static std::string col_ref_to_str(const ColumnRef& ref) {
    if (ref.table_name.empty()) return ref.column_name;
    return ref.table_name + "." + ref.column_name;
}

static std::string val_to_str(const flexql::ColumnValue& val) {
    if (std::holds_alternative<flexql::IntValue>(val)) {
        return std::to_string(std::get<flexql::IntValue>(val));
    } else {
        return "'" + std::get<flexql::TextValue>(val) + "'";
    }
}

std::string ast_print(const ASTNode& ast) {
    std::stringstream ss;
    switch (ast.type) {
        case ASTNodeType::CREATE_TABLE:
            ss << "CREATE TABLE " << ast.table_name << " (";
            for (size_t i = 0; i < ast.columns.size(); ++i) {
                ss << ast.columns[i].name << " ";
                if (ast.columns[i].type == flexql::ColumnType::INT) ss << "INT";
                else if (ast.columns[i].type == flexql::ColumnType::TEXT) ss << "TEXT";
                if (i < ast.columns.size() - 1) ss << ", ";
            }
            ss << ")";
            break;
            
        case ASTNodeType::INSERT:
        case ASTNodeType::BATCH_INSERT:
            ss << "INSERT INTO " << ast.table_name << " VALUES ";
            for (size_t i = 0; i < ast.insert_values.size(); ++i) {
                ss << "(";
                for (size_t j = 0; j < ast.insert_values[i].size(); ++j) {
                    ss << val_to_str(ast.insert_values[i][j]);
                    if (j < ast.insert_values[i].size() - 1) ss << ", ";
                }
                ss << ")";
                if (i < ast.insert_values.size() - 1) ss << ", ";
            }
            break;
            
        case ASTNodeType::SELECT:
        case ASTNodeType::SELECT_JOIN:
            ss << "SELECT ";
            if (ast.select_star || ast.selected_columns.empty()) {
                ss << "*";
            } else {
                for (size_t i = 0; i < ast.selected_columns.size(); ++i) {
                    ss << col_ref_to_str(ast.selected_columns[i]);
                    if (i < ast.selected_columns.size() - 1) ss << ", ";
                }
            }
            ss << " FROM " << ast.table_name;
            
            if (ast.type == ASTNodeType::SELECT_JOIN) {
                ss << " INNER JOIN " << ast.join_table_name 
                   << " ON " << col_ref_to_str(ast.join_on_left) 
                   << " = " << col_ref_to_str(ast.join_on_right);
            }
            
            if (ast.where_clause) {
                std::string op_str;
                switch (ast.where_clause->op) {
                    case Operator::EQUALS: op_str = " = "; break;
                    case Operator::GT:     op_str = " > "; break;
                    case Operator::LT:     op_str = " < "; break;
                    case Operator::GTE:    op_str = " >= "; break;
                    case Operator::LTE:    op_str = " <= "; break;
                }
                ss << " WHERE " << col_ref_to_str(ast.where_clause->column)
                   << op_str << val_to_str(ast.where_clause->value);
            }
            break;
    }
    return ss.str();
}

} // namespace parser
} // namespace flexql
