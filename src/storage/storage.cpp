#include "storage/storage.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

namespace flexql {
namespace storage {

StorageEngine::StorageEngine(const std::string& table_name, const std::string& data_dir, std::shared_ptr<Schema> schema)
    : file_path_(data_dir + "/tables/" + table_name + ".dat"), schema_(schema) {
}

StorageEngine::~StorageEngine() {
    close();
}

bool StorageEngine::open() {
    // O_APPEND ensures all writes go to end of file sequentially — no seek needed
    fd_ = ::open(file_path_.c_str(), O_RDWR | O_CREAT | O_APPEND, 0666);
    if (fd_ < 0) return false;
    
    load_pages();
    return true;
}

void StorageEngine::close() {
    if (fd_ >= 0) {
        // Use pwrite for close-time flush — pages may already exist on disk
        for (size_t i = 0; i < pages_.size(); ++i) {
            pwrite(fd_, pages_[i].get(), PAGE_SIZE, pages_[i]->page_id * PAGE_SIZE);
        }
        ::close(fd_);
        fd_ = -1;
    }
}

void StorageEngine::load_pages() {
    pages_.clear();
    struct stat st;
    if (fstat(fd_, &st) == 0 && st.st_size > 0) {
        size_t num_pages = st.st_size / PAGE_SIZE;
        for (size_t i = 0; i < num_pages; ++i) {
            auto p = std::make_unique<Page>();
            pread(fd_, p.get(), PAGE_SIZE, i * PAGE_SIZE);
            pages_.push_back(std::move(p));
        }
    }
}

void StorageEngine::flush_page(size_t page_idx) {
    if (fd_ >= 0 && page_idx < pages_.size()) {
        pwrite(fd_, pages_[page_idx].get(), PAGE_SIZE, pages_[page_idx]->page_id * PAGE_SIZE);
    }
}
std::vector<uint8_t> StorageEngine::serialize_row(const Row& row) {
    // Pre-compute size to avoid reallocations
    size_t total = 0;
    for (size_t i = 0; i < schema_->columns.size(); ++i) {
        if (schema_->columns[i].type == flexql::ColumnType::INT) {
            total += 8;
        } else {
            total += 4 + std::get<flexql::TextValue>(row.values[i]).size();
        }
    }

    std::vector<uint8_t> buf(total);
    uint8_t* ptr = buf.data();

    for (size_t i = 0; i < schema_->columns.size(); ++i) {
        if (schema_->columns[i].type == flexql::ColumnType::INT) {
            int64_t val = std::get<flexql::IntValue>(row.values[i]);
            std::memcpy(ptr, &val, 8);
            ptr += 8;
        } else {
            const std::string& val = std::get<flexql::TextValue>(row.values[i]);
            uint32_t len = static_cast<uint32_t>(val.size());
            std::memcpy(ptr, &len, 4);
            ptr += 4;
            std::memcpy(ptr, val.data(), len);
            ptr += len;
        }
    }
    return buf;
}

Row StorageEngine::deserialize_row(const std::vector<uint8_t>& data) {
    Row row;
    const uint8_t* ptr = data.data();
    for (size_t i = 0; i < schema_->columns.size(); ++i) {
        if (schema_->columns[i].type == flexql::ColumnType::INT) {
            int64_t val;
            std::memcpy(&val, ptr, 8);
            ptr += 8;
            row.values.push_back(val);
        } else {
            uint32_t len;
            std::memcpy(&len, ptr, 4);
            ptr += 4;
            std::string str(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
            row.values.push_back(std::move(str));
        }
    }
    return row;
}

uint64_t StorageEngine::insert_row(const Row& row) {
    std::vector<uint8_t> rdata = serialize_row(row);
    
    if (pages_.empty() || !page_has_space(pages_.back().get(), rdata.size())) {
        uint32_t new_id = pages_.size();
        pages_.push_back(page_alloc(new_id));
    }
    
    Page* p = pages_.back().get();
    uint16_t offset_in_page = p->free_offset;
    page_write_row(p, rdata);
    
    
    // We can let flush() or close() handle writing the page cache to OS buffering. 
    
    // disk offset is (page_id * PAGE_SIZE) + offset_in_page
    return static_cast<uint64_t>(p->page_id) * PAGE_SIZE + offset_in_page;
}

std::vector<uint64_t> StorageEngine::bulk_insert_rows(const std::vector<flexql::Row>& rows) {
    std::vector<uint64_t> offsets;
    offsets.reserve(rows.size());

    size_t first_dirty_idx = pages_.size();

    // Pre-reserve page vector capacity to avoid reallocation during bulk insert
    // Each page holds PAGE_SIZE - PAGE_HEADER_SIZE bytes; estimate conservatively
    if (!rows.empty()) {
        // Rough estimate: serialize first row to get avg size, then compute page count
        auto sample = serialize_row(rows[0]);
        size_t row_slot_size = sample.size() + 2; // +2 for length prefix
        size_t rows_per_page = (PAGE_SIZE - PAGE_HEADER_SIZE) / (row_slot_size > 0 ? row_slot_size : 1);
        if (rows_per_page == 0) rows_per_page = 1;
        size_t estimated_pages = (rows.size() + rows_per_page - 1) / rows_per_page;
        pages_.reserve(first_dirty_idx + estimated_pages + 1);
    }

    for (const auto& row : rows) {
        std::vector<uint8_t> rdata = serialize_row(row);

        if (pages_.empty() || !page_has_space(pages_.back().get(), rdata.size())) {
            uint32_t new_id = pages_.size();
            pages_.push_back(page_alloc(new_id));
        }

        Page* p = pages_.back().get();
        uint16_t offset_in_page = p->free_offset;
        page_write_row(p, rdata);

        offsets.push_back(static_cast<uint64_t>(p->page_id) * PAGE_SIZE + offset_in_page);
    }

    // Flush all dirty pages in a single sequential write
    if (first_dirty_idx < pages_.size()) {
        size_t dirty_count = pages_.size() - first_dirty_idx;
        // Build a contiguous buffer of all dirty pages and write in one syscall
        std::vector<uint8_t> write_buf(dirty_count * PAGE_SIZE);
        for (size_t i = 0; i < dirty_count; ++i) {
            std::memcpy(write_buf.data() + i * PAGE_SIZE,
                        pages_[first_dirty_idx + i].get(), PAGE_SIZE);
        }
        // O_APPEND: write goes to end of file — pages are always appended in order
        ::write(fd_, write_buf.data(), write_buf.size());
    }

    return offsets;
}

void StorageEngine::scan(std::function<bool(const Row&)> callback) {
    for (const auto& p : pages_) {
        for (int i = 0; i < p->row_count; ++i) {
            std::vector<uint8_t> rdata;
            if (page_read_row(p.get(), i, rdata)) {
                if (!callback(deserialize_row(rdata))) return;
            }
        }
    }
}

bool StorageEngine::read_row_at_offset(uint64_t offset, Row& row_out) {
    uint32_t page_id = offset / PAGE_SIZE;
    uint32_t page_offset = offset % PAGE_SIZE;
    
    if (page_id >= pages_.size()) return false;
    
    Page* p = pages_[page_id].get();
    
    // Direct read using page_offset
    if (page_offset < PAGE_HEADER_SIZE || page_offset >= p->free_offset) return false;
    
    const uint8_t* ptr = p->data + (page_offset - PAGE_HEADER_SIZE);
    uint16_t len;
    std::memcpy(&len, ptr, 2);
    
    std::vector<uint8_t> rdata(len);
    std::memcpy(rdata.data(), ptr + 2, len);
    
    row_out = deserialize_row(rdata);
    return true;
}

std::unique_ptr<StorageEngine> storage_open(const std::string& table_name, const std::string& data_dir, std::shared_ptr<Schema> schema) {
    auto engine = std::make_unique<StorageEngine>(table_name, data_dir, schema);
    if (!engine->open()) return nullptr;
    return engine;
}

} // namespace storage
} // namespace flexql
