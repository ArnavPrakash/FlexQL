#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace flexql {
namespace storage {

enum class WALOpType : uint8_t {
    INSERT = 1,
    CREATE_TABLE = 2
};

struct WALRecord {
    uint64_t record_id;
    WALOpType operation_type;
    char table_name[64];
    uint32_t payload_len;
    std::vector<uint8_t> payload;
    uint8_t committed_flag;
};

class WAL {
public:
    explicit WAL(const std::string& wal_dir);
    ~WAL();
    
    bool open();
    void close();
    
    // Returns disk offset pointing to the committed_flag
    uint64_t append_record(const WALRecord& record);
    
    // Commits a record via its disk offset computed during append
    bool commit_record(uint64_t status_offset);
    
    // Recovers all uncommitted records
    std::vector<WALRecord> recover();

private:
    std::string file_path_;
    int fd_ = -1;
    uint64_t next_id_ = 1;

    void fsync_safely();
};

} // namespace storage
} // namespace flexql
