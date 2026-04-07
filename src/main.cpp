#include <iostream>
#include <string>
#include "network/network.h"
#include "network/protocol.h"
#include "concurrency/concurrency.h"
#include "query/executor.h"

using namespace flexql;

int main(int argc, char** argv) {
    std::string data_dir = "data";
    int port = 9000;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    auto sm = std::make_shared<SchemaManager>(data_dir);
    auto wal = std::make_shared<storage::WAL>(data_dir + "/wal");
    wal->open();
    auto idx = std::make_shared<index::IndexManager>(data_dir);
    auto lru = std::make_shared<cache::LRUCache>(1000);
    auto mtx = std::make_shared<concurrency::ConcurrencyManager>();
    
    auto executor = std::make_shared<query::QueryExecutor>(data_dir, sm, wal, idx, lru, mtx);
    
    concurrency::ThreadPool pool(4);
    
    std::cout << "FlexQL Server starting on port " << port << "...\n";
    int server_fd = network::net_server_init(port);
    
    if (server_fd < 0) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    
    while (true) {
        int client_fd = network::net_server_accept(server_fd);
        if (client_fd < 0) continue;
        
        pool.submit([client_fd, executor]() {
            std::string errmsg;
            std::string payload;
            query::ClientSession session;   // per-connection state

            while (true) {
                if (network::net_recv_string_frame(client_fd, payload) < 0) {
                    break;
                }
                
                auto tokens = parser::lexer_tokenize(payload);
                auto ast = parser::parser_parse(tokens, errmsg);
                
                std::string response;
                
                if (!ast) {
                    response = "ERROR: " + errmsg;
                } else {
                    flexql::ResultSet res;
                    ErrorCode code = executor->executor_run(*ast, payload, res, errmsg, session);
                    
                    if (code != ErrorCode::OK) {
                        response = "ERROR: " + errmsg;
                    } else {
                        if (ast->type == parser::ASTNodeType::CREATE_TABLE) {
                            response = "CREATE TABLE OK";
                        } else if (ast->type == parser::ASTNodeType::CREATE_DATABASE) {
                            response = "CREATE DATABASE OK";
                        } else if (ast->type == parser::ASTNodeType::INSERT || ast->type == parser::ASTNodeType::BATCH_INSERT) {
                            response = "INSERT OK";
                        } else if (ast->type == parser::ASTNodeType::USE_DATABASE) {
                            response = "Database changed";
                        } else {
                            response = "COLS: " + std::to_string(res.column_names.size()) + "\n";
                            for (const auto& c : res.column_names) {
                                response += c + "\t";
                            }
                            response += "\nROWS: " + std::to_string(res.rows.size()) + "\n";
                            for (const auto& r : res.rows) {
                                for (const auto& v : r.values) {
                                    if (std::holds_alternative<IntValue>(v)) {
                                        response += std::to_string(std::get<IntValue>(v)) + "\t";
                                    } else {
                                        response += std::get<TextValue>(v) + "\t";
                                    }
                                }
                                response += "\n";
                            }
                        }
                    }
                }
                
                network::net_send_string_frame(client_fd, response);
            }
            network::net_close(client_fd);
        });
    }
    
    return 0;
}
