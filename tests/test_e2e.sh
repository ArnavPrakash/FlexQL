#!/bin/bash

# FlexQL End-to-End Test Script
set -e

# Start server in background
./build/flexql_server &
SERVER_PID=$!

# Wait for server to start
sleep 2

# Create script logic through standard input redirection
OUTPUT=$(./build/flexql_client <<EOF
CREATE TABLE e2e_users (id INT, name TEXT);
INSERT INTO e2e_users VALUES (1, 'Alice');
INSERT INTO e2e_users VALUES (2, 'Bob');
SELECT * FROM e2e_users;
exit
EOF
)

# Shutdown server safely
kill -9 $SERVER_PID

# Validate output
if echo "$OUTPUT" | grep -q 'Alice'; then
    echo "E2E Test Passed: Alice found in results."
    exit 0
else
    echo "E2E Test Failed: Expected 'Alice' but got something else."
    echo "$OUTPUT"
    exit 1
fi
