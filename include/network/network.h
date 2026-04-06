#pragma once

#include <string>

namespace flexql {
namespace network {

// Forward declare to simplify server logic later
struct ServerContext {
    int server_fd;
};

// Initialize TCP server socket on given port
int net_server_init(int port);

// Blocking accept, returns client fd or -1 on error
int net_server_accept(int server_fd);

// Close socket fd
void net_close(int fd);

} // namespace network
} // namespace flexql
