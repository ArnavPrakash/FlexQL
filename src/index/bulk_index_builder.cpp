#include "index/bulk_index_builder.h"
#include "index/btree.h"
#include <algorithm>

namespace flexql {
namespace index {

void BulkIndexBuilder::add(const flexql::ColumnValue& key, uint64_t offset) {
    pairs_.emplace_back(key, offset);
}

void BulkIndexBuilder::flush_to_index(const std::string& table_name, IndexManager& im) {
    std::sort(pairs_.begin(), pairs_.end(),
        [](const std::pair<flexql::ColumnValue, uint64_t>& a,
           const std::pair<flexql::ColumnValue, uint64_t>& b) {
            return compare_val(a.first, b.first) < 0;
        });

    im.index_manager_bulk_insert(table_name, pairs_);
}

void BulkIndexBuilder::reset() {
    pairs_.clear();
}

} // namespace index
} // namespace flexql
