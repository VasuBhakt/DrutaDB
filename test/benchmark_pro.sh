#!/bin/bash

# Ensure DrutaDB is running before executing this script
echo "=========================================================="
echo "          DrutaDB High-Performance Benchmark Suite        "
echo "=========================================================="
echo "NOTE: This suite uses 'redis-benchmark' to test the true "
echo "C++ epoll throughput without Python GIL bottlenecks."
echo "=========================================================="
echo ""

# Helper: run redis-benchmark and show only throughput + latency summary
run_bench() {
  redis-benchmark "$@" 2>&1 | grep -E "(^[A-Z]+:|throughput summary|avg.*min.*p50|^\s+[0-9]+\.[0-9]+)"
}

echo "[1/4] Single-client Sequential (No Pipelining)"
echo "  redis-benchmark -p 6379 -c 1 -n 100000 -t set,get,lpush,lpop,sadd,hset"
run_bench -p 6379 -c 1 -n 100000 -t set,get,lpush,lpop,sadd,hset
echo ""

echo "[2/4] High Concurrency (1,000 Clients, No Pipelining)"
echo "  redis-benchmark -p 6379 -c 1000 -n 100000 -t set,get,lpush,lpop,sadd,hset"
run_bench -p 6379 -c 1000 -n 100000 -t set,get,lpush,lpop,sadd,hset
echo ""

echo "[3/4] Production Pipelining (100 concurrent, 100 pipeline batch)"
echo "  redis-benchmark -p 6379 -c 100 -n 500000 -P 100 -t set,get,lpush,lpop,sadd,hset"
run_bench -p 6379 -c 100 -n 500000 -P 100 -t set,get,lpush,lpop,sadd,hset
echo ""

echo "[4/4] Extreme Stress Test (1,000 clients, 99 pipeline batch)"
echo "  redis-benchmark -p 6379 -c 1000 -n 1000000 -P 99 -t set,get,lpush,lpop,sadd,hset"
run_bench -p 6379 -c 1000 -n 1000000 -P 99 -t set,get,lpush,lpop,sadd,hset
echo ""

echo "=========================================================="
echo "Benchmark Complete."
