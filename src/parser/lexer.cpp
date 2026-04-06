#include "parser/lexer.h"
#include <cctype>
#include <map>
#include <algorithm>

namespace flexql {
namespace parser {

static std::string to_upper(const std::string& str) {
    std::string res = str;
    for (auto& c : res) c = std::toupper(c);
    return res;
}

std::vector<Token> lexer_tokenize(const std::string& sql) {
    std::vector<Token> tokens;
    size_t i = 0;
    size_t len = sql.length();

    static const std::map<std::string, TokenType> keywords = {
        {"SELECT", TokenType::KW_SELECT},
        {"FROM", TokenType::KW_FROM},
        {"WHERE", TokenType::KW_WHERE},
        {"INSERT", TokenType::KW_INSERT},
        {"INTO", TokenType::KW_INTO},
        {"VALUES", TokenType::KW_VALUES},
        {"CREATE", TokenType::KW_CREATE},
        {"TABLE", TokenType::KW_TABLE},
        {"INNER", TokenType::KW_INNER},
        {"JOIN", TokenType::KW_JOIN},
        {"ON", TokenType::KW_ON},
        {"DECIMAL", TokenType::KW_DECIMAL},
        {"VARCHAR", TokenType::KW_VARCHAR}
    };

    while (i < len) {
        if (std::isspace(sql[i])) {
            i++;
            continue;
        }

        int start = i;
        char c = sql[i];

        if (std::isalpha(c) || c == '_') {
            while (i < len && (std::isalnum(sql[i]) || sql[i] == '_')) {
                i++;
            }
            std::string val = sql.substr(start, i - start);
            std::string upper_val = to_upper(val);
            
            auto it = keywords.find(upper_val);
            if (it != keywords.end()) {
                tokens.push_back({it->second, upper_val, start}); // Normalize keyword to upper
            } else {
                tokens.push_back({TokenType::IDENTIFIER, val, start});
            }
        }
        else if (std::isdigit(c)) {
            while (i < len && std::isdigit(sql[i])) {
                i++;
            }
            tokens.push_back({TokenType::INTEGER_LITERAL, sql.substr(start, i - start), start});
        }
        else if (c == '\'') {
            i++; // skip open quote
            start = i;
            while (i < len && sql[i] != '\'') {
                i++;
            }
            if (i < len) {
                tokens.push_back({TokenType::TEXT_LITERAL, sql.substr(start, i - start), start - 1});
                i++; // skip closing quote
            } else {
                tokens.push_back({TokenType::LEX_ERROR, "Unterminated string literal", start - 1});
                break;
            }
        }
        else {
            TokenType type = TokenType::LEX_ERROR;
            switch (c) {
                case '(': type = TokenType::PUNCT_OPEN_PAREN; break;
                case ')': type = TokenType::PUNCT_CLOSE_PAREN; break;
                case ',': type = TokenType::PUNCT_COMMA; break;
                case '=': type = TokenType::PUNCT_EQUALS; break;
                case '>': type = TokenType::PUNCT_GT; break;
                case '.': type = TokenType::PUNCT_DOT; break;
                case ';': type = TokenType::PUNCT_SEMI; break;
                case '*': type = TokenType::PUNCT_STAR; break;
            }
            
            tokens.push_back({type, std::string(1, c), start});
            i++;
        }
    }
    
    tokens.push_back({TokenType::END_OF_FILE, "", static_cast<int>(len)});
    return tokens;
}

} // namespace parser
} // namespace flexql
