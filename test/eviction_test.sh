#!/bin/bash
# DrutaDB LRU Eviction Demonstration
# Limit is 100KB. We will push 4x 30KB keys.

send_raw() {
    echo -ne "$1" | nc -w 1 localhost 6379
}

echo "--- STARTING EVICTION DEMO ---"
echo "Target: 100KB Memory Limit"
echo ""

# 30,000 character string (~30KB)
DATA=$(printf 'A%.0s' {1..30000})

for i in {1..4}; do
    echo "Pusing 30KB key 'demo:$i'..."
    send_raw "*3\r\n\$3\r\nSET\r\n\$6\r\ndemo:$i\r\n\$30000\r\n$DATA\r\n" > /dev/null
    sleep 0.5
done

echo ""
echo "Checking results..."

# Key 1 should be gone
CHECK_1=$(send_raw "*2\r\n\$3\r\nGET\r\n\$6\r\ndemo:1\r\n")
if [[ "$CHECK_1" == *"$-1"* ]]; then
    echo "✅ SUCCESS: 'demo:1' was evicted (LRU working!)"
else
    echo "❌ FAILURE: 'demo:1' is still there. Current memory calculation might be off."
fi

# Key 4 should definitely be there
CHECK_4=$(send_raw "*2\r\n\$3\r\nGET\r\n\$6\r\ndemo:4\r\n")
if [[ "$CHECK_4" == *"A"* ]]; then
    echo "✅ SUCCESS: 'demo:4' (most recent) is still present."
else
    echo "❌ FAILURE: 'demo:4' was evicted unexpectedly."
fi

echo ""
echo "Demo complete."
