"""
DrutaDB Benchmark Suite
Tests: single-client throughput, pipelining, multi-client concurrency,
mixed workloads, key distribution, and latency percentiles.

Usage:
    python benchmark_drutadb.py [host] [port]
    python benchmark_drutadb.py 127.0.0.1 6379
"""

import socket
import time
import sys
import threading
import random
import string
import statistics
from concurrent.futures import ThreadPoolExecutor, as_completed


# ─── Config ───────────────────────────────────────────────────────────────────

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 6379

TOTAL_REQUESTS = 10_000
NUM_CLIENTS = 50
PIPELINE_SIZE = 50
WARMUP_REQUESTS = 500
KEY_SPACE = 5_000  # number of distinct keys

DIVIDER = "─" * 50


# ─── RESP helpers ─────────────────────────────────────────────────────────────


def encode_cmd(*args) -> bytes:
    """Encode a command into RESP array format."""
    out = f"*{len(args)}\r\n".encode()
    for arg in args:
        arg = str(arg)
        out += f"${len(arg)}\r\n{arg}\r\n".encode()
    return out


def recv_exact(sock: socket.socket, n: int) -> bytes:
    """Read exactly n bytes from a socket."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Server closed connection unexpectedly")
        buf += chunk
    return buf


def recv_response(f) -> bytes:
    """
    Read one complete RESP response from a file-like object.
    Handles: +simple  -error  :integer  $bulk  *array (shallow)
    Returns the raw bytes (including the type byte and CRLF).
    """
    line = f.readline()
    if not line:
        raise ConnectionError("Server closed connection unexpectedly")

    kind = chr(line[0])

    if kind in ("+", "-", ":"):
        return line

    if kind == "$":
        length = int(line[1:-2])
        if length == -1:
            return line  # null bulk
        data = f.read(length + 2)
        return line + data

    if kind == "*":
        count = int(line[1:-2])
        result = line
        for _ in range(count):
            el = recv_response(f)
            result += el
        return result

    raise ValueError(f"Unknown RESP type byte: {kind!r}")


def make_connection() -> socket.socket:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((HOST, PORT))
        return s
    except ConnectionRefusedError:
        print(f"\n✗  Cannot connect to DrutaDB at {HOST}:{PORT}")
        print("   Is the server running?\n")
        sys.exit(1)


def random_key() -> str:
    return f"key:{random.randint(0, KEY_SPACE - 1)}"


def random_value(length: int = 8) -> str:
    return "".join(random.choices(string.ascii_letters, k=length))


# ─── Latency stats helper ─────────────────────────────────────────────────────


def latency_stats(samples: list[float]) -> dict:
    s = sorted(samples)
    n = len(s)
    return {
        "min_ms": round(s[0] * 1000, 3),
        "p50_ms": round(s[n // 2] * 1000, 3),
        "p95_ms": round(s[int(n * 0.95)] * 1000, 3),
        "p99_ms": round(s[int(n * 0.99)] * 1000, 3),
        "max_ms": round(s[-1] * 1000, 3),
        "mean_ms": round(statistics.mean(s) * 1000, 3),
    }


def print_latency(stats: dict):
    print(
        f"  Latency  min={stats['min_ms']}ms  "
        f"p50={stats['p50_ms']}ms  "
        f"p95={stats['p95_ms']}ms  "
        f"p99={stats['p99_ms']}ms  "
        f"max={stats['max_ms']}ms"
    )


# ─── Warmup ───────────────────────────────────────────────────────────────────


def warmup():
    print(f"Warming up ({WARMUP_REQUESTS} requests)...", end=" ", flush=True)
    s = make_connection()
    f = s.makefile("rb")
    cmd = encode_cmd("SET", "warmup", "val")
    for _ in range(WARMUP_REQUESTS):
        s.sendall(cmd)
        recv_response(f)
    s.close()
    print("done\n")


# ─── Benchmark 1: Single-client sequential throughput ─────────────────────────


def bench_single_client():
    print(f"[1/6] Single-client sequential SET  ({TOTAL_REQUESTS:,} requests)")
    s = make_connection()
    f = s.makefile("rb")
    cmd = encode_cmd("SET", "bench:single", "value")
    latencies = []

    t0 = time.perf_counter()
    for _ in range(TOTAL_REQUESTS):
        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(f)
        latencies.append(time.perf_counter() - ts)
    total = time.perf_counter() - t0
    s.close()

    rps = TOTAL_REQUESTS / total
    stats = latency_stats(latencies)
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    print_latency(stats)
    print()
    return rps


# ─── Benchmark 2: Pipelining ──────────────────────────────────────────────────


def bench_pipeline():
    print(f"[2/6] Pipelining  (pipeline={PIPELINE_SIZE}, total={TOTAL_REQUESTS:,})")
    s = make_connection()
    f = s.makefile("rb")
    cmd = encode_cmd("SET", "bench:pipe", "value")
    pipeline = cmd * PIPELINE_SIZE
    batches = TOTAL_REQUESTS // PIPELINE_SIZE

    t0 = time.perf_counter()
    for _ in range(batches):
        s.sendall(pipeline)
        for _ in range(PIPELINE_SIZE):
            recv_response(f)
    total = time.perf_counter() - t0
    s.close()

    actual = batches * PIPELINE_SIZE
    rps = actual / total
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    print()
    return rps


# ─── Benchmark 3: Concurrent clients ─────────────────────────────────────────


def _worker(requests_per_client: int, workload: str) -> list[float]:
    """Single worker thread. Returns [per-request latencies]."""
    s = make_connection()
    f = s.makefile("rb")
    latencies = []

    for i in range(requests_per_client):
        key = random_key()
        if workload == "set":
            cmd = encode_cmd("SET", key, random_value())
        elif workload == "get":
            cmd = encode_cmd("GET", key)
        else:  # mixed 80/20 read-write
            if random.random() < 0.4:
                cmd = encode_cmd("SET", key, random_value())
            else:
                cmd = encode_cmd("GET", key)

        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(f)
        latencies.append(time.perf_counter() - ts)

    s.close()
    return latencies


def bench_concurrent(workload: str = "set"):
    rpc = TOTAL_REQUESTS // NUM_CLIENTS
    label = {"set": "SET only", "get": "GET only", "mixed": "60% GET / 40% SET"}[
        workload
    ]
    print(f"[3/6] {NUM_CLIENTS} concurrent clients  –  {label}  ({rpc:,} req/client)")

    all_latencies = []
    t0 = time.perf_counter()
    from concurrent.futures import ProcessPoolExecutor

    with ProcessPoolExecutor(max_workers=NUM_CLIENTS) as pool:
        futures = [pool.submit(_worker, rpc, workload) for _ in range(NUM_CLIENTS)]
        for f in as_completed(futures):
            all_latencies.extend(f.result())
    total = time.perf_counter() - t0

    actual = len(all_latencies)
    rps = actual / total
    stats = latency_stats(all_latencies)
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    print_latency(stats)
    print()
    return rps


# ─── Benchmark 4: Mixed workload across large key space ───────────────────────


def bench_mixed_keyspace():
    print(
        f"[4/6] Mixed workload  –  {KEY_SPACE:,}-key space  ({TOTAL_REQUESTS:,} requests)"
    )
    # Pre-populate with random keys so GETs can hit real data
    print(f"  Pre-populating {KEY_SPACE} keys...", end=" ", flush=True)
    s = make_connection()
    f = s.makefile("rb")
    for i in range(KEY_SPACE):
        s.sendall(encode_cmd("SET", f"key:{i}", random_value()))
        recv_response(f)
    print("done")

    latencies_set = []
    latencies_get = []
    latencies_list = []
    latencies_hash = []

    t0 = time.perf_counter()
    for _ in range(TOTAL_REQUESTS):
        key = random_key()
        r = random.random()
        if r < 0.2:  # 20% SET
            cmd = encode_cmd("SET", key, random_value())
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(f)
            latencies_set.append(time.perf_counter() - ts)
        elif r < 0.3:  # 10% RPUSH
            cmd = encode_cmd("RPUSH", f"list:{key}", random_value())
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(f)
            latencies_list.append(time.perf_counter() - ts)
        elif r < 0.35:  # 5% LPOP
            cmd = encode_cmd("LPOP", f"list:{key}")
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(f)
            latencies_list.append(time.perf_counter() - ts)
        elif r < 0.40:  # 5% HSET
            cmd = encode_cmd("HSET", f"hash:{key}", "field", random_value())
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(f)
            latencies_hash.append(time.perf_counter() - ts)
        elif r < 0.45:  # 5% HGET
            cmd = encode_cmd("HGET", f"hash:{key}", "field")
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(f)
            latencies_hash.append(time.perf_counter() - ts)
        else:  # 55% GET
            cmd = encode_cmd("GET", key)
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(f)
            latencies_get.append(time.perf_counter() - ts)
    total = time.perf_counter() - t0
    s.close()

    rps = TOTAL_REQUESTS / total
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    if latencies_set:
        st = latency_stats(latencies_set)
        print(
            f"  SET  p50={st['p50_ms']}ms  p99={st['p99_ms']}ms  ({len(latencies_set)} ops)"
        )
    if latencies_get:
        gt = latency_stats(latencies_get)
        print(
            f"  GET  p50={gt['p50_ms']}ms  p99={gt['p99_ms']}ms  ({len(latencies_get)} ops)"
        )
    if latencies_list:
        lt = latency_stats(latencies_list)
        print(
            f"  LIST p50={lt['p50_ms']}ms  p99={lt['p99_ms']}ms  ({len(latencies_list)} ops)"
        )
    if latencies_hash:
        ht = latency_stats(latencies_hash)
        print(
            f"  HASH p50={ht['p50_ms']}ms  p99={ht['p99_ms']}ms  ({len(latencies_hash)} ops)"
        )
    print()
    return rps


# ─── Benchmark 5: List Operations ─────────────────────────────────────────────


def bench_list_operations():
    print(f"[5/6] List Operations  –  RPUSH and LPOP  ({TOTAL_REQUESTS:,} requests)")
    s = make_connection()
    f = s.makefile("rb")
    list_key = "bench:list"

    # Cleanup if exists
    s.sendall(encode_cmd("DEL", list_key))
    recv_response(f)

    latencies_rpush = []
    latencies_lpop = []

    t0 = time.perf_counter()
    # 1. Benchmark RPUSH
    for _ in range(TOTAL_REQUESTS // 2):
        cmd = encode_cmd("RPUSH", list_key, random_value())
        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(f)
        latencies_rpush.append(time.perf_counter() - ts)

    # 2. Benchmark LPOP
    for _ in range(TOTAL_REQUESTS // 2):
        cmd = encode_cmd("LPOP", list_key)
        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(f)
        latencies_lpop.append(time.perf_counter() - ts)

    total = time.perf_counter() - t0
    s.close()

    rps = TOTAL_REQUESTS / total
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    if latencies_rpush:
        st = latency_stats(latencies_rpush)
        print(
            f"  RPUSH  p50={st['p50_ms']}ms  p99={st['p99_ms']}ms  ({len(latencies_rpush)} ops)"
        )
    if latencies_lpop:
        lt = latency_stats(latencies_lpop)
        print(
            f"  LPOP   p50={lt['p50_ms']}ms  p99={lt['p99_ms']}ms  ({len(latencies_lpop)} ops)"
        )
    print()
    return rps


# ─── Benchmark 6: Hash Operations ─────────────────────────────────────────────


def bench_hash_operations():
    print(f"[6/6] Hash Operations  –  HSET and HGET  ({TOTAL_REQUESTS:,} requests)")
    s = make_connection()
    f = s.makefile("rb")
    hash_key = "bench:hash"

    # Cleanup if exists
    s.sendall(encode_cmd("DEL", hash_key))
    recv_response(f)

    latencies_hset = []
    latencies_hget = []

    t0 = time.perf_counter()
    # 1. Benchmark HSET
    for i in range(TOTAL_REQUESTS // 2):
        field = f"f:{i}"
        cmd = encode_cmd("HSET", hash_key, field, random_value())
        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(f)
        latencies_hset.append(time.perf_counter() - ts)

    # 2. Benchmark HGET
    for i in range(TOTAL_REQUESTS // 2):
        field = f"f:{i}"
        cmd = encode_cmd("HGET", hash_key, field)
        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(f)
        latencies_hget.append(time.perf_counter() - ts)

    total = time.perf_counter() - t0
    s.close()

    rps = TOTAL_REQUESTS / total
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    if latencies_hset:
        st = latency_stats(latencies_hset)
        print(
            f"  HSET   p50={st['p50_ms']}ms  p99={st['p99_ms']}ms  ({len(latencies_hset)} ops)"
        )
    if latencies_hget:
        lt = latency_stats(latencies_hget)
        print(
            f"  HGET   p50={lt['p50_ms']}ms  p99={lt['p99_ms']}ms  ({len(latencies_hget)} ops)"
        )
    print()
    return rps


# ─── Main ─────────────────────────────────────────────────────────────────────


def main():
    print(f"\nDrutaDB Benchmark  →  {HOST}:{PORT}")
    print(DIVIDER)

    warmup()

    r1 = bench_single_client()
    r2 = bench_pipeline()
    r3 = bench_concurrent(workload="set")
    bench_concurrent(workload="mixed")
    r4 = bench_mixed_keyspace()
    r5 = bench_list_operations()
    r6 = bench_hash_operations()

    print(DIVIDER)
    print("Summary")
    print(f"  Single-client sequential :  {r1:>10,.0f} req/s")
    print(f"  Pipelining (x{PIPELINE_SIZE})          :  {r2:>10,.0f} req/s")
    print(f"  {NUM_CLIENTS} concurrent clients (SET) :  {r3:>10,.0f} req/s")
    print(f"  Mixed keyspace           :  {r4:>10,.0f} req/s")
    print(f"  List Operations (RPUSH/LPOP) :  {r5:>10,.0f} req/s")
    print(f"  Hash Operations (HSET/HGET) :  {r6:>10,.0f} req/s")
    if r1 > 0:
        print(f"\n  Pipeline speedup  : {r2/r1:.1f}x")
        print(f"  Concurrency speedup: {r3/r1:.1f}x")
    print(DIVIDER + "\n")


if __name__ == "__main__":
    main()
