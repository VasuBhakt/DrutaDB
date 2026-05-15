#!/bin/bash
# DrutaDB Pub/Sub Test Suite
# Requires: nc (netcat), server running on localhost:6379

# sed -i 's/\r$//' pubsub_test.sh

PASS=0
FAIL=0

pass() {
    echo "✅ $1"
    ((PASS++))
}

fail() {
    echo "❌ $1 (Got: $2)"
    ((FAIL++))
}

# Helper: send a single RESP command and get response
send_cmd() {
    printf "$1" | nc -w 1 localhost 6379
}

echo "=== DrutaDB Pub/Sub Test Suite ==="
echo ""

# -------------------------------------------------------------------
# TEST 1: SUBSCRIBE returns correct confirmation
# -------------------------------------------------------------------
echo "--- Test 1: SUBSCRIBE confirmation ---"

# Subscribe to channel "test1" and read for 1 second
# SUBSCRIBE test1 => *2 $9 SUBSCRIBE $5 test1
SUB_RES=$(printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$5\r\ntest1\r\n" | nc -w 1 localhost 6379)

if [[ "$SUB_RES" == *"subscribe"* && "$SUB_RES" == *"test1"* ]]; then
    pass "SUBSCRIBE returns confirmation with channel name"
else
    fail "SUBSCRIBE confirmation" "$SUB_RES"
fi

# -------------------------------------------------------------------
# TEST 2: PUBLISH to empty channel returns 0
# -------------------------------------------------------------------
echo "--- Test 2: PUBLISH to empty channel ---"

# PUBLISH emptychan "hello" => *3 $7 PUBLISH $9 emptychan $5 hello
PUB_EMPTY_RES=$(send_cmd "*3\r\n\$7\r\nPUBLISH\r\n\$9\r\nemptychan\r\n\$5\r\nhello\r\n")

if [[ "$PUB_EMPTY_RES" == *":0"* ]]; then
    pass "PUBLISH to empty channel returns 0"
else
    fail "PUBLISH to empty channel" "$PUB_EMPTY_RES"
fi

# -------------------------------------------------------------------
# TEST 3: Full pub/sub flow - subscriber receives message
# -------------------------------------------------------------------

echo "--- Test 3: Full pub/sub message delivery ---"
SUB_OUTPUT_FILE=$(mktemp)
(
    printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$8\r\ntestchan\r\n"
    sleep 2
) | timeout 3s nc localhost 6379 > "$SUB_OUTPUT_FILE" 2>/dev/null &
SUB_PID=$!
sleep 0.5
send_cmd "*3\r\n\$7\r\nPUBLISH\r\n\$8\r\ntestchan\r\n\$12\r\nhello_druta!\r\n" > /dev/null
wait $SUB_PID 2>/dev/null
[[ $(cat "$SUB_OUTPUT_FILE") == *"hello_druta!"* ]] && pass "Subscriber received message" || fail "Subscriber receipt" "$(cat "$SUB_OUTPUT_FILE")"

# -------------------------------------------------------------------
# TEST 4: Multiple subscribers on same channel
# -------------------------------------------------------------------

echo "--- Test 4: Multiple subscribers ---"
OUT_A=$(mktemp); OUT_B=$(mktemp)
(printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$6\r\nmulti1\r\n"; sleep 2) | timeout 3s nc localhost 6379 > "$OUT_A" 2>/dev/null &
(printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$6\r\nmulti1\r\n"; sleep 2) | timeout 3s nc localhost 6379 > "$OUT_B" 2>/dev/null &
sleep 0.5
MULTI_RES=$(send_cmd "*3\r\n\$7\r\nPUBLISH\r\n\$6\r\nmulti1\r\n\$9\r\nbroadcast\r\n")
wait; # Waits for all background timeouts
[[ "$MULTI_RES" == *":2"* ]] && pass "PUBLISH to 2 returns 2" || fail "Multi-pub" "$MULTI_RES"
# Cleanup temp files
rm -f "$SUB_OUTPUT_FILE" "$OUT_A" "$OUT_B"
echo ""
echo "=== RESULTS: $PASS Passed, $FAIL Failed ==="

# -------------------------------------------------------------------
# TEST 5: Subscriber disconnect cleanup (ghost subscriber removal)
# -------------------------------------------------------------------
echo "--- Test 5: Disconnect cleanup ---"

SUB_OUT_GHOST=$(mktemp /tmp/drutadb_ghost_XXXXXX)

# Subscribe and immediately disconnect (only wait 1 second)
(
    printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$8\r\nghostchn\r\n"
    sleep 1
) | timeout 3s nc localhost 6379 > "$SUB_OUT_GHOST" 2>/dev/null &
GHOST_PID=$!

sleep 0.5

# Verify subscription worked
GHOST_PUB1=$(send_cmd "*3\r\n\$7\r\nPUBLISH\r\n\$8\r\nghostchn\r\n\$4\r\ntest\r\n")
if [[ "$GHOST_PUB1" == *":1"* ]]; then
    pass "Ghost subscriber initially connected"
else
    fail "Ghost subscriber initial connection" "$GHOST_PUB1"
fi

# Wait for subscriber to disconnect
wait $GHOST_PID 2>/dev/null
sleep 0.5

# Now publish again - should get 0 subscribers
GHOST_PUB2=$(send_cmd "*3\r\n\$7\r\nPUBLISH\r\n\$8\r\nghostchn\r\n\$4\r\ntest\r\n")
if [[ "$GHOST_PUB2" == *":0"* ]]; then
    pass "Ghost subscriber cleaned up after disconnect"
else
    fail "Ghost subscriber cleanup" "$GHOST_PUB2"
fi

rm -f "$SUB_OUT_GHOST"

# -------------------------------------------------------------------
# TEST 6: Subscriber mode blocks non-pubsub commands
# -------------------------------------------------------------------
echo "--- Test 6: Subscriber mode command restriction ---"

SUB_RESTRICT=$(mktemp /tmp/drutadb_restrict_XXXXXX)

(
    # Subscribe first, then try SET (which should be blocked)
    printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$7\r\nrestrct\r\n"
    sleep 0.5
    printf "*3\r\n\$3\r\nSET\r\n\$3\r\nfoo\r\n\$3\r\nbar\r\n"
    sleep 1
) | timeout 5s nc localhost 6379 > "$SUB_RESTRICT" 2>/dev/null

RESTRICT_OUT=$(cat "$SUB_RESTRICT")
if [[ "$RESTRICT_OUT" == *"-ERR"* && "$RESTRICT_OUT" == *"pub/sub"* ]]; then
    pass "Subscriber mode correctly blocks non-pubsub commands"
else
    fail "Subscriber mode restriction" "$RESTRICT_OUT"
fi

rm -f "$SUB_RESTRICT"

# -------------------------------------------------------------------
# TEST 7: PUBLISH with wrong argument count
# -------------------------------------------------------------------
echo "--- Test 7: PUBLISH argument validation ---"

# PUBLISH with only 1 arg (missing message)
PUB_BAD=$(send_cmd "*2\r\n\$7\r\nPUBLISH\r\n\$4\r\ntest\r\n")
if [[ "$PUB_BAD" == *"-ERR"* ]]; then
    pass "PUBLISH with missing message returns error"
else
    fail "PUBLISH missing message" "$PUB_BAD"
fi

# -------------------------------------------------------------------
# TEST 8: Unsubscribe from non-joined channel
# -------------------------------------------------------------------
echo "--- Test 8: Unsubscribe from non-joined channel ---"
UNSUB_RES=$(send_cmd "*2\r\n\$11\r\nunsubscribe\r\n\$10\r\nnever_here\r\n")
if [[ "$UNSUB_RES" == *"unsubscribe"* ]]; then
    pass "Handled unsubscribing from non-joined channel"
else
    fail "Unsubscribe non-joined" "$UNSUB_RES"
fi

# -------------------------------------------------------------------
# TEST 9: Namespace collision
# -------------------------------------------------------------------
echo "--- Test 9: Namespace Isolation ---"
# Set a key in KV store
send_cmd "*3\r\n\$3\r\nSET\r\n\$4\r\ntest\r\n\$5\r\nvalue\r\n" > /dev/null
# Publish to a channel with same name
send_cmd "*3\r\n\$7\r\nPUBLISH\r\n\$4\r\ntest\r\n\$6\r\nignore\r\n" > /dev/null
# Check if KV value is still there
VAL=$(send_cmd "*2\r\n\$3\r\nGET\r\n\$4\r\ntest\r\n")
if [[ "$VAL" == *"value"* ]]; then
    pass "KV Store and Pub/Sub namespaces are isolated"
else
    fail "Namespace Collision" "$VAL"
fi

# -------------------------------------------------------------------
# TEST 10: Multi-Sub
# -------------------------------------------------------------------
echo "--- Test 10: Multi-channel Subscription ---"
# SUBSCRIBE chan1 chan2 ... chan5
MULTI_SUB=$(send_cmd "*31\r\n\$9\r\nSUBSCRIBE\r\n\$1\r\n1\r\n\$1\r\n2\r\n\$1\r\n3\r\n\$1\r\n4\r\n\$1\r\n5\r\n\$1\r\n6\r\n\$1\r\n7\r\n\$1\r\n8\r\n\$1\r\n9\r\n\$1\r\n10\r\n\$1\r\n11\r\n\$1\r\n12\r\n\$1\r\n13\r\n\$1\r\n14\r\n\$1\r\n15\r\n\$1\r\n16\r\n\$1\r\n17\r\n\$1\r\n18\r\n\$1\r\n19\r\n\$1\r\n20\r\n\$1\r\n21\r\n\$1\r\n22\r\n\$1\r\n23\r\n\$1\r\n24\r\n\$1\r\n25\r\n\$1\r\n26\r\n\$1\r\n27\r\n\$1\r\n28\r\n\$1\r\n29\r\n\$1\r\n30\r\n")
if [[ "$MULTI_SUB" == *"*3"* ]]; then
    pass "Handled bulk subscription to 30 channels"
else
    fail "Bulk subscription" "$MULTI_SUB"
fi

# # -------------------------------------------------------------------
# # TEST 11: Subscription Limit (MAX=2 for testing)
# # -------------------------------------------------------------------
# echo "--- Test 11: Subscription Limit ---"
# # We try to subscribe to 3 channels in one connection. 
# # The 3rd one should fail because we set the limit to 2.
# LIMIT_RES=$(printf "*2\r\n\$9\r\nSUBSCRIBE\r\n\$1\r\nA\r\n*2\r\n\$9\r\nSUBSCRIBE\r\n\$1\r\nB\r\n*2\r\n\$9\r\nSUBSCRIBE\r\n\$1\r\nC\r\n" | nc -w 1 localhost 6379 | tr -d '\r')

# if [[ "$LIMIT_RES" == *"-ERR maximum number"* ]]; then
#     pass "Subscription limit enforced correctly"
# else
#     fail "Subscription limit not enforced" "$LIMIT_RES"
# fi

# -------------------------------------------------------------------
# RESULTS
# -------------------------------------------------------------------
echo ""
echo "=== RESULTS ==="
echo "✅ Passed: $PASS"
echo "❌ Failed: $FAIL"
TOTAL=$((PASS + FAIL))
echo "Total: $TOTAL tests"

if [[ $FAIL -eq 0 ]]; then
    echo "🎉 All tests passed!"
else
    echo "⚠️  Some tests failed."
fi
