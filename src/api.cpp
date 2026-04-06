#include "common/flexql.h"
#include "network/client_conn.h"
#include "network/protocol.h"
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <iostream>

struct FlexQL {
    int fd;
};

extern "C" int flexql_open(const char* host, int port, FlexQL** db) {
    int fd = flexql::network::net_client_connect(host, port);
    if (fd < 0) return FLEXQL_ERROR;
    
    *db = new FlexQL{fd};
    return FLEXQL_OK;
}

extern "C" int flexql_close(FlexQL* db) {
    if (!db) return FLEXQL_OK;
    flexql::network::net_client_close(db->fd);
    delete db;
    return FLEXQL_OK;
}

extern "C" void flexql_free(void* ptr) {
    free(ptr);
}

extern "C" int flexql_exec(FlexQL* db, const char* sql, 
                           int (*callback)(void*, int, char**, char**), 
                           void* arg, char** errmsg) 
{
    if (!db) return FLEXQL_ERROR;
    
    std::string sql_str(sql);
    if (flexql::network::net_send_string_frame(db->fd, sql_str) < 0) {
        if (errmsg) *errmsg = strdup("Failed to send query");
        return FLEXQL_ERROR;
    }
    
    std::string response;
    if (flexql::network::net_recv_string_frame(db->fd, response) < 0) {
        if (errmsg) *errmsg = strdup("Failed to receive response");
        return FLEXQL_ERROR;
    }
    
    if (response.substr(0, 6) == "ERROR:") {
        if (errmsg) *errmsg = strdup(response.substr(7).c_str());
        return FLEXQL_ERROR;
    }
    
    if (response == "CREATE TABLE OK" || response == "INSERT OK") {
        return FLEXQL_OK;
    }
    
    // Parse COLS and ROWS
    std::istringstream stream(response);
    std::string line;
    
    if (!std::getline(stream, line) || line.substr(0, 6) != "COLS: ") {
        return FLEXQL_OK; // Or ERROR depending on strictness
    }
    
    int num_cols = std::stoi(line.substr(6));
    
    std::vector<std::string> col_names(num_cols);
    std::getline(stream, line);
    std::istringstream col_stream(line);
    for (int i = 0; i < num_cols; i++) {
        std::string col;
        std::getline(col_stream, col, '\t');
        col_names[i] = col;
    }
    
    std::getline(stream, line);
    if (line.substr(0, 6) != "ROWS: ") return FLEXQL_OK;
    
    int num_rows = std::stoi(line.substr(6));
    
    // Convert col_names to char**
    std::vector<char*> azColName(num_cols);
    for (int i = 0; i < num_cols; i++) azColName[i] = const_cast<char*>(col_names[i].c_str());
    
    for (int r = 0; r < num_rows; r++) {
        std::getline(stream, line);
        std::istringstream row_stream(line);
        std::vector<std::string> row_vals(num_cols);
        std::vector<char*> argv(num_cols);
        
        for (int i = 0; i < num_cols; i++) {
            std::string val;
            std::getline(row_stream, val, '\t');
            row_vals[i] = val;
            argv[i] = const_cast<char*>(row_vals[i].c_str());
        }
        
        if (callback) {
            int ret = callback(arg, num_cols, argv.data(), azColName.data());
            if (ret != 0) break;
        }
    }
    
    return FLEXQL_OK;
}
