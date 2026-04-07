#include "parser/parser.h"

namespace flexql {
namespace parser {

struct ParserState {
    const std::vector<Token>& tokens;
    size_t pos;
    std::string* errmsg;

    const Token& peek() {
        if (pos >= tokens.size()) return tokens.back(); // EOF
        return tokens[pos];
    }
    
    const Token& advance() {
        if (pos >= tokens.size()) return tokens.back();
        return tokens[pos++];
    }
    
    bool match(TokenType type) {
        if (peek().type == type) {
            advance();
            return true;
        }
        return false;
    }

    bool check(TokenType type) {
        return peek().type == type;
    }

    void error(const std::string& msg) {
        if (errmsg && errmsg->empty()) {
            *errmsg = "Parse error at pos " + std::to_string(peek().pos) + " ('" + peek().value + "'): " + msg;
        }
    }
};

static ColumnRef parse_column_ref(ParserState& st) {
    ColumnRef ref;
    const Token& t1 = st.advance();
    if (t1.type != TokenType::IDENTIFIER) {
        st.error("Expected identifier in column reference");
        return ref;
    }
    
    if (st.match(TokenType::PUNCT_DOT)) {
        ref.table_name = t1.value;
        const Token& t2 = st.advance();
        if (t2.type != TokenType::IDENTIFIER) {
            st.error("Expected column name after '.'");
        } else {
            ref.column_name = t2.value;
        }
    } else {
        ref.column_name = t1.value;
    }
    return ref;
}

static std::unique_ptr<ASTNode> parse_create_table(ParserState& st) {
    auto ast = std::make_unique<ASTNode>();
    ast->type = ASTNodeType::CREATE_TABLE;
    
    if (!st.match(TokenType::KW_TABLE)) { st.error("Expected TABLE or DATABASE after CREATE"); return nullptr; }
    
    const Token& t_name = st.advance();
    if (t_name.type != TokenType::IDENTIFIER) { st.error("Expected table name"); return nullptr; }
    ast->table_name = t_name.value;
    
    if (!st.match(TokenType::PUNCT_OPEN_PAREN)) { st.error("Expected '('"); return nullptr; }
    
    while (!st.check(TokenType::PUNCT_CLOSE_PAREN) && !st.check(TokenType::END_OF_FILE)) {
        const Token& c_name = st.advance();
        if (c_name.type != TokenType::IDENTIFIER) { st.error("Expected column name"); return nullptr; }
        
        const Token& c_type = st.advance();
        flexql::ColumnType mapped_type;
        if (c_type.type == TokenType::IDENTIFIER && c_type.value == "INT") {
            mapped_type = flexql::ColumnType::INT;
        } else if (c_type.type == TokenType::IDENTIFIER && c_type.value == "TEXT") {
            mapped_type = flexql::ColumnType::TEXT;
        } else if (c_type.type == TokenType::KW_DECIMAL) {
            mapped_type = flexql::ColumnType::INT; // Normalize
        } else if (c_type.type == TokenType::KW_VARCHAR) {
            mapped_type = flexql::ColumnType::TEXT; // Normalize
            // Handle optional (N) pattern for VARCHAR
            if (st.match(TokenType::PUNCT_OPEN_PAREN)) {
                if (!st.match(TokenType::INTEGER_LITERAL)) {
                    st.error("Expected integer length for VARCHAR"); return nullptr;
                }
                if (!st.match(TokenType::PUNCT_CLOSE_PAREN)) {
                    st.error("Expected ')' after VARCHAR length"); return nullptr;
                }
            }
        } else {
            st.error("Unsupported column type: " + c_type.value);
            return nullptr;
        }
        
        ast->columns.push_back({c_name.value, mapped_type});
        
        if (!st.match(TokenType::PUNCT_COMMA)) break;
    }
    
    if (!st.match(TokenType::PUNCT_CLOSE_PAREN)) { st.error("Expected ')'"); return nullptr; }
    return ast;
}

static std::unique_ptr<ASTNode> parse_create(ParserState& st) {
    if (st.match(TokenType::KW_DATABASE)) {
        const Token& name_tok = st.advance();
        if (name_tok.type != TokenType::IDENTIFIER) {
            st.error("Expected database name after CREATE DATABASE");
            return nullptr;
        }
        auto ast = std::make_unique<ASTNode>();
        ast->type = ASTNodeType::CREATE_DATABASE;
        ast->table_name = name_tok.value;
        return ast;
    }
    return parse_create_table(st);
}

static std::unique_ptr<ASTNode> parse_insert(ParserState& st) {
    auto ast = std::make_unique<ASTNode>();
    
    if (!st.match(TokenType::KW_INTO)) { st.error("Expected INTO after INSERT"); return nullptr; }
    
    const Token& t_name = st.advance();
    if (t_name.type != TokenType::IDENTIFIER) { st.error("Expected table name"); return nullptr; }
    ast->table_name = t_name.value;
    
    if (!st.match(TokenType::KW_VALUES)) { st.error("Expected VALUES"); return nullptr; }
    
    int tuple_count = 0;
    do {
        if (!st.match(TokenType::PUNCT_OPEN_PAREN)) { st.error("Expected '(' before values"); return nullptr; }
        
        std::vector<flexql::ColumnValue> tuple_vals;
        while (!st.check(TokenType::PUNCT_CLOSE_PAREN) && !st.check(TokenType::END_OF_FILE)) {
            const Token& val_tok = st.advance();
            if (val_tok.type == TokenType::INTEGER_LITERAL) {
                tuple_vals.push_back(std::stoll(val_tok.value));
            } else if (val_tok.type == TokenType::TEXT_LITERAL) {
                tuple_vals.push_back(val_tok.value);
            } else {
                st.error("Expected literal value"); return nullptr;
            }
            if (!st.match(TokenType::PUNCT_COMMA)) break;
        }
        if (!st.match(TokenType::PUNCT_CLOSE_PAREN)) { st.error("Expected ')' after values"); return nullptr; }
        
        ast->insert_values.push_back(tuple_vals);
        tuple_count++;
    } while (st.match(TokenType::PUNCT_COMMA));
    
    ast->type = (tuple_count > 1) ? ASTNodeType::BATCH_INSERT : ASTNodeType::INSERT;
    return ast;
}

static std::unique_ptr<ASTNode> parse_select(ParserState& st) {
    auto ast = std::make_unique<ASTNode>();
    ast->type = ASTNodeType::SELECT;
    
    // Parse Projection
    if (st.match(TokenType::PUNCT_STAR)) {
        ast->select_star = true;
    } else {
        ast->select_star = false;
        do {
            ColumnRef ref = parse_column_ref(st);
            if (st.errmsg && !st.errmsg->empty()) return nullptr;
            ast->selected_columns.push_back(ref);
        } while (st.match(TokenType::PUNCT_COMMA));
    }
    
    if (!st.match(TokenType::KW_FROM)) { st.error("Expected FROM"); return nullptr; }
    
    const Token& t_name = st.advance();
    if (t_name.type != TokenType::IDENTIFIER) { st.error("Expected table name"); return nullptr; }
    ast->table_name = t_name.value;
    
    // Optional INNER JOIN
    if (st.match(TokenType::KW_INNER)) {
        if (!st.match(TokenType::KW_JOIN)) { st.error("Expected JOIN after INNER"); return nullptr; }
        const Token& t_join = st.advance();
        if (t_join.type != TokenType::IDENTIFIER) { st.error("Expected JOIN table name"); return nullptr; }
        ast->join_table_name = t_join.value;
        ast->type = ASTNodeType::SELECT_JOIN;
        
        if (!st.match(TokenType::KW_ON)) { st.error("Expected ON for JOIN"); return nullptr; }
        
        ast->join_on_left = parse_column_ref(st);
        if (st.errmsg && !st.errmsg->empty()) return nullptr;
        if (!st.match(TokenType::PUNCT_EQUALS)) { st.error("Expected '=' in JOIN condition"); return nullptr; }
        ast->join_on_right = parse_column_ref(st);
        if (st.errmsg && !st.errmsg->empty()) return nullptr;
    }
    
    // Optional WHERE
    if (st.match(TokenType::KW_WHERE)) {
        ast->where_clause = std::make_unique<WhereCondition>();
        ast->where_clause->column = parse_column_ref(st);
        if (st.errmsg && !st.errmsg->empty()) return nullptr;
        
        if (st.match(TokenType::PUNCT_EQUALS)) {
            ast->where_clause->op = Operator::EQUALS;
        } else if (st.match(TokenType::PUNCT_GTE)) {
            ast->where_clause->op = Operator::GTE;
        } else if (st.match(TokenType::PUNCT_GT)) {
            ast->where_clause->op = Operator::GT;
        } else if (st.match(TokenType::PUNCT_LTE)) {
            ast->where_clause->op = Operator::LTE;
        } else if (st.match(TokenType::PUNCT_LT)) {
            ast->where_clause->op = Operator::LT;
        } else {
            st.error("Expected '=', '>', '<', '>=' or '<=' in WHERE"); return nullptr;
        }
        
        const Token& v_tok = st.advance();
        if (v_tok.type == TokenType::INTEGER_LITERAL) {
            ast->where_clause->value = std::stoll(v_tok.value);
        } else if (v_tok.type == TokenType::TEXT_LITERAL) {
            ast->where_clause->value = v_tok.value;
        } else {
            st.error("Expected literal value in WHERE"); return nullptr;
        }
    }
    
    return ast;
}

static std::unique_ptr<ASTNode> parse_show(ParserState& st) {
    if (!st.match(TokenType::KW_DATABASES)) {
        st.error("Expected DATABASES after SHOW");
        return nullptr;
    }
    auto ast = std::make_unique<ASTNode>();
    ast->type = ASTNodeType::SHOW_DATABASES;
    return ast;
}

static std::unique_ptr<ASTNode> parse_use(ParserState& st) {
    if (!st.match(TokenType::KW_DATABASE)) {
        st.error("Expected DATABASE after USE");
        return nullptr;
    }
    const Token& name_tok = st.advance();
    if (name_tok.type != TokenType::IDENTIFIER) {
        st.error("Expected database name after USE DATABASE");
        return nullptr;
    }
    auto ast = std::make_unique<ASTNode>();
    ast->type = ASTNodeType::USE_DATABASE;
    ast->table_name = name_tok.value; // reused as db_name
    return ast;
}

std::unique_ptr<ASTNode> parser_parse(const std::vector<Token>& tokens, std::string& errmsg_out) {
    if (tokens.empty() || tokens[0].type == TokenType::END_OF_FILE) {
        errmsg_out = "Empty query";
        return nullptr;
    }
    
    ParserState st{tokens, 0, &errmsg_out};
    
    if (st.peek().type == TokenType::LEX_ERROR) {
        st.error("Lexical error");
        return nullptr;
    }

    std::unique_ptr<ASTNode> ast = nullptr;
    
    if (st.match(TokenType::KW_CREATE)) {
        ast = parse_create(st);
    } else if (st.match(TokenType::KW_INSERT)) {
        ast = parse_insert(st);
    } else if (st.match(TokenType::KW_SELECT)) {
        ast = parse_select(st);
    } else if (st.match(TokenType::KW_SHOW)) {
        ast = parse_show(st);
    } else if (st.match(TokenType::KW_USE)) {
        ast = parse_use(st);
    } else {
        st.error("Unrecognized statement type");
    }
    
    if (ast && !errmsg_out.empty()) {
        return nullptr; // An error occurred late
    }
    
    if (ast && st.peek().type == TokenType::PUNCT_SEMI) {
        st.advance();
    }
    
    if (ast && st.peek().type != TokenType::END_OF_FILE) {
        if (errmsg_out.empty()) {
            st.error("Unexpected tokens after statement");
        }
        return nullptr;
    }

    return ast;
}

} // namespace parser
} // namespace flexql
