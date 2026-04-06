#pragma once

#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <condition_variable>

namespace flexql {
namespace concurrency {

class ConcurrencyManager {
public:
    ConcurrencyManager();
    ~ConcurrencyManager() = default;

    // Use try_lock_shared_for... but since standard C++ doesn't have try_lock_shared_for natively on shared_mutex 
    // unless it's shared_timed_mutex, we use std::shared_timed_mutex.
    // Returns true if acquired within timeout_ms
    bool read_lock(const std::string& table_name, int timeout_ms);
    void read_unlock(const std::string& table_name);

    bool write_lock(const std::string& table_name, int timeout_ms);
    void write_unlock(const std::string& table_name);

    void global_lock();
    void global_unlock();

private:
    std::mutex global_mutex_;
    std::mutex registry_mutex_;
    // Using shared_timed_mutex requires <shared_mutex>
    std::unordered_map<std::string, std::shared_ptr<std::shared_timed_mutex>> table_locks_;
    
    std::shared_ptr<std::shared_timed_mutex> get_or_create_lock(const std::string& table_name);
};

class ThreadPool {
public:
    explicit ThreadPool(size_t size);
    ~ThreadPool();

    void submit(std::function<void()> task);

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

} // namespace concurrency
} // namespace flexql
