# Requirements Document

## Introduction

FlexQL is a client-server relational database system implemented in C/C++. It consists of an interactive REPL client and a multithreaded server that processes SQL-like queries, manages persistent storage, maintains B-Tree indexes, caches query results with LRU eviction, and ensures data durability via a Write-Ahead Log (WAL). The system supports a subset of SQL (CREATE TABLE, INSERT, SELECT with optional WHERE and INNER JOIN) over a TCP network connection using a length-prefixed binary/text protocol.

## Glossary

- **FlexQL_System**: The complete client-server database system described in this document.
- **Client**: The interactive REPL terminal process that connects to the Server using the FlexQL C/C++ API.
- **Server**: The multithreaded process that accepts TCP connections, parses queries, executes them, and returns results.
- **REPL**: Read-Eval-Print Loop — the interactive terminal interface provided by the Client.
- **API**: The public C interface defined in `flexql.h` used by the Client to communicate with the Server.
- **Parser**: The Server-side component that tokenizes and parses SQL-like query strings into an internal AST representation.
- **AST**: Abstract Syntax Tree — the internal structured representation of a parsed query.
- **Query_Executor**: The Server-side component that evaluates an AST against the Storage Engine and returns result rows.
- **Storage_Engine**: The Server-side component that manages persistent row-major table data on disk.
- **Schema_Manager**: The Server-side component that stores and retrieves table schemas (column names and types).
- **Index_Manager**: The Server-side component that manages B-Tree indexes on primary key columns.
- **B_Tree**: The balanced tree data structure used for primary key indexing, inspired by InnoDB's clustered index.
- **LRU_Cache**: The Least Recently Used cache used to store and evict query results.
- **WAL**: Write-Ahead Log — a durability mechanism that records changes before applying them to data files.
- **Concurrency_Manager**: The Server-side component that manages reader-writer locks per table and a global mutex for shared structures.
- **Network_Layer**: The component responsible for TCP socket management and the length-prefixed protocol framing.
- **Protocol**: The length-prefixed binary/text protocol used to transmit queries and results over TCP.
- **Row**: A single record in a table, stored in row-major format.
- **Column**: A named, typed field within a table schema. Supported types: INT, TEXT.
- **Primary_Key**: The first column of a table, used as the B-Tree index key.
- **Result_Set**: The collection of rows returned by a SELECT query.
- **Callback**: The user-supplied function passed to `flexql_exec` that is invoked once per result row.
- **Connection**: A single TCP session between one Client instance and the Server.
- **Thread_Pool**: The pool of Server threads that handle concurrent client connections.
- **Page**: A fixed-size unit of disk storage used by the Storage_Engine.
- **WAL_Record**: A single entry in the WAL describing one write operation.
- **Column_Projection**: The list of columns specified in a SELECT statement to be returned in the Result_Set. When `*` is used, all columns are included.
- **Comparison_Operator**: An operator used in a WHERE clause condition; supported operators are `=` (equality) and `>` (greater-than).
- **Batch_Insert**: An INSERT statement that specifies multiple value tuples in a single statement, e.g., `INSERT INTO t VALUES (...), (...), ...`.
- **Type_Alias**: An alternative column type name accepted by the Parser and normalized to an internal type (e.g., `DECIMAL` → INT, `VARCHAR(n)` → TEXT).

---

## Requirements

### Requirement 1: Client REPL Interface

**User Story:** As a database user, I want an interactive terminal interface, so that I can type SQL-like commands and see results immediately.

#### Acceptance Criteria

1. THE Client SHALL present an interactive REPL prompt to the user upon successful connection to the Server.
2. WHEN the user enters a SQL-like command and presses Enter, THE Client SHALL transmit the command to the Server via `flexql_exec` and display the returned results.
3. WHEN the user enters the `.exit` command, THE Client SHALL call `flexql_close` and terminate the process with exit code 0.
4. WHEN `flexql_exec` returns `FLEXQL_ERROR`, THE Client SHALL display the error message from `errmsg` to stderr and continue the REPL loop.
5. WHEN the Server returns a Result_Set, THE Client SHALL invoke the user Callback once per Row, printing column names on the first invocation and column values on each subsequent invocation.
6. WHILE a query is in progress, THE Client SHALL block the REPL prompt until the Server response is fully received.
7. IF the connection to the Server is lost during a query, THEN THE Client SHALL display a connection error message and exit with a non-zero exit code.

---

### Requirement 2: FlexQL C/C++ API

**User Story:** As a developer, I want a well-defined C API, so that I can embed FlexQL client functionality in any C/C++ application.

#### Acceptance Criteria

1. THE API SHALL expose the `flexql_open(const char *host, int port, FlexQL **db)` function that establishes a TCP Connection to the Server and allocates a `FlexQL` handle, returning `FLEXQL_OK` on success.
2. THE API SHALL expose the `flexql_close(FlexQL *db)` function that closes the TCP Connection and frees the `FlexQL` handle, returning `FLEXQL_OK` on success.
3. THE API SHALL expose the `flexql_exec(FlexQL *db, const char *sql, int (*callback)(void*, int, char**, char**), void *arg, char **errmsg)` function that sends a SQL string to the Server, receives the Result_Set, and invokes the Callback once per Row.
4. WHEN `flexql_exec` is called with a NULL callback, THE API SHALL execute the query and discard result rows without error.
5. WHEN the Server returns an error response, THE API SHALL set `*errmsg` to a heap-allocated error string and return `FLEXQL_ERROR`.
6. THE API SHALL expose `flexql_free(void *ptr)` that frees memory allocated by the API, including error message strings.
7. WHEN `flexql_open` fails to connect, THE API SHALL return `FLEXQL_ERROR` and set `*db` to NULL.
8. IF the Callback returns 1 during result row iteration, THEN THE API SHALL stop invoking the Callback for subsequent rows and return `FLEXQL_OK`.
9. THE API SHALL define the constants `FLEXQL_OK` as 0 and `FLEXQL_ERROR` as 1.

---

### Requirement 3: Network Protocol

**User Story:** As a system integrator, I want a reliable network protocol between Client and Server, so that queries and results are transmitted without corruption or ambiguity.

#### Acceptance Criteria

1. THE Network_Layer SHALL use TCP sockets for all Client-Server communication.
2. THE Protocol SHALL frame every message with a 4-byte big-endian length prefix followed by the message payload.
3. WHEN the Client sends a query, THE Network_Layer SHALL transmit a Protocol frame containing the UTF-8 encoded SQL string.
4. WHEN the Server sends a Result_Set, THE Network_Layer SHALL transmit one Protocol frame per Row, each containing column values as newline-delimited UTF-8 strings.
5. WHEN the Server sends an error, THE Network_Layer SHALL transmit a Protocol frame with an error-type marker and a UTF-8 error message.
6. WHEN the Server finishes sending all rows of a Result_Set, THE Network_Layer SHALL transmit a Protocol end-of-result frame to signal completion.
7. IF a partial Protocol frame is received, THEN THE Network_Layer SHALL buffer the data and wait for the remaining bytes before processing the message.
8. THE Server SHALL listen on a configurable TCP port specified at startup.

---

### Requirement 4: SQL Parser

**User Story:** As a developer, I want the Server to parse SQL-like commands into structured representations, so that the Query_Executor can evaluate them correctly.

#### Acceptance Criteria

1. WHEN a `CREATE TABLE table_name (col1 TYPE, col2 TYPE, ...)` statement is received, THE Parser SHALL produce an AST node of type CREATE_TABLE containing the table name and an ordered list of (column name, type) pairs.
2. WHEN an `INSERT INTO table_name VALUES (v1, v2, ...)` statement is received, THE Parser SHALL produce an AST node of type INSERT containing the table name and an ordered list of literal values.
3. WHEN a `SELECT * FROM table_name` statement is received, THE Parser SHALL produce an AST node of type SELECT containing the table name, a wildcard Column_Projection, and no WHERE clause.
4. WHEN a `SELECT col1, col2, ... FROM table_name` statement is received, THE Parser SHALL produce an AST node of type SELECT containing the table name and a Column_Projection listing the named columns.
5. WHEN a `SELECT * FROM table_name WHERE col = value` statement is received, THE Parser SHALL produce an AST node of type SELECT containing the table name, a wildcard Column_Projection, and a WHERE condition with Comparison_Operator `=`.
6. WHEN a `SELECT col_list FROM table_name WHERE col op value` statement is received, THE Parser SHALL produce an AST node of type SELECT containing the table name, the Column_Projection, and a WHERE condition with the specified Comparison_Operator (`=` or `>`).
7. WHEN a `SELECT * FROM tableA INNER JOIN tableB ON tableA.col = tableB.col` statement is received, THE Parser SHALL produce an AST node of type SELECT_JOIN containing both table names, a wildcard Column_Projection, and the join condition column pair.
8. WHEN a SELECT_JOIN statement includes a Column_Projection with qualified column references of the form `table.column`, THE Parser SHALL include those qualified references in the AST node's Column_Projection.
9. WHERE a SELECT_JOIN statement includes a WHERE clause with a Comparison_Operator (`=` or `>`), THE Parser SHALL include the condition (column, operator, value) in the AST node.
10. IF a statement does not match any supported SQL pattern, THEN THE Parser SHALL return a parse error with a descriptive message identifying the unrecognized syntax.
11. IF a column type other than INT, TEXT, DECIMAL, or VARCHAR(n) is specified in a CREATE TABLE statement, THEN THE Parser SHALL return a parse error.
12. WHEN a CREATE TABLE statement specifies `DECIMAL` as a column type, THE Parser SHALL normalize it to INT in the AST node.
13. WHEN a CREATE TABLE statement specifies `VARCHAR(n)` as a column type, THE Parser SHALL normalize it to TEXT in the AST node.
14. WHEN an `INSERT INTO table_name VALUES (...), (...), ...` statement with multiple value tuples is received, THE Parser SHALL produce an AST node of type BATCH_INSERT containing the table name and an ordered list of value-tuple lists.
15. THE Parser SHALL be case-insensitive for SQL keywords (SELECT, FROM, WHERE, INSERT, INTO, VALUES, CREATE, TABLE, INNER, JOIN, ON, DECIMAL, VARCHAR).
16. THE Pretty_Printer SHALL format any AST node back into a canonical SQL string.
17. FOR ALL valid SQL strings that parse successfully, parsing then printing then parsing SHALL produce an equivalent AST (round-trip property).

---

### Requirement 5: Query Executor

**User Story:** As a database user, I want the Server to correctly execute my queries, so that I get accurate results from the database.

#### Acceptance Criteria

1. WHEN a CREATE_TABLE AST node is executed, THE Query_Executor SHALL instruct the Schema_Manager to persist the table schema and the Storage_Engine to initialize a new table data file, returning success if both succeed.
2. IF a CREATE TABLE statement names a table that already exists, THEN THE Query_Executor SHALL return an error without modifying any existing data.
3. WHEN an INSERT AST node is executed, THE Query_Executor SHALL validate that the number of values matches the number of columns in the schema, then instruct the Storage_Engine to append the Row and the Index_Manager to insert the primary key.
4. WHEN a BATCH_INSERT AST node is executed, THE Query_Executor SHALL validate each value tuple against the schema and insert all tuples atomically, such that either all rows are inserted or none are inserted on error.
5. IF an INSERT or BATCH_INSERT statement provides a value count per tuple that does not match the table's column count, THEN THE Query_Executor SHALL return an error without modifying any existing data.
6. IF an INSERT or BATCH_INSERT statement provides a value that cannot be coerced to the declared column type, THEN THE Query_Executor SHALL return an error without modifying any existing data.
7. WHEN a SELECT AST node with a wildcard Column_Projection and no WHERE clause is executed, THE Query_Executor SHALL retrieve all Rows from the Storage_Engine for the specified table and return them as a Result_Set.
8. WHEN a SELECT AST node with a named Column_Projection is executed, THE Query_Executor SHALL return only the specified columns in the Result_Set, in the order listed in the Column_Projection.
9. WHEN a SELECT AST node with a WHERE clause using Comparison_Operator `=` is executed, THE Query_Executor SHALL use the Index_Manager if the condition column is the Primary_Key, otherwise perform a full table scan, and return only matching Rows.
10. WHEN a SELECT AST node with a WHERE clause using Comparison_Operator `>` is executed, THE Query_Executor SHALL perform a full table scan and return only Rows where the condition column value is numerically greater than the specified value.
11. WHEN a SELECT_JOIN AST node is executed, THE Query_Executor SHALL perform an INNER JOIN between the two tables on the specified column pair and return only rows where the join condition is satisfied.
12. WHEN a SELECT_JOIN AST node includes a Column_Projection with qualified column references of the form `table.column`, THE Query_Executor SHALL resolve each reference to the correct column in the corresponding joined table and include only those columns in the Result_Set.
13. WHEN a SELECT_JOIN AST node includes a WHERE clause with a qualified column reference and a Comparison_Operator, THE Query_Executor SHALL apply the filter after the join using the resolved column value.
14. IF a SELECT or INSERT statement references a table that does not exist, THEN THE Query_Executor SHALL return an error.
15. IF a SELECT WHERE, JOIN ON, or Column_Projection clause references a column that does not exist in the table schema, THEN THE Query_Executor SHALL return an error.

---

### Requirement 6: Storage Engine

**User Story:** As a system architect, I want the database to persist data in row-major format on disk, so that rows can be efficiently read and written.

#### Acceptance Criteria

1. THE Storage_Engine SHALL store table data in row-major format, where each Row is stored as a contiguous sequence of fixed-size or length-prefixed fields on disk.
2. THE Storage_Engine SHALL organize data into fixed-size Pages of 4096 bytes.
3. WHEN a Row is inserted, THE Storage_Engine SHALL append the Row to the appropriate Page, allocating a new Page if the current Page is full.
4. WHEN a full table scan is requested, THE Storage_Engine SHALL read all Pages for the table sequentially and return all non-deleted Rows.
5. THE Storage_Engine SHALL store each table's data in a dedicated file under the `data/tables/` directory, named by table name.
6. WHEN the Server starts, THE Storage_Engine SHALL load existing table data files from `data/tables/` and make them available for queries.
7. IF a disk write fails during a Row insertion, THEN THE Storage_Engine SHALL return an error and leave the table data in its pre-insertion state.
8. THE Schema_Manager SHALL persist table schemas (column names and types) to a dedicated schema file under `data/tables/`, loadable on Server restart.

---

### Requirement 7: B-Tree Index

**User Story:** As a database user, I want primary key lookups to be fast, so that WHERE clause queries on the primary key complete efficiently even with large datasets.

#### Acceptance Criteria

1. THE Index_Manager SHALL maintain a B-Tree index on the Primary_Key column of every table.
2. WHEN a Row is inserted, THE Index_Manager SHALL insert the Primary_Key value and the corresponding Row's disk offset into the B-Tree.
3. WHEN a SELECT WHERE query filters on the Primary_Key column, THE Query_Executor SHALL use the Index_Manager to perform a B-Tree lookup and retrieve only the matching Row's disk offset, avoiding a full table scan.
4. THE Index_Manager SHALL persist B-Tree index data to a dedicated file under `data/indexes/`, named by table name.
5. WHEN the Server starts, THE Index_Manager SHALL load existing index files from `data/indexes/` and reconstruct the in-memory B-Tree structures.
6. THE B_Tree SHALL maintain the sorted order of Primary_Key values after every insertion.
7. IF a B-Tree index file is missing on Server startup for an existing table, THEN THE Index_Manager SHALL rebuild the index by scanning the corresponding table data file.

---

### Requirement 8: LRU Query Cache

**User Story:** As a database user, I want repeated identical queries to return results faster, so that read-heavy workloads benefit from caching.

#### Acceptance Criteria

1. THE LRU_Cache SHALL store the Result_Set of SELECT queries, keyed by the exact SQL string.
2. WHEN a SELECT query is received and an entry for the exact SQL string exists in the LRU_Cache, THE Query_Executor SHALL return the cached Result_Set without accessing the Storage_Engine.
3. WHEN a SELECT query is executed against the Storage_Engine, THE LRU_Cache SHALL store the resulting Result_Set keyed by the SQL string.
4. WHEN the LRU_Cache reaches its configured maximum entry count, THE LRU_Cache SHALL evict the least recently used entry before inserting a new one.
5. WHEN an INSERT is executed on a table, THE LRU_Cache SHALL invalidate all cached Result_Sets whose SQL string references that table name.
6. WHEN a CREATE TABLE is executed, THE LRU_Cache SHALL invalidate all cached entries referencing the new table name.
7. THE LRU_Cache maximum entry count SHALL be configurable via a Server startup configuration file.

---

### Requirement 9: Write-Ahead Log (WAL)

**User Story:** As a system operator, I want the database to survive crashes without data corruption, so that committed writes are not lost.

#### Acceptance Criteria

1. BEFORE the Storage_Engine applies any write operation to a table data file, THE WAL SHALL append a WAL_Record describing the operation to the WAL file under `data/wal/`.
2. WHEN a WAL_Record is successfully written and fsynced, THE Storage_Engine SHALL proceed to apply the write to the table data file.
3. WHEN the Server starts, THE WAL SHALL scan the WAL file for any WAL_Records that were written but not yet applied to the table data files, and replay them.
4. WHEN a write operation is fully applied to the table data file, THE WAL SHALL mark the corresponding WAL_Record as committed.
5. IF the WAL file cannot be written during an INSERT, THEN THE Storage_Engine SHALL return an error and not modify the table data file.
6. THE WAL file SHALL be stored under `data/wal/` and named by a fixed filename (e.g., `wal.log`).

---

### Requirement 10: Concurrency Control

**User Story:** As a system operator, I want the Server to safely handle multiple simultaneous clients, so that concurrent queries do not corrupt data or produce incorrect results.

#### Acceptance Criteria

1. THE Server SHALL accept multiple simultaneous TCP Connections, handling each Connection in a dedicated thread from the Thread_Pool.
2. THE Concurrency_Manager SHALL maintain one reader-writer lock per table, allowing multiple concurrent readers or one exclusive writer.
3. WHEN a SELECT query is executed on a table, THE Concurrency_Manager SHALL acquire a read lock on that table before accessing the Storage_Engine and release it after the Result_Set is fully assembled.
4. WHEN an INSERT or CREATE TABLE query is executed on a table, THE Concurrency_Manager SHALL acquire a write lock on that table before modifying the Storage_Engine and release it after the operation completes.
5. THE Concurrency_Manager SHALL use a global mutex to protect shared structures including the Schema_Manager metadata and the LRU_Cache.
6. IF a thread fails to acquire a lock within a configurable timeout, THEN THE Concurrency_Manager SHALL return a timeout error to the Query_Executor.
7. THE Thread_Pool size SHALL be configurable via a Server startup configuration file.

---

### Requirement 11: Server Lifecycle

**User Story:** As a system operator, I want the Server to start up and shut down cleanly, so that data is not corrupted and resources are released properly.

#### Acceptance Criteria

1. WHEN the Server is started with a valid configuration file, THE Server SHALL initialize the Storage_Engine, Index_Manager, LRU_Cache, WAL, Concurrency_Manager, and Network_Layer before accepting any connections.
2. WHEN the Server receives a SIGINT or SIGTERM signal, THE Server SHALL stop accepting new connections, wait for all active queries to complete, flush the WAL, and exit with code 0.
3. IF the Server fails to bind to the configured TCP port at startup, THEN THE Server SHALL log an error message to stderr and exit with a non-zero exit code.
4. IF any subsystem fails to initialize at startup, THEN THE Server SHALL log a descriptive error message to stderr and exit with a non-zero exit code.
5. THE Server SHALL log each accepted Connection, each executed query, and each error to a log file at a configurable path.

---

### Requirement 12: Performance Targets

**User Story:** As a database user, I want the system to handle large datasets efficiently, so that queries on ~10 million rows complete in a reasonable time.

#### Acceptance Criteria

1. WHEN 10 million Rows are inserted sequentially into a single table, THE Storage_Engine SHALL complete all insertions within 300 seconds on reference hardware (modern x86-64, SSD).
2. WHEN a SELECT WHERE query filters on the Primary_Key of a table containing 10 million Rows, THE Query_Executor SHALL return the matching Row within 100 milliseconds using the B-Tree index.
3. WHEN a full table scan SELECT is executed on a table containing 10 million Rows, THE Query_Executor SHALL return all rows within 60 seconds on reference hardware.
4. WHILE the Server is handling 10 million Rows, THE Storage_Engine SHALL consume no more than 2 GB of resident memory for in-memory index and cache structures combined.
