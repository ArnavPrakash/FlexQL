#include "concurrency/concurrency.h"
#include <chrono>

namespace flexql {
namespace concurrency {

ConcurrencyManager::ConcurrencyManager() {}

std::shared_ptr<std::shared_timed_mutex> ConcurrencyManager::get_or_create_lock(const std::string& table_name) {
    std::lock_guard<std::mutex> lock(registry_mutex_);
    auto it = table_locks_.find(table_name);
    if (it == table_locks_.end()) {
        auto mut = std::make_shared<std::shared_timed_mutex>();
        table_locks_[table_name] = mut;
        return mut;
    }
    return it->second;
}

bool ConcurrencyManager::read_lock(const std::string& table_name, int timeout_ms) {
    auto mut = get_or_create_lock(table_name);
    return mut->try_lock_shared_for(std::chrono::milliseconds(timeout_ms));
}

void ConcurrencyManager::read_unlock(const std::string& table_name) {
    auto mut = get_or_create_lock(table_name);
    mut->unlock_shared();
}

bool ConcurrencyManager::write_lock(const std::string& table_name, int timeout_ms) {
    auto mut = get_or_create_lock(table_name);
    return mut->try_lock_for(std::chrono::milliseconds(timeout_ms));
}

void ConcurrencyManager::write_unlock(const std::string& table_name) {
    auto mut = get_or_create_lock(table_name);
    mut->unlock();
}

void ConcurrencyManager::global_lock() {
    global_mutex_.lock();
}

void ConcurrencyManager::global_unlock() {
    global_mutex_.unlock();
}

} // namespace concurrency
} // namespace flexql
