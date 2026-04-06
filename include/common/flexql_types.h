#pragma once

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <map>
#include <cstdint>
#include <stdexcept>

namespace flexql {

// Forward declarations
struct ASTNode;
class FlexQL;

// Basic Types
enum class ColumnType {
    INT,
    TEXT,
    DECIMAL,     // Parsed but mapped to INT
    VARCHAR      // Parsed but mapped to TEXT
};

enum class ErrorCode {
    OK = 0,
    ERROR = 1,
    NOT_FOUND,
    INVALID_SQL,
    TYPE_MISMATCH,
    TIMEOUT
};

// Values
using IntValue = int64_t;
using TextValue = std::string;
using ColumnValue = std::variant<IntValue, TextValue>;

// Row & ResultSet
struct Row {
    std::vector<ColumnValue> values;
};

struct ResultSet {
    std::vector<std::string> column_names;
    std::vector<Row> rows;
};

} // namespace flexql
