cmake_minimum_required(VERSION 3.14)
project(FlexQL VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release if no build type specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Always compile with -O3
add_compile_options(-O3)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Threading
find_package(Threads REQUIRED)

# Common sources
set(COMMON_SOURCES
    src/network/network.cpp
    src/network/protocol.cpp
    src/network/client_conn.cpp
    src/parser/lexer.cpp
    src/parser/parser.cpp
    src/parser/printer.cpp
)

# Server executable target
# set(SERVER_SOURCES src/server/server.cpp src/server/config.cpp src/server/logger.cpp)
# add_executable(flexql_server ${SERVER_SOURCES} ${COMMON_SOURCES})
# target_link_libraries(flexql_server Threads::Threads)

# Client executable target
# set(CLIENT_SOURCES src/client/main.cpp src/client/repl.cpp src/client/flexql_api.cpp)
# add_executable(flex_client ${CLIENT_SOURCES} src/network/client_conn.cpp src/network/protocol.cpp)
# target_link_libraries(flex_client Threads::Threads)

# Testing (Using Google Test via FetchContent)
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

FetchContent_Declare(
  rapidcheck
  GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
  GIT_TAG        master
)
FetchContent_MakeAvailable(rapidcheck)

enable_testing()

add_executable(test_schema tests/test_schema.cpp src/storage/schema.cpp)
target_link_libraries(test_schema gtest gtest_main Threads::Threads)
add_test(NAME SchemaTest COMMAND test_schema)

add_executable(test_storage tests/test_storage.cpp src/storage/page.cpp src/storage/storage.cpp src/storage/schema.cpp)
target_link_libraries(test_storage gtest gtest_main rapidcheck Threads::Threads)
add_test(NAME StorageTest COMMAND test_storage)

add_executable(test_btree tests/test_btree.cpp src/index/btree.cpp src/index/index_manager.cpp src/index/bulk_index_builder.cpp src/storage/page.cpp src/storage/storage.cpp src/storage/schema.cpp)
target_link_libraries(test_btree gtest gtest_main rapidcheck Threads::Threads)
add_test(NAME BTreeTest COMMAND test_btree)

add_executable(test_lru_cache tests/test_lru_cache.cpp src/cache/lru_cache.cpp)
target_link_libraries(test_lru_cache gtest gtest_main Threads::Threads)
add_test(NAME LRUCacheTest COMMAND test_lru_cache)

add_executable(test_wal tests/test_wal.cpp src/storage/wal.cpp)
target_link_libraries(test_wal gtest gtest_main rapidcheck Threads::Threads)
add_test(NAME WALTest COMMAND test_wal)

add_executable(test_concurrency tests/test_concurrency.cpp src/concurrency/concurrency.cpp src/concurrency/thread_pool.cpp)
target_link_libraries(test_concurrency gtest gtest_main Threads::Threads)
add_test(NAME ConcurrencyTest COMMAND test_concurrency)

add_executable(test_executor tests/test_executor.cpp src/query/executor.cpp src/query/database_manager.cpp src/parser/lexer.cpp src/parser/parser.cpp src/storage/storage.cpp src/storage/page.cpp src/storage/schema.cpp src/storage/wal.cpp src/index/btree.cpp src/index/index_manager.cpp src/index/bulk_index_builder.cpp src/concurrency/concurrency.cpp src/concurrency/thread_pool.cpp src/cache/lru_cache.cpp)
target_link_libraries(test_executor gtest gtest_main rapidcheck Threads::Threads)
add_test(NAME ExecutorTest COMMAND test_executor)

add_executable(flexql_server src/main.cpp src/network/network.cpp src/network/protocol.cpp src/query/executor.cpp src/query/database_manager.cpp src/parser/lexer.cpp src/parser/parser.cpp src/storage/storage.cpp src/storage/page.cpp src/storage/schema.cpp src/storage/wal.cpp src/index/btree.cpp src/index/index_manager.cpp src/index/bulk_index_builder.cpp src/concurrency/concurrency.cpp src/concurrency/thread_pool.cpp src/cache/lru_cache.cpp)
target_link_libraries(flexql_server Threads::Threads)

add_executable(flexql_client src/client.cpp src/network/client_conn.cpp src/network/protocol.cpp)
target_link_libraries(flexql_client Threads::Threads)

add_executable(benchmark_flexql benchmark_flexql.cpp src/api.cpp src/network/client_conn.cpp src/network/protocol.cpp)
target_link_libraries(benchmark_flexql Threads::Threads)

add_executable(test_network tests/test_network.cpp src/network/protocol.cpp)
target_link_libraries(test_network gtest gtest_main Threads::Threads)
add_test(NAME NetworkTest COMMAND test_network)

add_executable(test_parser tests/test_parser.cpp src/parser/lexer.cpp src/parser/parser.cpp src/parser/printer.cpp)
target_link_libraries(test_parser gtest gtest_main Threads::Threads)
add_test(NAME ParserTest COMMAND test_parser)


