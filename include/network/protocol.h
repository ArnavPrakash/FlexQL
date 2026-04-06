#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace flexql {
namespace network {

// Send length-prefixed frame (4-byte BE length + data)
// Returns 0 on success, < 0 on error
int net_send_frame(int fd, const std::vector<uint8_t>& data);
int net_send_string_frame(int fd, const std::string& data);

// Receive length-prefixed frame
// Returns 0 on success and populates out_data, < 0 on error or disconnect
int net_recv_frame(int fd, std::vector<uint8_t>& out_data);
int net_recv_string_frame(int fd, std::string& out_string);

} // namespace network
} // namespace flexql
