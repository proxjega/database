#!/bin/bash
# test_operations.sh - Comprehensive test of all HTTP operations
# Tests routing correctness for write (leader-only) and read (follower-capable) operations

set -e

echo "========================================="
echo "COMPREHENSIVE HTTP OPERATIONS TEST"
echo "========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test result tracking
PASSED=0
FAILED=0
PROBLEMS=()

# Helper function to test and report
test_operation() {
    local test_name="$1"
    local expected="$2"
    local command="$3"

    echo -n "Testing: $test_name ... "

    result=$(eval "$command" 2>&1)

    if [[ "$expected" == "SUCCESS" ]]; then
        if echo "$result" | grep -q "error\|Error\|ERR"; then
            echo -e "${RED}FAIL${NC}"
            echo "  Expected: Success"
            echo "  Got: $result"
            FAILED=$((FAILED + 1))
            PROBLEMS+=("FAIL: $test_name - $result")
        else
            echo -e "${GREEN}PASS${NC}"
            PASSED=$((PASSED + 1))
        fi
    else
        # Expected failure
        if echo "$result" | grep -q "$expected"; then
            echo -e "${GREEN}PASS${NC} (correctly rejected)"
            PASSED=$((PASSED + 1))
        else
            echo -e "${RED}FAIL${NC}"
            echo "  Expected error containing: $expected"
            echo "  Got: $result"
            FAILED=$((FAILED + 1))
            PROBLEMS+=("FAIL: $test_name - Expected '$expected' but got: $result")
        fi
    fi
}

# Get cluster status
echo "========================================="
echo "PHASE 1: Initial Cluster Status"
echo "========================================="
cd database/Replication
./client status
echo ""

# Identify leader and followers
LEADER=$(./client status | grep LEADER | awk '{print $1}')
FOLLOWERS=$(./client status | grep FOLLOWER | awk '{print $1}')
FOLLOWER1=$(echo "$FOLLOWERS" | head -1)
FOLLOWER2=$(echo "$FOLLOWERS" | tail -1)

echo "Leader: Node $LEADER"
echo "Followers: $FOLLOWERS"
echo ""

# Setup test data
echo "========================================="
echo "PHASE 2: Setup Test Data"
echo "========================================="
curl -X POST "http://localhost:8080/api/set/test_key?nodeId=$LEADER" \
  -H "Content-Type: application/json" \
  -d '{"value":"test_value"}' -s > /dev/null
echo "Created test_key=test_value"

curl -X POST "http://localhost:8080/api/set/alpha?nodeId=$LEADER" \
  -H "Content-Type: application/json" \
  -d '{"value":"first"}' -s > /dev/null
curl -X POST "http://localhost:8080/api/set/beta?nodeId=$LEADER" \
  -H "Content-Type: application/json" \
  -d '{"value":"second"}' -s > /dev/null
curl -X POST "http://localhost:8080/api/set/gamma?nodeId=$LEADER" \
  -H "Content-Type: application/json" \
  -d '{"value":"third"}' -s > /dev/null
echo "Created alpha, beta, gamma for range queries"
echo ""

sleep 1

echo "========================================="
echo "PHASE 3: Write Operations (Leader-Only)"
echo "========================================="

# SET operations
test_operation "SET to leader" "SUCCESS" \
  "curl -s -X POST 'http://localhost:8080/api/set/write_test?nodeId=$LEADER' -H 'Content-Type: application/json' -d '{\"value\":\"data\"}'"

test_operation "SET to follower (should reject)" "must target the leader" \
  "curl -s -X POST 'http://localhost:8080/api/set/write_test2?nodeId=$FOLLOWER1' -H 'Content-Type: application/json' -d '{\"value\":\"data\"}'"

# DEL operations
test_operation "DEL to leader" "SUCCESS" \
  "curl -s -X POST 'http://localhost:8080/api/del/write_test?nodeId=$LEADER'"

test_operation "DEL to follower (should reject)" "must target the leader" \
  "curl -s -X POST 'http://localhost:8080/api/del/test_key?nodeId=$FOLLOWER1'"

# OPTIMIZE operation
test_operation "OPTIMIZE to leader" "SUCCESS" \
  "curl -s -X POST 'http://localhost:8080/api/optimize?nodeId=$LEADER'"

test_operation "OPTIMIZE to follower (should reject)" "must target the leader\|Failed to discover" \
  "curl -s -X POST 'http://localhost:8080/api/optimize?nodeId=$FOLLOWER1'"

echo ""

echo "========================================="
echo "PHASE 4: Read Operations (Follower-Capable)"
echo "========================================="

# GET operations
test_operation "GET from leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/get/test_key?nodeId=$LEADER'"

test_operation "GET from follower 1" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/get/test_key?nodeId=$FOLLOWER1'"

test_operation "GET from follower 2" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/get/test_key?nodeId=$FOLLOWER2'"

# GETFF operations (forward range query)
test_operation "GETFF from leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getff/a?count=5&nodeId=$LEADER'"

test_operation "GETFF from follower 1" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getff/a?count=5&nodeId=$FOLLOWER1'"

test_operation "GETFF from follower 2" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getff/a?count=5&nodeId=$FOLLOWER2'"

# GETFB operations (backward range query)
test_operation "GETFB from leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getfb/z?count=5&nodeId=$LEADER'"

test_operation "GETFB from follower 1" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getfb/z?count=5&nodeId=$FOLLOWER1'"

test_operation "GETFB from follower 2" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getfb/z?count=5&nodeId=$FOLLOWER2'"

# GETKEYS operations (prefix-based key listing)
test_operation "GETKEYS (prefix) from leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/prefix/a?nodeId=$LEADER'"

test_operation "GETKEYS (prefix) from follower 1" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/prefix/a?nodeId=$FOLLOWER1'"

test_operation "GETKEYS (prefix) from follower 2" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/prefix/a?nodeId=$FOLLOWER2'"

# GETKEYSPAGING operations
test_operation "GETKEYSPAGING from leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/paging?pageSize=10&pageNum=1&nodeId=$LEADER'"

test_operation "GETKEYSPAGING from follower 1" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/paging?pageSize=10&pageNum=1&nodeId=$FOLLOWER1'"

test_operation "GETKEYSPAGING from follower 2" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/paging?pageSize=10&pageNum=1&nodeId=$FOLLOWER2'"

echo ""

echo "========================================="
echo "PHASE 5: Leader Failover Test"
echo "========================================="

echo "Current leader: Node $LEADER"
echo "Killing leader to trigger failover..."

# Kill the leader (and force-close all its port connections to avoid TIME_WAIT)
case $LEADER in
  1) ssh Anthony@207.180.251.206 'ss -K dst :7001 dst :7002 dst :7101 dst :8001 2>/dev/null || true; killall run leader follower' 2>/dev/null || true ;;
  2) ssh Austin@167.86.66.60 'ss -K dst :7001 dst :7002 dst :7102 dst :8002 2>/dev/null || true; killall run leader follower' 2>/dev/null || true ;;
  3) ssh Edward@167.86.83.198 'ss -K dst :7001 dst :7002 dst :7103 dst :8003 2>/dev/null || true; killall run leader follower' 2>/dev/null || true ;;
  4) ssh Anthony@167.86.81.251 'ss -K dst :7001 dst :7002 dst :7104 dst :8004 2>/dev/null || true; killall run leader follower' 2>/dev/null || true ;;
esac

echo "Waiting 5 seconds for election..."
sleep 5

# Get new cluster status
echo ""
echo "New cluster status:"
./client status
echo ""

# Identify new leader
NEW_LEADER=$(./client status | grep LEADER | awk '{print $1}')
NEW_FOLLOWERS=$(./client status | grep FOLLOWER | awk '{print $1}')
NEW_FOLLOWER1=$(echo "$NEW_FOLLOWERS" | head -1)

echo "New leader: Node $NEW_LEADER"
echo "New followers: $NEW_FOLLOWERS"
echo ""

# Insert failover test data
curl -X POST "http://localhost:8080/api/set/failover_key?nodeId=$NEW_LEADER" \
  -H "Content-Type: application/json" \
  -d '{"value":"after_failover"}' -s > /dev/null
echo "Created failover_key=after_failover"
echo ""

echo "========================================="
echo "PHASE 6: Post-Failover Operation Tests"
echo "========================================="

# Write operations after failover
test_operation "POST-FAILOVER: SET to new leader" "SUCCESS" \
  "curl -s -X POST 'http://localhost:8080/api/set/post_failover?nodeId=$NEW_LEADER' -H 'Content-Type: application/json' -d '{\"value\":\"data\"}'"

test_operation "POST-FAILOVER: SET to follower (should reject)" "must target the leader" \
  "curl -s -X POST 'http://localhost:8080/api/set/post_failover2?nodeId=$NEW_FOLLOWER1' -H 'Content-Type: application/json' -d '{\"value\":\"data\"}'"

# Read operations after failover
test_operation "POST-FAILOVER: GET from new leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/get/failover_key?nodeId=$NEW_LEADER'"

test_operation "POST-FAILOVER: GET from follower" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/get/failover_key?nodeId=$NEW_FOLLOWER1'"

test_operation "POST-FAILOVER: GETFF from new leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getff/a?count=5&nodeId=$NEW_LEADER'"

test_operation "POST-FAILOVER: GETFF from follower" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getff/a?count=5&nodeId=$NEW_FOLLOWER1'"

test_operation "POST-FAILOVER: GETFB from new leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getfb/z?count=5&nodeId=$NEW_LEADER'"

test_operation "POST-FAILOVER: GETFB from follower" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/getfb/z?count=5&nodeId=$NEW_FOLLOWER1'"

test_operation "POST-FAILOVER: GETKEYS from new leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/prefix/a?nodeId=$NEW_LEADER'"

test_operation "POST-FAILOVER: GETKEYS from follower" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/prefix/a?nodeId=$NEW_FOLLOWER1'"

test_operation "POST-FAILOVER: GETKEYSPAGING from new leader" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/paging?pageSize=10&pageNum=1&nodeId=$NEW_LEADER'"

test_operation "POST-FAILOVER: GETKEYSPAGING from follower" "SUCCESS" \
  "curl -s 'http://localhost:8080/api/keys/paging?pageSize=10&pageNum=1&nodeId=$NEW_FOLLOWER1'"

echo ""

echo "========================================="
echo "PHASE 7: Restart Killed Leader Node"
echo "========================================="

echo "Restarting Node $LEADER (killed in Phase 5)..."

# Restart the killed leader node
case $LEADER in
  1)
    echo "Restarting Node 1 on 207.180.251.206 (Anthony)..."
    ssh -n -f Anthony@207.180.251.206 'cd database/Replication && setsid ./run 1 > node1.out 2>&1 < /dev/null &' 2>/dev/null || echo "Warning: Could not restart Node 1"
    ;;
  2)
    echo "Restarting Node 2 on 167.86.66.60 (Austin)..."
    ssh -n -f Austin@167.86.66.60 'cd database/Replication && setsid ./run 2 > node2.out 2>&1 < /dev/null &' 2>/dev/null || echo "Warning: Could not restart Node 2"
    ;;
  3)
    echo "Restarting Node 3 on 167.86.83.198 (Edward)..."
    ssh -n -f Edward@167.86.83.198 'cd database/Replication && setsid ./run 3 > node3.out 2>&1 < /dev/null &' 2>/dev/null || echo "Warning: Could not restart Node 3"
    ;;
  4)
    echo "Restarting Node 4 on 167.86.81.251 (Anthony)..."
    ssh -n -f Anthony@167.86.81.251 'cd database/Replication && setsid ./run 4 > node4.out 2>&1 < /dev/null &' 2>/dev/null || echo "Warning: Could not restart Node 4"
    ;;
esac

echo "Waiting 5 seconds for node to rejoin cluster..."
sleep 5

echo ""
echo "Final cluster status:"
./client status
echo ""

echo "========================================="
echo "TEST SUMMARY"
echo "========================================="
echo -e "Total tests: $((PASSED + FAILED))"
echo -e "${GREEN}Passed: $PASSED${NC}"
echo -e "${RED}Failed: $FAILED${NC}"
echo ""

if [ $FAILED -gt 0 ]; then
    echo "========================================="
    echo "PROBLEMS ENCOUNTERED:"
    echo "========================================="
    for problem in "${PROBLEMS[@]}"; do
        echo -e "${RED}$problem${NC}"
    done
    echo ""
    exit 1
else
    echo -e "${GREEN}ALL TESTS PASSED!${NC}"
    echo ""
    echo "✅ Write operations correctly route to leader only"
    echo "✅ Read operations successfully route to all nodes (leader + followers)"
    echo "✅ Operations work correctly after leader failover"
    echo ""
fi
