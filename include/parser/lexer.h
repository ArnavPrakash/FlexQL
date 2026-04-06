#pragma once

#include <string>
#include <vector>

namespace flexql {
namespace parser {

enum class TokenType {
    IDENTIFIER,
    INTEGER_LITERAL,
    TEXT_LITERAL,
    
    // Punctuation
    PUNCT_OPEN_PAREN,   // (
    PUNCT_CLOSE_PAREN,  // )
    PUNCT_COMMA,        // ,
    PUNCT_EQUALS,       // =
    PUNCT_GT,           // >
    PUNCT_DOT,          // .
    PUNCT_SEMI,         // ;
    PUNCT_STAR,         // *

    // Keywords
    KW_SELECT,
    KW_FROM,
    KW_WHERE,
    KW_INSERT,
    KW_INTO,
    KW_VALUES,
    KW_CREATE,
    KW_TABLE,
    KW_INNER,
    KW_JOIN,
    KW_ON,
    KW_DECIMAL,
    KW_VARCHAR,

    LEX_ERROR,
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string value; // Stores original text
    int pos;
};

// Tokenize SQL string into a list of tokens
std::vector<Token> lexer_tokenize(const std::string& sql);

} // namespace parser
} // namespace flexql
