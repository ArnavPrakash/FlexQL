#pragma once

#include <string>

namespace flexql {
namespace network {

// Connect to the TCP server at target host and port
// Returns client_fd, or -1 on error
int net_client_connect(const std::string& host, int port);

// Close client connection socket
void net_client_close(int fd);

} // namespace network
} // namespace flexql
