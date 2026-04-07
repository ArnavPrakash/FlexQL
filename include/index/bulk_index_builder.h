#pragma once

#include <vector>
#include <string>
#include <utility>
#include "common/flexql_types.h"
#include "index/index_manager.h"

namespace flexql {
namespace index {

class BulkIndexBuilder {
public:
    // Accumulate a (key, offset) pair. Does not touch the live BTree.
    void add(const flexql::ColumnValue& key, uint64_t offset);

    // Pre-allocate internal buffer to avoid reallocations.
    void reserve(size_t n) { pairs_.reserve(n); }

    // Sort accumulated pairs by key ascending, then insert into the
    // provided IndexManager in sorted order.
    void flush_to_index(const std::string& table_name, IndexManager& im);

    // Reset internal buffer (allows reuse across batches).
    void reset();

private:
    std::vector<std::pair<flexql::ColumnValue, uint64_t>> pairs_;
};

} // namespace index
} // namespace flexql
