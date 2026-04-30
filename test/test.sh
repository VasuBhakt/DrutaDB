#!/bin/bash
# DrutaDB Test Suite

send_cmd() {
    # Use printf for more reliable CRLF handling in WSL
    printf "$1" | nc -w 1 localhost 6379
}

echo "Testing PING..."
# Escape the $ with a backslash: \$
RESPONSE=$(send_cmd "*1\r\n\$4\r\nPING\r\n")
if [[ "$RESPONSE" == *"PONG"* ]]; then
    echo "✅ PING Successful"
else
    echo "❌ PING Failed (Got: $RESPONSE)"
fi

echo "Testing SET..."
# Escape all $ signs
SET_RESPONSE=$(send_cmd "*3\r\n\$3\r\nSET\r\n\$3\r\nfoo\r\n\$3\r\nbar\r\n")
if [[ "$SET_RESPONSE" == *"OK"* ]]; then
    echo "✅ SET Successful"
else
    echo "❌ SET Failed (Got: $SET_RESPONSE)"
fi

echo "Testing GET..."
GET_RESPONSE=$(send_cmd "*2\r\n\$3\r\nGET\r\n\$3\r\nfoo\r\n")
if [[ "$GET_RESPONSE" == *"bar"* ]]; then
    echo "✅ GET Successful (Got: 'bar')"
else
    echo "❌ GET Failed (Got: $GET_RESPONSE)"
fi