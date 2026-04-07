#include "storage/wal.h"
#include "parser/parser.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace flexql {
namespace storage {

WAL::WAL(const std::string& wal_dir) {
    std::filesystem::create_directories(wal_dir);
    file_path_ = wal_dir + "/wal.log";
}

WAL::~WAL() {
    close();
}

bool WAL::open() {
    fd_ = ::open(file_path_.c_str(), O_RDWR | O_CREAT, 0666);
    return fd_ >= 0;
}

void WAL::close() {
    if (fd_ >= 0) {
        fsync_safely();
        ::close(fd_);
        fd_ = -1;
    }
}

void WAL::fsync_safely() {
    if (fd_ >= 0) {
        fsync(fd_);
    }
}

uint64_t WAL::append_record(const WALRecord& record) {
    if (fd_ < 0) return 0;
    
    // Seek to end
    off_t file_size = lseek(fd_, 0, SEEK_END);
    
    // Serialize to buffer
    std::vector<uint8_t> buf;
    auto append_val = [&buf](const auto* ptr, size_t size) {
        const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(ptr);
        buf.insert(buf.end(), byte_ptr, byte_ptr + size);
    };
    
    append_val(&record.record_id, sizeof(record.record_id));
    append_val(&record.operation_type, sizeof(record.operation_type));
    append_val(record.table_name, sizeof(record.table_name));
    append_val(&record.payload_len, sizeof(record.payload_len));
    
    if (record.payload_len > 0) {
        append_val(record.payload.data(), record.payload_len);
    }
    
    uint64_t status_offset = file_size + buf.size();
    append_val(&record.committed_flag, sizeof(record.committed_flag));
    
    write(fd_, buf.data(), buf.size());
    
    return status_offset;
}

bool WAL::commit_record(uint64_t status_offset) {
    if (fd_ < 0) return false;
    
    uint8_t flag = 1;
    pwrite(fd_, &flag, 1, status_offset);
    // Disable fsync here for high throughput inserts. Handled by close().
    return true;
}

std::vector<WALRecord> WAL::recover() {
    std::vector<WALRecord> uncommitted;
    if (fd_ < 0) return uncommitted;
    
    lseek(fd_, 0, SEEK_SET);
    
    while (true) {
        WALRecord rec;
        if (read(fd_, &rec.record_id, sizeof(rec.record_id)) <= 0) break;
        read(fd_, &rec.operation_type, sizeof(rec.operation_type));
        read(fd_, rec.table_name, sizeof(rec.table_name));
        read(fd_, &rec.payload_len, sizeof(rec.payload_len));
        
        rec.payload.resize(rec.payload_len);
        if (rec.payload_len > 0) {
            read(fd_, rec.payload.data(), rec.payload_len);
        }
        
        read(fd_, &rec.committed_flag, sizeof(rec.committed_flag));
        
        if (rec.committed_flag == 0) {
            uncommitted.push_back(rec);
        }
    }
    return uncommitted;
}

uint64_t WAL::append_batch_record(const std::string& table_name,
                                   const std::vector<flexql::Row>& rows,
                                   const std::shared_ptr<flexql::storage::Schema>& schema) {
    if (fd_ < 0) return 0;

    // Write a lightweight marker only — row data is already durable via storage flush.
    // This avoids serializing potentially millions of rows into the WAL.
    WALRecord record;
    record.record_id = next_id_++;
    record.operation_type = WALOpType::BATCH_INSERT;
    std::memset(record.table_name, 0, sizeof(record.table_name));
    std::strncpy(record.table_name, table_name.c_str(), sizeof(record.table_name) - 1);

    // Payload: just the row count (4 bytes) — enough for recovery metadata
    uint32_t row_count = static_cast<uint32_t>(rows.size());
    record.payload.resize(4);
    for (int k = 0; k < 4; ++k) record.payload[k] = (row_count >> (k * 8)) & 0xFF;
    record.payload_len = 4;
    record.committed_flag = 0;

    return append_record(record);
}

} // namespace storage
} // namespace flexql
