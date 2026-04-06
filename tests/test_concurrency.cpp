#include <gtest/gtest.h>
#include "concurrency/concurrency.h"
#include <atomic>

using namespace flexql::concurrency;

TEST(ConcurrencyTest, ReadWriteLocks) {
    ConcurrencyManager mgr;
    
    // Multiple readers can acquire
    EXPECT_TRUE(mgr.read_lock("t1", 100));
    EXPECT_TRUE(mgr.read_lock("t1", 100));
    
    // Writer cannot acquire if readers hold it
    EXPECT_FALSE(mgr.write_lock("t1", 50));
    
    mgr.read_unlock("t1");
    mgr.read_unlock("t1");
    
    // Now writer can acquire
    EXPECT_TRUE(mgr.write_lock("t1", 100));
    
    // Now reader cannot acquire
    EXPECT_FALSE(mgr.read_lock("t1", 50));
    
    mgr.write_unlock("t1");
}

TEST(ConcurrencyTest, ThreadPoolExecution) {
    ThreadPool pool(4);
    std::atomic<int> counter = 0;
    
    for (int i = 0; i < 100; i++) {
        pool.submit([&] {
            counter++;
        });
    }
    
    // Wait for a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(counter.load(), 100);
}
