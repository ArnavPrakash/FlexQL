#include <iostream>
#include <string>
#include "network/network.h"
#include "network/client_conn.h"
#include "network/protocol.h"

using namespace flexql;

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 5432;
    
    int fd = network::net_client_connect(host, port);
    if (fd < 0) {
        std::cerr << "Failed to connect to " << host << ":" << port << "\n";
        return 1;
    }
    
    std::cout << "Connected to FlexQL. Type your SQL queries.\n";
    
    std::string sql;
    while (true) {
        std::cout << "flexql> ";
        if (!std::getline(std::cin, sql)) break;
        if (sql.empty()) continue;
        if (sql == "exit" || sql == "quit") break;
        
        if (network::net_send_string_frame(fd, sql) < 0) {
            std::cerr << "Disconnected from server\n";
            break;
        }
        
        std::string response;
        if (network::net_recv_string_frame(fd, response) < 0) {
            std::cerr << "Disconnected from server\n";
            break;
        }
        
        std::cout << response << "\n";
    }
    
    network::net_client_close(fd);
    return 0;
}
