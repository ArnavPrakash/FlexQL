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
    fd_ = ::open(file_path_.c_str(), O_RDWR | O_CREAT, 0666);
    if (fd_ < 0) return false;
    
    load_pages();
    return true;
}

void StorageEngine::close() {
    if (fd_ >= 0) {
        for (size_t i = 0; i < pages_.size(); ++i) {
            flush_page(i);
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
    std::vector<uint8_t> buf;
    for (size_t i = 0; i < schema_->columns.size(); ++i) {
        if (schema_->columns[i].type == flexql::ColumnType::INT) {
            int64_t val = std::get<flexql::IntValue>(row.values[i]);
            uint8_t vbuf[8];
            // Little endian representation
            for (int k = 0; k < 8; ++k) {
                vbuf[k] = (val >> (k * 8)) & 0xFF;
            }
            buf.insert(buf.end(), vbuf, vbuf + 8);
        } else {
            const std::string& val = std::get<flexql::TextValue>(row.values[i]);
            uint32_t len = val.size();
            uint8_t lbuf[4];
            for (int k = 0; k < 4; ++k) {
                lbuf[k] = (len >> (k * 8)) & 0xFF;
            }
            buf.insert(buf.end(), lbuf, lbuf + 4);
            buf.insert(buf.end(), val.begin(), val.end());
        }
    }
    return buf;
}

Row StorageEngine::deserialize_row(const std::vector<uint8_t>& data) {
    Row row;
    size_t offset = 0;
    for (size_t i = 0; i < schema_->columns.size(); ++i) {
        if (schema_->columns[i].type == flexql::ColumnType::INT) {
            int64_t val = 0;
            for (int k = 0; k < 8; ++k) {
                val |= static_cast<int64_t>(data[offset + k]) << (k * 8);
            }
            offset += 8;
            row.values.push_back(val);
        } else {
            uint32_t len = 0;
            for (int k = 0; k < 4; ++k) {
                len |= static_cast<uint32_t>(data[offset + k]) << (k * 8);
            }
            offset += 4;
            std::string str(reinterpret_cast<const char*>(&data[offset]), len);
            offset += len;
            row.values.push_back(str);
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
