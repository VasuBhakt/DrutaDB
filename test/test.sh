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

echo "Testing COMMAND DOCS..."
CMD_RESPONSE=$(send_cmd "*2\r\n\$7\r\nCOMMAND\r\n\$4\r\nDOCS\r\n")
if [[ "$CMD_RESPONSE" == *"*0"* ]]; then
    echo "✅ COMMAND DOCS Handled Successfully"
else
    echo "❌ COMMAND DOCS Failed (Got: $CMD_RESPONSE)"
fi

echo "Testing Unknown Command..."
UNKNOWN_RESPONSE=$(send_cmd "*1\r\n\$8\r\nBOGUSCMD\r\n")
if [[ "$UNKNOWN_RESPONSE" == *"-ERR unknown command"* ]]; then
    echo "✅ Unknown Command Handled Successfully"
else
    echo "❌ Unknown Command Failed (Got: $UNKNOWN_RESPONSE)"
fi

echo "Testing RPUSH (List Creation)..."
RPUSH_RES=$(send_cmd "*5\r\n\$5\r\nRPUSH\r\n\$6\r\nmylist\r\n\$1\r\na\r\n\$1\r\nb\r\n\$1\r\nc\r\n")
if [[ "$RPUSH_RES" == *":3"* ]]; then
    echo "✅ RPUSH Successful (Pushed 3 items)"
else
    echo "❌ RPUSH Failed (Got: $RPUSH_RES)"
fi

echo "Testing LRANGE (Normal)..."
LRANGE_RES=$(send_cmd "*4\r\n\$6\r\nLRANGE\r\n\$6\r\nmylist\r\n\$1\r\n0\r\n\$1\r\n1\r\n")
if [[ "$LRANGE_RES" == *"a"* && "$LRANGE_RES" == *"b"* ]]; then
    echo "✅ LRANGE Normal Successful"
else
    echo "❌ LRANGE Normal Failed (Got: $LRANGE_RES)"
fi

echo "Testing LRANGE (Tricky: Negative Indices)..."
LRANGE_NEG_RES=$(send_cmd "*4\r\n\$6\r\nLRANGE\r\n\$6\r\nmylist\r\n\$2\r\n-2\r\n\$2\r\n-1\r\n")
if [[ "$LRANGE_NEG_RES" == *"b"* && "$LRANGE_NEG_RES" == *"c"* ]]; then
    echo "✅ LRANGE Negative Indices Successful"
else
    echo "❌ LRANGE Negative Indices Failed (Got: $LRANGE_NEG_RES)"
fi

echo "Testing Type Collision (Tricky: RPUSH to String Key)..."
COLLISION_RES=$(send_cmd "*3\r\n\$5\r\nRPUSH\r\n\$3\r\nfoo\r\n\$4\r\nitem\r\n")
if [[ "$COLLISION_RES" == *"-WRONGTYPE"* ]]; then
    echo "✅ Type Collision Handled Safely"
else
    echo "❌ Type Collision Failed (Got: $COLLISION_RES)"
fi

echo "Testing LRANGE Exception (Tricky: Non-numeric limit)..."
LRANGE_EXC_RES=$(send_cmd "*4\r\n\$6\r\nLRANGE\r\n\$6\r\nmylist\r\n\$1\r\nx\r\n\$1\r\ny\r\n")
if [[ "$LRANGE_EXC_RES" == *"-WRONGTYPE Non-numeric"* ]]; then
    echo "✅ LRANGE Non-numeric Exception Handled Safely"
else
    echo "❌ LRANGE Exception Failed (Got: $LRANGE_EXC_RES)"
fi

echo "Testing PING (Tricky: Extra Arguments)..."
PING_EXTRA_RES=$(send_cmd "*3\r\n\$4\r\nPING\r\n\$5\r\nhello\r\n\$5\r\nworld\r\n")
if [[ "$PING_EXTRA_RES" == *"PONG"* ]]; then
    echo "✅ PING Extra Args Handled Safely"
else
    echo "❌ PING Extra Args Failed"
fi

echo "Testing ECHO (Tricky: Missing Arguments)..."
ECHO_MISSING_RES=$(send_cmd "*1\r\n\$4\r\nECHO\r\n")
if [[ "$ECHO_MISSING_RES" == *"-ERR"* ]]; then
    echo "✅ ECHO Missing Args Handled Safely"
else
    echo "❌ ECHO Missing Args Failed"
fi

echo "Testing SET (Tricky: Missing Value)..."
SET_MISSING_RES=$(send_cmd "*2\r\n\$3\r\nSET\r\n\$3\r\nfoo\r\n")
if [[ "$SET_MISSING_RES" == *"-ERR"* ]]; then
    echo "✅ SET Missing Value Handled Safely"
else
    echo "❌ SET Missing Value Failed (Warning: Server might have crashed!)"
fi

echo "Testing SET (Tricky: Non-numeric Expiry)..."
SET_EXC_RES=$(send_cmd "*5\r\n\$3\r\nSET\r\n\$3\r\nfoo\r\n\$3\r\nbar\r\n\$2\r\nEX\r\n\$3\r\nabc\r\n")
if [[ "$SET_EXC_RES" == *"-ERR"* || "$SET_EXC_RES" == *"-WRONGTYPE"* ]]; then
    echo "✅ SET Non-numeric Expiry Handled Safely"
else
    echo "❌ SET Non-numeric Expiry Failed (Warning: Server might have crashed!)"
fi

echo "Testing GET (Tricky: Wrong Type)..."
GET_WRONGTYPE_RES=$(send_cmd "*2\r\n\$3\r\nGET\r\n\$6\r\nmylist\r\n")
if [[ "$GET_WRONGTYPE_RES" == *"-WRONGTYPE"* ]]; then
    echo "✅ GET Wrong Type Handled Safely"
else
    echo "❌ GET Wrong Type Failed (Got: $GET_WRONGTYPE_RES)"
fi

echo "Testing GET (Tricky: Missing Key Argument)..."
GET_MISSING_RES=$(send_cmd "*1\r\n\$3\r\nGET\r\n")
if [[ "$GET_MISSING_RES" == *"-ERR"* ]]; then
    echo "✅ GET Missing Key Handled Safely"
else
    echo "❌ GET Missing Key Failed (Warning: Server might have crashed!)"
fi