#pragma once
#include <string>
#include <memory>

namespace flexql {

class SchemaManager;

namespace query {

struct ClientSession {
    std::string active_database;          // empty = no database selected
    std::shared_ptr<flexql::SchemaManager> schema_mgr; // scoped to active_database
};

} // namespace query
} // namespace flexql
