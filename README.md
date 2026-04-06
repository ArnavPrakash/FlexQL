# FlexQL

FlexQL is a powerful, lightweight client-server relational database implemented entirely in C++17. It manages full schema generation, persistent page storage mapping, concurrency access with multi-threading, B-Tree indexed search performance, write-ahead logging (WAL), LRU queries cache mapping, and native TCP networking protocols simultaneously.

## Features
- **SQL Parser engine:** Custom Lexer & Recursive Descent Parser built natively without `yacc`/`bison`. AST translations cover `SELECT`, `CREATE`, `INSERT`, `BATCH`, and inner `JOIN` query evaluations.
- **Relational Operations:** Performs evaluations tracking rows using highly efficient string logic inside native memory limits validating tables across WHERE operations.
- **Persistent B+Tree:** Full binary payload serialization on nodes mapping router identifiers dynamically per `BTREE_T = 50`. Lookups achieve optimal sub-linear speeds handling large clustered workloads linking `index_manager`.
- **Row-Major 4KB Storage Page Tables:** Append-only mapped limits handling length-prefixed variables translating safely mapped inside standard Linux page buffers (`4096-bytes`) mapped to uncompressed offset bounds per chunk.
- **Write-Ahead Logging (WAL):** Prevents crash data losses executing inserts.
- **LRU Cache Layer:** Resolves query outputs seamlessly skipping internal scans evaluating repetitive SQL strings via an intertwined unordered map and dynamic linked list mappings tracking capacities.
- **Concurrency Manager:** Protects the memory mapping pools scaling Readers and Writers seamlessly with standardized `shared_timed_mutex` limits.

## Compiling the Project

Ensure you have CMake (v3.14+) and a C++17 compatible compiler tracking pthread mappings.

```bash
mkdir -p build && cd build
cmake ..
make -j4
```
*Note: All executable binaries generated from the main target builds and testing wrappers will output natively straight into the local `./bin` directory layout!*

## Execution 

### Run FlexQL Server
Start the daemon allowing background TCP packets to parse queries automatically on port 5432!
```bash
./bin/flexql_server
```

*(By default, this stores active tables in `/data`, indices in`/data/indexes/`, schemas directly under`/data/tables/` and logs within `/data/wal/`.)*

### Run Interacting CLI Shell
Use the CLI Client opening the local TCP bind looping readline inputs mapping length-prefixed protocol layouts targeting queries.
```bash
./bin/flexql_client
```

### Run Subsystems Benchmark Profiler
The testing environment includes a generic generic benchmark execution scaling thousands of rows verifying C API wrapper limits dynamically linked resolving memory matrices!

```bash
# Launch generic benchmark linking local server inputs
./bin/benchmark_flexql
```

## Running Tests
Run the entire library of Unit Tests evaluating individual B-tree, LRU cache validations, WAL recoveries, Schemas layouts, Storage limits, and standard Query Executors.
```bash
cd build && ctest --output-on-failure
```
