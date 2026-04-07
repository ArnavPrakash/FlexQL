#pragma once
#include <string>
#include <vector>

namespace flexql {
namespace query {

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& root_data_dir);

    // Returns names of all valid databases (subdirs with a tables/ child,
    // plus "default" if root_data_dir/tables/ exists).
    std::vector<std::string> list_databases() const;

    // Returns true if a database with the given name exists.
    bool database_exists(const std::string& name) const;

    // Returns the filesystem path for a named database.
    // For "default" this is root_data_dir_ itself.
    std::string database_path(const std::string& name) const;

private:
    std::string root_data_dir_;
};

} // namespace query
} // namespace flexql
