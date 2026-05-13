#!/bin/bash
# DrutaDB LRU Eviction Demonstration
# Limit is 100KB. Using 5 large keys (30KB each).

echo "--- STARTING EVICTION DEMO ---"
echo "Target: 100KB Memory Limit"
echo ""

# Generate a 30KB string robustly
# head -c is very reliable for generating exact byte counts
DATA=$(head -c 30000 < /dev/zero | tr '\0' 'A')

echo "Sending 5 keys (30KB each)..."
for i in {1..5}; do
    KEY="large:$i"
    KLEN=${#KEY}
    
    # Send via a single pipe to nc
    printf "*3\r\n\$3\r\nSET\r\n\$${KLEN}\r\n${KEY}\r\n\$30000\r\n${DATA}\r\n" | nc -N localhost 6379 > /dev/null
    
    echo "  Sent key '$KEY' (30KB)"
    
    # Check stats after every push so we can see it grow
    printf "*1\r\n\$8\r\nMEMSTATS\r\n" | nc -N -w 1 localhost 6379 | grep "Usage"
done

echo ""
echo "Waiting for server to finalize memory cleanup..."
# Send a dummy PING to trigger any pending eviction, then check
printf "*1\r\n\$4\r\nPING\r\n" | nc -N -w 1 localhost 6379 > /dev/null
sleep 0.5

echo "Checking eviction results..."

# Key 1 and 2 should be gone (30KB * 5 = 150KB, which is 50KB over the limit)
CHECK_1=$(printf "*2\r\n\$3\r\nGET\r\n\$7\r\nlarge:1\r\n" | nc -N -w 1 localhost 6379)
if [[ "$CHECK_1" == *'$-1'* ]]; then
    echo "✅ SUCCESS: 'large:1' was evicted correctly!"
else
    echo "❌ FAILURE: 'large:1' is still there. Eviction didn't trigger."
fi

# Key 5 should definitely be there
CHECK_5=$(printf "*2\r\n\$3\r\nGET\r\n\$7\r\nlarge:5\r\n" | nc -N -w 1 localhost 6379)
if [[ "$CHECK_5" == *'A'* ]]; then
    echo "✅ SUCCESS: 'large:5' (most recent) is still present."
else
    echo "❌ FAILURE: 'large:5' was evicted unexpectedly."
fi

echo ""
echo "Demo complete."
