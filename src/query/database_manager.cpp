#include "query/database_manager.h"
#include <filesystem>
#include <algorithm>

namespace flexql {
namespace query {

DatabaseManager::DatabaseManager(const std::string& root_data_dir)
    : root_data_dir_(root_data_dir) {}

std::vector<std::string> DatabaseManager::list_databases() const {
    std::vector<std::string> result;

    try {
        // Check if root_data_dir/tables/ exists → expose as "default"
        std::filesystem::path root(root_data_dir_);
        if (std::filesystem::is_directory(root / "tables")) {
            result.push_back("default");
        }

        // Iterate subdirectories; include iff subdir/tables/ exists
        for (const auto& entry : std::filesystem::directory_iterator(root)) {
            if (!entry.is_directory()) continue;
            if (std::filesystem::is_directory(entry.path() / "tables")) {
                result.push_back(entry.path().filename().string());
            }
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Return whatever was collected; caller may check for empty or handle error
        return result;
    }

    return result;
}

bool DatabaseManager::database_exists(const std::string& name) const {
    const auto dbs = list_databases();
    return std::find(dbs.begin(), dbs.end(), name) != dbs.end();
}

std::string DatabaseManager::database_path(const std::string& name) const {
    if (name == "default") {
        return root_data_dir_;
    }
    return root_data_dir_ + "/" + name;
}

} // namespace query
} // namespace flexql
