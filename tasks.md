# Implementation Plan

## Phase 0: Project Scaffolding

- [ ] 0.1 Create the top-level directory structure: `bin/`, `build/`, `config/`, `scripts/`, `include/`, `src/`, `tests/`, `data/tables/`, `data/indexes/`, `data/wal/`
- [ ] 0.2 Create `CMakeLists.txt` (or `Makefile`) at the project root with targets for `flexql_server`, `flexql_client`, and a test runner
- [ ] 0.3 Create `config/server.conf` with default values for port, thread pool size, LRU cache max entries, lock timeout, and log file path
- [ ] 0.4 Create `.gitignore` excluding `bin/`, `build/`, `data/`, and generated object files
- [ ] 0.5 Create `include/common/flexql_types.h` defining shared types: `FlexQL`, `ColumnType` (INT/TEXT), `Row`, `ResultSet`, `ASTNode`, `ErrorCode`
- [ ] 0.6 Create `include/common/flexql.h` with the public API declarations: `flexql_open`, `flexql_close`, `flexql_exec`, `flexql_free`, `FLEXQL_OK`, `FLEXQL_ERROR`

---

## Phase 1: Network Layer

- [ ] 1.1 Create `include/network/network.h` declaring the server-side socket API: `net_server_init`, `net_server_accept`, `net_send_frame`, `net_recv_frame`, `net_close`
- [ ] 1.2 Create `include/network/client_conn.h` declaring the client-side socket API: `net_client_connect`, `net_client_send_frame`, `net_client_recv_frame`, `net_client_close`
- [ ] 1.3 Implement `src/network/network.c` — server TCP socket: bind, listen, accept loop; each accepted fd passed to thread pool
  - [ ] 1.3.1 Implement `net_server_init(port)` — creates, binds, and listens on a TCP socket
  - [ ] 1.3.2 Implement `net_server_accept(server_fd)` — blocking accept returning client fd
  - [ ] 1.3.3 Implement `net_close(fd)` — closes a socket fd
- [ ] 1.4 Implement `src/network/protocol.c` — length-prefixed framing
  - [ ] 1.4.1 Implement `net_send_frame(fd, data, len)` — writes 4-byte big-endian length then payload; handles partial writes
  - [ ] 1.4.2 Implement `net_recv_frame(fd, buf, max_len)` — reads 4-byte length then exactly that many bytes; handles partial reads
- [ ] 1.5 Implement `src/network/client_conn.c` — client-side TCP connect, send, recv using the same framing as 1.4
- [ ] 1.6 Write unit tests in `tests/test_network.c` verifying:
  - [ ] 1.6.1 A frame sent by `net_send_frame` is received intact by `net_recv_frame` (loopback)
  - [ ] 1.6.2 Partial reads are buffered and reassembled correctly
  - [ ] 1.6.3 Sending a zero-length payload is handled without error

---

## Phase 2: SQL Parser

- [ ] 2.1 Create `include/parser/lexer.h` declaring token types and `lexer_tokenize(sql, tokens, max_tokens)`
- [ ] 2.2 Create `include/parser/parser.h` declaring `parser_parse(tokens, token_count, ast_out)` and AST node structures
- [ ] 2.3 Create `include/parser/printer.h` declaring `ast_print(ast, buf, buf_len)` (pretty printer)
- [ ] 2.4 Implement `src/parser/lexer.c`
  - [ ] 2.4.1 Tokenize SQL keywords: SELECT, FROM, WHERE, INSERT, INTO, VALUES, CREATE, TABLE, INNER, JOIN, ON (case-insensitive)
  - [ ] 2.4.2 Tokenize identifiers, integer literals, and single-quoted TEXT literals
  - [ ] 2.4.3 Tokenize punctuation: `(`, `)`, `,`, `=`, `.`, `;`, `*`
  - [ ] 2.4.4 Skip whitespace between tokens
  - [ ] 2.4.5 Return a LEX_ERROR token with position info for unrecognized characters
  - [ ] 2.4.6 Tokenize `DECIMAL` keyword and `VARCHAR` keyword with optional `(n)` parameter
  - [ ] 2.4.7 Tokenize `>` as a comparison operator token (in addition to `=`)
- [ ] 2.5 Implement `src/parser/parser.c`
  - [ ] 2.5.1 Parse `CREATE TABLE name (col TYPE, ...)` → AST CREATE_TABLE node
  - [ ] 2.5.2 Parse `INSERT INTO name VALUES (v1, v2, ...)` → AST INSERT node
  - [ ] 2.5.3 Parse `SELECT * FROM name` → AST SELECT node (no WHERE)
  - [ ] 2.5.4 Parse `SELECT * FROM name WHERE col = value` → AST SELECT node with WHERE condition
  - [ ] 2.5.5 Parse `SELECT * FROM tableA INNER JOIN tableB ON tableA.col = tableB.col` → AST SELECT_JOIN node
  - [ ] 2.5.6 Parse `SELECT * FROM tableA INNER JOIN tableB ON tableA.col = tableB.col WHERE col = value` → AST SELECT_JOIN node with WHERE
  - [ ] 2.5.7 Return descriptive parse errors for unrecognized syntax
  - [ ] 2.5.8 Return parse error for column types other than INT, TEXT, DECIMAL, or VARCHAR(n)
  - [ ] 2.5.9 Parse `SELECT col1, col2, ... FROM table` → AST SELECT node with named Column_Projection list
  - [ ] 2.5.10 Parse `SELECT table.col1, table.col2 FROM tableA INNER JOIN tableB ON ...` → AST SELECT_JOIN node with qualified Column_Projection
  - [ ] 2.5.11 Parse WHERE clause with `>` operator → AST WHERE condition node with operator field set to GT
  - [ ] 2.5.12 Parse `INSERT INTO table VALUES (...), (...), ...` multi-row syntax → AST BATCH_INSERT node
  - [ ] 2.5.13 Normalize `DECIMAL` column type to INT in CREATE_TABLE AST node
  - [ ] 2.5.14 Normalize `VARCHAR(n)` column type to TEXT in CREATE_TABLE AST node
- [ ] 2.6 Implement `src/parser/printer.c` — `ast_print` serializes any AST node back to canonical SQL string
- [ ] 2.7 Write unit tests in `tests/test_parser.c`
  - [ ] 2.7.1 Test each of the 6 supported statement forms parses to the correct AST node type and fields
  - [ ] 2.7.2 Test that invalid SQL returns a parse error with a non-empty message
  - [ ] 2.7.3 Test round-trip property: for each valid SQL string, `ast_print(parse(sql))` re-parses to an equivalent AST
  - [ ] 2.7.4 Test case-insensitivity of SQL keywords (e.g., `select * FROM` parses correctly)
  - [ ] 2.7.5 Test that unsupported column types (e.g., FLOAT) return a parse error
  - [ ] 2.7.6 Test `SELECT col1, col2 FROM t WHERE col > value` parses to SELECT node with named projection and GT condition
  - [ ] 2.7.7 Test `SELECT t1.col, t2.col FROM t1 INNER JOIN t2 ON t1.id = t2.id` parses to SELECT_JOIN with qualified projection
  - [ ] 2.7.8 Test multi-row INSERT parses to BATCH_INSERT node with correct tuple count
  - [ ] 2.7.9 Test `DECIMAL` and `VARCHAR(64)` in CREATE TABLE are normalized to INT and TEXT respectively

---

## Phase 3: Schema Manager

- [ ] 3.1 Create `include/storage/schema.h` declaring `Schema`, `ColumnDef`, and functions: `schema_create`, `schema_load`, `schema_save`, `schema_get`, `schema_free`
- [ ] 3.2 Implement `src/storage/schema.c`
  - [ ] 3.2.1 `schema_create(table_name, columns, col_count)` — allocates and initializes a Schema struct
  - [ ] 3.2.2 `schema_save(schema, data_dir)` — serializes schema to `data/tables/<table_name>.schema`
  - [ ] 3.2.3 `schema_load(table_name, data_dir)` — deserializes schema from file; returns NULL if not found
  - [ ] 3.2.4 `schema_get_column(schema, col_name)` — returns ColumnDef pointer or NULL
  - [ ] 3.2.5 `schema_free(schema)` — frees all allocated memory
  - [ ] 3.2.6 On Server startup, scan `data/tables/` for `*.schema` files and load all schemas into an in-memory registry
- [ ] 3.3 Write unit tests in `tests/test_schema.c`
  - [ ] 3.3.1 Test that a schema saved and reloaded produces identical column names and types
  - [ ] 3.3.2 Test that `schema_get_column` returns NULL for unknown column names
  - [ ] 3.3.3 Test that loading a non-existent schema returns NULL without crashing

---

## Phase 4: Storage Engine

- [ ] 4.1 Create `include/storage/storage.h` declaring `StorageEngine`, `Page`, and functions: `storage_open`, `storage_close`, `storage_insert_row`, `storage_scan`, `storage_read_row_at_offset`
- [ ] 4.2 Implement `src/storage/page.c` — fixed-size 4096-byte page management
  - [ ] 4.2.1 Define Page header: page_id (4 bytes), row_count (2 bytes), free_offset (2 bytes), flags (1 byte)
  - [ ] 4.2.2 `page_alloc()` — allocates a zeroed in-memory Page
  - [ ] 4.2.3 `page_has_space(page, row_size)` — returns 1 if the page has room for a row of given size
  - [ ] 4.2.4 `page_write_row(page, row_data, row_size)` — writes row bytes at free_offset, updates free_offset and row_count
  - [ ] 4.2.5 `page_read_row(page, slot_index, row_out)` — reads row at given slot index
- [ ] 4.3 Implement `src/storage/storage.c`
  - [ ] 4.3.1 `storage_open(table_name, data_dir)` — opens or creates `data/tables/<table_name>.dat`; loads existing pages into memory
  - [ ] 4.3.2 `storage_close(engine)` — flushes dirty pages to disk and frees resources
  - [ ] 4.3.3 `storage_insert_row(engine, row_data, row_size)` — appends row to current page (or new page if full); returns disk offset; writes page to disk
  - [ ] 4.3.4 `storage_scan(engine, callback, arg)` — iterates all pages and all rows, invoking callback per row
  - [ ] 4.3.5 `storage_read_row_at_offset(engine, offset, row_out)` — seeks to offset and reads one row (used by index lookups)
  - [ ] 4.3.6 Implement row serialization: INT as 8-byte little-endian int64; TEXT as 4-byte length prefix + UTF-8 bytes
  - [ ] 4.3.7 Implement row deserialization matching the serialization format
- [ ] 4.4 Write unit tests in `tests/test_storage.c`
  - [ ] 4.4.1 Test that a row inserted and then scanned returns the same column values
  - [ ] 4.4.2 Test that inserting rows across a page boundary (>4096 bytes) allocates a new page and all rows are retrievable
  - [ ] 4.4.3 Test that `storage_read_row_at_offset` returns the correct row for a known offset
  - [ ] 4.4.4 Test that closing and reopening a storage file returns all previously inserted rows

---

## Phase 5: B-Tree Index

- [ ] 5.1 Create `include/index/btree.h` declaring `BTree`, `BTreeNode`, and functions: `btree_create`, `btree_insert`, `btree_search`, `btree_save`, `btree_load`, `btree_free`
- [ ] 5.2 Implement `src/index/btree.c`
  - [ ] 5.2.1 Define BTreeNode: keys array, child pointers array, is_leaf flag, key_count, and for leaf nodes: value (disk offset) array
  - [ ] 5.2.2 Define B-Tree order (minimum degree t=50, so each node holds 2t-1=99 keys max)
  - [ ] 5.2.3 `btree_create()` — allocates an empty B-Tree with a single empty leaf root
  - [ ] 5.2.4 `btree_insert(tree, key, offset)` — inserts (key, offset) pair; splits nodes as needed to maintain B-Tree invariants
  - [ ] 5.2.5 `btree_search(tree, key, offset_out)` — returns 1 and sets offset_out if key found, returns 0 if not found
  - [ ] 5.2.6 `btree_save(tree, filepath)` — serializes the entire B-Tree to `data/indexes/<table_name>.idx` using BFS order
  - [ ] 5.2.7 `btree_load(filepath)` — deserializes B-Tree from file; returns NULL if file not found
  - [ ] 5.2.8 `btree_free(tree)` — recursively frees all nodes
- [ ] 5.3 Implement `src/index/index_manager.c`
  - [ ] 5.3.1 `index_manager_init(data_dir)` — loads all `*.idx` files from `data/indexes/`; rebuilds missing indexes from table scans
  - [ ] 5.3.2 `index_manager_insert(table_name, key, offset)` — delegates to the table's B-Tree
  - [ ] 5.3.3 `index_manager_lookup(table_name, key, offset_out)` — delegates to the table's B-Tree
  - [ ] 5.3.4 `index_manager_rebuild(table_name, storage_engine)` — scans table data and rebuilds B-Tree from scratch
- [ ] 5.4 Write unit tests in `tests/test_btree.c`
  - [ ] 5.4.1 Test that inserting N keys in random order and searching for each returns the correct offset
  - [ ] 5.4.2 Test that the B-Tree maintains sorted key order after 10,000 insertions
  - [ ] 5.4.3 Test that searching for a non-existent key returns 0
  - [ ] 5.4.4 Test round-trip: save B-Tree to file, load it back, search for all previously inserted keys
  - [ ] 5.4.5 Test node splitting: insert enough keys to force a root split and verify tree height increases correctly

---

## Phase 6: LRU Cache

- [ ] 6.1 Create `include/cache/lru_cache.h` declaring `LRUCache` and functions: `lru_create`, `lru_get`, `lru_put`, `lru_invalidate_table`, `lru_free`
- [ ] 6.2 Implement `src/cache/lru_cache.c`
  - [ ] 6.2.1 Implement LRU cache using a doubly-linked list (most-recently-used at head) and a hash map (SQL string → list node)
  - [ ] 6.2.2 `lru_create(max_entries)` — allocates LRU cache with given capacity
  - [ ] 6.2.3 `lru_get(cache, sql_key, result_out)` — returns 1 and copies result if key found; moves entry to head
  - [ ] 6.2.4 `lru_put(cache, sql_key, result_set)` — inserts entry at head; evicts tail entry if at capacity
  - [ ] 6.2.5 `lru_invalidate_table(cache, table_name)` — removes all entries whose SQL key contains the table name as a word
  - [ ] 6.2.6 `lru_free(cache)` — frees all entries and the cache structure
- [ ] 6.3 Write unit tests in `tests/test_lru_cache.c`
  - [ ] 6.3.1 Test that a result stored with `lru_put` is retrievable with `lru_get`
  - [ ] 6.3.2 Test that inserting beyond capacity evicts the least recently used entry
  - [ ] 6.3.3 Test that accessing an entry promotes it to most-recently-used (it is not evicted next)
  - [ ] 6.3.4 Test that `lru_invalidate_table` removes all entries referencing the given table and leaves others intact
  - [ ] 6.3.5 Test idempotence: calling `lru_invalidate_table` twice on the same table produces the same cache state as calling it once

---

## Phase 7: Write-Ahead Log (WAL)

- [ ] 7.1 Create `include/storage/wal.h` declaring `WAL`, `WALRecord`, and functions: `wal_open`, `wal_append`, `wal_commit`, `wal_recover`, `wal_close`
- [ ] 7.2 Implement `src/storage/wal.c`
  - [ ] 7.2.1 Define WALRecord: record_id (8 bytes), operation_type (1 byte: INSERT/CREATE), table_name (fixed 64 bytes), payload_len (4 bytes), payload (variable), committed_flag (1 byte)
  - [ ] 7.2.2 `wal_open(wal_dir)` — opens or creates `data/wal/wal.log` in append mode
  - [ ] 7.2.3 `wal_append(wal, record)` — writes WALRecord to file and calls fsync; returns error if fsync fails
  - [ ] 7.2.4 `wal_commit(wal, record_id)` — seeks to the record's committed_flag byte and sets it to 1; calls fsync
  - [ ] 7.2.5 `wal_recover(wal, storage_engine, schema_manager)` — scans WAL file; for each uncommitted record, replays the operation against the storage engine
  - [ ] 7.2.6 `wal_close(wal)` — flushes and closes the WAL file
- [ ] 7.3 Write unit tests in `tests/test_wal.c`
  - [ ] 7.3.1 Test that a WAL record written and fsynced is present after reopening the file
  - [ ] 7.3.2 Test that `wal_recover` replays uncommitted records and does not replay committed records
  - [ ] 7.3.3 Test that a simulated crash (file closed without commit) causes `wal_recover` to replay the pending record

---

## Phase 8: Concurrency Manager

- [ ] 8.1 Create `include/concurrency/concurrency.h` declaring `ConcurrencyManager` and functions: `concurrency_init`, `concurrency_read_lock`, `concurrency_read_unlock`, `concurrency_write_lock`, `concurrency_write_unlock`, `concurrency_global_lock`, `concurrency_global_unlock`, `concurrency_destroy`
- [ ] 8.2 Implement `src/concurrency/concurrency.c`
  - [ ] 8.2.1 Use `pthread_rwlock_t` per table, stored in a hash map keyed by table name
  - [ ] 8.2.2 Use a single `pthread_mutex_t` as the global lock for Schema_Manager and LRU_Cache access
  - [ ] 8.2.3 `concurrency_init()` — initializes the global mutex and empty per-table rwlock registry
  - [ ] 8.2.4 `concurrency_read_lock(mgr, table_name, timeout_ms)` — acquires read lock; returns timeout error if not acquired within timeout_ms
  - [ ] 8.2.5 `concurrency_write_lock(mgr, table_name, timeout_ms)` — acquires write lock; returns timeout error if not acquired within timeout_ms
  - [ ] 8.2.6 `concurrency_read_unlock` / `concurrency_write_unlock` — release the respective lock
  - [ ] 8.2.7 `concurrency_global_lock` / `concurrency_global_unlock` — acquire/release the global mutex
  - [ ] 8.2.8 `concurrency_destroy(mgr)` — destroys all rwlocks and the global mutex
- [ ] 8.3 Implement `src/concurrency/thread_pool.c`
  - [ ] 8.3.1 `thread_pool_create(size)` — spawns `size` worker threads waiting on a task queue
  - [ ] 8.3.2 `thread_pool_submit(pool, task_fn, arg)` — enqueues a task; worker thread picks it up and calls `task_fn(arg)`
  - [ ] 8.3.3 `thread_pool_destroy(pool)` — signals all workers to exit, joins all threads, frees resources
- [ ] 8.4 Write unit tests in `tests/test_concurrency.c`
  - [ ] 8.4.1 Test that multiple threads can hold read locks on the same table simultaneously
  - [ ] 8.4.2 Test that a write lock blocks until all read locks are released
  - [ ] 8.4.3 Test that the lock timeout returns an error when a write lock is held and a second write lock is requested
  - [ ] 8.4.4 Test that the thread pool executes all submitted tasks exactly once

---

## Phase 9: Query Executor

- [ ] 9.1 Create `include/query/executor.h` declaring `QueryExecutor` and `executor_run(executor, ast, result_out, errmsg_out)`
- [ ] 9.2 Implement `src/query/executor.c`
  - [ ] 9.2.1 `executor_run_create_table(ast)` — acquires write lock; checks table does not exist; calls schema_create + schema_save + storage_open + index_manager init; releases lock
  - [ ] 9.2.2 `executor_run_insert(ast)` — acquires write lock; validates column count and types; calls wal_append; calls storage_insert_row; calls index_manager_insert; calls wal_commit; invalidates LRU cache for table; releases lock
  - [ ] 9.2.3 `executor_run_select(ast)` — checks LRU cache first; acquires read lock; if WHERE on primary key uses index lookup + storage_read_row_at_offset, else calls storage_scan with filter; assembles ResultSet; stores in LRU cache; releases lock
  - [ ] 9.2.4 `executor_run_select_join(ast)` — acquires read locks on both tables; performs nested-loop INNER JOIN; applies optional WHERE filter; assembles ResultSet; releases locks
  - [ ] 9.2.5 Return descriptive error strings for: table not found, column not found, type mismatch, duplicate table
  - [ ] 9.2.6 `executor_run_select` — apply Column_Projection: after assembling rows, filter to only the requested columns in the specified order; if wildcard, return all columns
  - [ ] 9.2.7 `executor_run_select` — support `>` operator in WHERE: perform full table scan comparing numeric column value against threshold
  - [ ] 9.2.8 `executor_run_batch_insert` — iterate value tuples; validate each against schema; acquire write lock once; insert all rows and index entries; commit WAL once; invalidate LRU cache; release lock
  - [ ] 9.2.9 `executor_run_select_join` — resolve qualified column references (`table.column`) in Column_Projection to the correct joined table's column
  - [ ] 9.2.10 `executor_run_select_join` — apply WHERE filter with qualified column reference and `>` or `=` operator after join
- [ ] 9.3 Write unit tests in `tests/test_executor.c`
  - [ ] 9.3.1 Test CREATE TABLE creates schema and storage files
  - [ ] 9.3.2 Test INSERT followed by SELECT * returns the inserted row
  - [ ] 9.3.3 Test SELECT WHERE on primary key returns only the matching row
  - [ ] 9.3.4 Test SELECT WHERE on non-primary-key column performs full scan and returns correct rows
  - [ ] 9.3.5 Test INNER JOIN returns only rows satisfying the join condition
  - [ ] 9.3.6 Test INSERT with wrong column count returns an error
  - [ ] 9.3.7 Test SELECT on non-existent table returns an error
  - [ ] 9.3.8 Test that a second CREATE TABLE with the same name returns an error
  - [ ] 9.3.9 Test `SELECT NAME, BALANCE FROM t WHERE ID = 1` returns only the two named columns
  - [ ] 9.3.10 Test `SELECT NAME FROM t WHERE BALANCE > 1000` returns only rows where BALANCE > 1000
  - [ ] 9.3.11 Test `SELECT NAME FROM t WHERE BALANCE > 5000` returns empty result set when no rows match
  - [ ] 9.3.12 Test BATCH_INSERT of 5 tuples followed by SELECT * returns all 5 rows
  - [ ] 9.3.13 Test `SELECT t1.NAME, t2.AMOUNT FROM t1 JOIN t2 ON t1.ID = t2.USER_ID WHERE t2.AMOUNT > 900` returns correct filtered join result
  - [ ] 9.3.14 Test BATCH_INSERT with one invalid tuple returns error and inserts zero rows (atomicity)

---

## Phase 10: Server Main

- [ ] 10.1 Implement `src/server/config.c` — parse `config/server.conf` key=value format; expose `config_get_int`, `config_get_str`
- [ ] 10.2 Implement `src/server/logger.c` — thread-safe file logger; `log_info`, `log_error`, `log_debug` writing timestamped lines to configured log file
- [ ] 10.3 Implement `src/server/server.c` — main server entry point
  - [ ] 10.3.1 Parse config file path from argv
  - [ ] 10.3.2 Initialize all subsystems in order: WAL recovery → Schema_Manager → Storage_Engine → Index_Manager → LRU_Cache → Concurrency_Manager → Thread_Pool → Network_Layer
  - [ ] 10.3.3 Accept loop: call `net_server_accept`, submit connection handler to thread pool
  - [ ] 10.3.4 Connection handler: loop reading frames, parse SQL, run executor, send result frames or error frame, send end-of-result frame
  - [ ] 10.3.5 Register SIGINT/SIGTERM handlers: set a shutdown flag; accept loop checks flag; drain thread pool; flush WAL; exit 0
  - [ ] 10.3.6 Log each accepted connection (client IP:port), each query received, and each error

---

## Phase 11: Client Implementation

- [ ] 11.1 Implement `src/client/flexql_api.c` — the `flexql.h` API implementation
  - [ ] 11.1.1 `flexql_open` — calls `net_client_connect`; allocates FlexQL struct; returns FLEXQL_OK or FLEXQL_ERROR
  - [ ] 11.1.2 `flexql_close` — calls `net_client_close`; frees FlexQL struct; returns FLEXQL_OK
  - [ ] 11.1.3 `flexql_exec` — sends query frame; reads response frames in a loop; for each row frame invokes callback; stops if callback returns 1; on error frame sets *errmsg and returns FLEXQL_ERROR; on end-of-result frame returns FLEXQL_OK
  - [ ] 11.1.4 `flexql_free` — calls `free(ptr)`
- [ ] 11.2 Implement `src/client/repl.c` — the interactive REPL
  - [ ] 11.2.1 Print a welcome banner and prompt string (e.g., `flexql> `)
  - [ ] 11.2.2 Read a line from stdin using `fgets` or `readline`
  - [ ] 11.2.3 If input is `.exit`, call `flexql_close` and exit 0
  - [ ] 11.2.4 Call `flexql_exec` with a callback that prints column names on first row and values on each row
  - [ ] 11.2.5 If `flexql_exec` returns `FLEXQL_ERROR`, print the error message to stderr and continue the loop
  - [ ] 11.2.6 Handle EOF (Ctrl-D) the same as `.exit`
- [ ] 11.3 Implement `src/client/main.c` — parse host and port from argv; call `flexql_open`; on failure print error and exit 1; on success enter REPL loop
- [ ] 11.4 Write integration tests in `tests/test_client_server.c`
  - [ ] 11.4.1 Start a Server in a child process; connect a Client; send CREATE TABLE, INSERT, SELECT; verify results match expected rows
  - [ ] 11.4.2 Test `.exit` command closes the connection cleanly
  - [ ] 11.4.3 Test that a Client connecting to a non-listening port returns FLEXQL_ERROR from `flexql_open`

---

## Phase 12: End-to-End and Performance Tests

- [ ] 12.1 Write `tests/test_e2e.c` — full pipeline tests using the FlexQL API directly (no REPL)
  - [ ] 12.1.1 CREATE TABLE → INSERT 1000 rows → SELECT * → verify row count = 1000
  - [ ] 12.1.2 INSERT 1000 rows → SELECT WHERE primary_key = X → verify exactly 1 row returned
  - [ ] 12.1.3 CREATE TABLE A and B → INSERT rows → INNER JOIN → verify join result correctness
  - [ ] 12.1.4 Verify LRU cache hit: execute same SELECT twice; second call must not access storage (mock or counter)
  - [ ] 12.1.5 Verify WAL recovery: insert rows, simulate crash (kill server), restart server, SELECT and verify rows are present
- [ ] 12.2 Write `tests/test_performance.c` — performance benchmarks (not part of CI pass/fail, output metrics only)
  - [ ] 12.2.1 Benchmark: insert 10,000,000 rows sequentially; record total time; assert < 300 seconds
  - [ ] 12.2.2 Benchmark: SELECT WHERE primary_key on 10M-row table; record time; assert < 100ms
  - [ ] 12.2.3 Benchmark: full table scan SELECT on 10M-row table; record time; assert < 60 seconds
  - [ ] 12.2.4 Benchmark: measure resident memory after loading 10M rows; assert < 2 GB
- [ ] 12.3 Write `tests/test_concurrency_e2e.c` — concurrent client stress tests
  - [ ] 12.3.1 Spawn 10 threads each inserting 1000 rows concurrently; after all threads finish, SELECT * and verify total row count = 10,000
  - [ ] 12.3.2 Spawn 5 reader threads and 2 writer threads simultaneously; verify no data corruption or deadlock

---

## Phase 13: Scripts and Documentation

- [ ] 13.1 Create `scripts/build.sh` — runs CMake configure + build in `build/` and copies binaries to `bin/`
- [ ] 13.2 Create `scripts/run_server.sh` — starts the server with `config/server.conf`
- [ ] 13.3 Create `scripts/run_client.sh` — starts the client REPL connecting to localhost on the configured port
- [ ] 13.4 Create `scripts/run_tests.sh` — builds and runs all test binaries, reporting pass/fail per test suite
- [ ] 13.5 Create `README.md` documenting: build instructions, how to run the server and client, supported SQL syntax, API usage example, configuration options, and folder structure
