#include "network/protocol.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>

namespace flexql {
namespace network {

// Helper to write exactly 'len' bytes
static ssize_t write_all(int fd, const void* buf, size_t len) {
    size_t total_written = 0;
    const char* ptr = static_cast<const char*>(buf);
    while (total_written < len) {
        ssize_t w = write(fd, ptr + total_written, len - total_written);
        if (w <= 0) {
            return -1; // Error or disconnect
        }
        total_written += w;
    }
    return total_written;
}

// Helper to read exactly 'len' bytes
static ssize_t read_all(int fd, void* buf, size_t len) {
    size_t total_read = 0;
    char* ptr = static_cast<char*>(buf);
    while (total_read < len) {
        ssize_t r = read(fd, ptr + total_read, len - total_read);
        if (r <= 0) {
            return -1; // Error or EOF
        }
        total_read += r;
    }
    return total_read;
}

int net_send_frame(int fd, const std::vector<uint8_t>& data) {
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    if (write_all(fd, &len, sizeof(len)) < 0) return -1;
    if (!data.empty()) {
        if (write_all(fd, data.data(), data.size()) < 0) return -1;
    }
    return 0;
}

int net_send_string_frame(int fd, const std::string& data) {
    uint32_t len = htonl(static_cast<uint32_t>(data.size()));
    if (write_all(fd, &len, sizeof(len)) < 0) return -1;
    if (!data.empty()) {
        if (write_all(fd, data.data(), data.size()) < 0) return -1;
    }
    return 0;
}

int net_recv_frame(int fd, std::vector<uint8_t>& out_data) {
    uint32_t len_be;
    if (read_all(fd, &len_be, sizeof(len_be)) < 0) return -1;
    uint32_t len = ntohl(len_be);
    
    out_data.resize(len);
    if (len > 0) {
        if (read_all(fd, out_data.data(), len) < 0) return -1;
    }
    return 0;
}

int net_recv_string_frame(int fd, std::string& out_string) {
    uint32_t len_be;
    if (read_all(fd, &len_be, sizeof(len_be)) < 0) return -1;
    uint32_t len = ntohl(len_be);
    
    out_string.resize(len);
    if (len > 0) {
        if (read_all(fd, out_string.data(), len) < 0) return -1;
    }
    return 0;
}

} // namespace network
} // namespace flexql
