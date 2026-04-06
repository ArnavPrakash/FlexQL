#pragma once

#include "parser/parser.h"
#include <string>

namespace flexql {
namespace parser {

// Print an AST node back to a canonical SQL string
std::string ast_print(const ASTNode& ast);

} // namespace parser
} // namespace flexql
