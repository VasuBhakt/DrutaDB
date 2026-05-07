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
NUM_CLIENTS = 10
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


def recv_response(sock: socket.socket) -> bytes:
    """
    Read one complete RESP response.
    Handles: +simple  -error  :integer  $bulk  *array (shallow)
    Returns the raw bytes (including the type byte and CRLF).
    """
    buf = b""
    while b"\r\n" not in buf:
        buf += sock.recv(256)

    line, rest = buf.split(b"\r\n", 1)
    kind = chr(line[0])

    if kind in ("+", "-", ":"):
        return line + b"\r\n"

    if kind == "$":
        length = int(line[1:])
        if length == -1:
            return line + b"\r\n"  # null bulk
        needed = length + 2 - len(rest)
        if needed > 0:
            rest += recv_exact(sock, needed)
        return line + b"\r\n" + rest[: length + 2]

    if kind == "*":
        count = int(line[1:])
        result = line + b"\r\n"
        sock_buf = rest
        for _ in range(count):
            # read each element; minimal – only handles bulk strings here
            while b"\r\n" not in sock_buf:
                sock_buf += sock.recv(256)
            el_line, sock_buf = sock_buf.split(b"\r\n", 1)
            result += el_line + b"\r\n"
            if chr(el_line[0]) == "$":
                el_len = int(el_line[1:])
                if el_len != -1:
                    needed = el_len + 2 - len(sock_buf)
                    if needed > 0:
                        sock_buf += recv_exact(sock, needed)
                    result += sock_buf[: el_len + 2]
                    sock_buf = sock_buf[el_len + 2 :]
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
    cmd = encode_cmd("SET", "warmup", "val")
    for _ in range(WARMUP_REQUESTS):
        s.sendall(cmd)
        recv_response(s)
    s.close()
    print("done\n")


# ─── Benchmark 1: Single-client sequential throughput ─────────────────────────


def bench_single_client():
    print(f"[1/4] Single-client sequential SET  ({TOTAL_REQUESTS:,} requests)")
    s = make_connection()
    cmd = encode_cmd("SET", "bench:single", "value")
    latencies = []

    t0 = time.perf_counter()
    for _ in range(TOTAL_REQUESTS):
        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(s)
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
    print(f"[2/4] Pipelining  (pipeline={PIPELINE_SIZE}, total={TOTAL_REQUESTS:,})")
    s = make_connection()
    cmd = encode_cmd("SET", "bench:pipe", "value")
    pipeline = cmd * PIPELINE_SIZE
    batches = TOTAL_REQUESTS // PIPELINE_SIZE
    ok_resp = b"+OK\r\n"

    t0 = time.perf_counter()
    for _ in range(batches):
        s.sendall(pipeline)
        received = 0
        buf = b""
        while received < PIPELINE_SIZE:
            buf += s.recv(4096)
            received += buf.count(ok_resp)
            buf = buf[buf.rfind(ok_resp) + 5 :] if ok_resp in buf else buf
    total = time.perf_counter() - t0
    s.close()

    actual = batches * PIPELINE_SIZE
    rps = actual / total
    print(f"  Throughput  {rps:,.0f} req/s  |  total {total:.3f}s")
    print(f"  Pipelining speedup vs sequential: measured separately above")
    print()
    return rps


# ─── Benchmark 3: Concurrent clients ─────────────────────────────────────────


def _worker(requests_per_client: int, workload: str) -> tuple[float, list[float]]:
    """Single worker thread. Returns (elapsed_seconds, [per-request latencies])."""
    s = make_connection()
    latencies = []

    for i in range(requests_per_client):
        key = random_key()
        if workload == "set":
            cmd = encode_cmd("SET", key, random_value())
        elif workload == "get":
            cmd = encode_cmd("GET", key)
        else:  # mixed 80/20 read-write
            if random.random() < 0.2:
                cmd = encode_cmd("SET", key, random_value())
            else:
                cmd = encode_cmd("GET", key)

        ts = time.perf_counter()
        s.sendall(cmd)
        recv_response(s)
        latencies.append(time.perf_counter() - ts)

    s.close()
    return latencies


def bench_concurrent(workload: str = "set"):
    rpc = TOTAL_REQUESTS // NUM_CLIENTS
    label = {"set": "SET only", "get": "GET only", "mixed": "80% GET / 20% SET"}[
        workload
    ]
    print(f"[3/4] {NUM_CLIENTS} concurrent clients  –  {label}  ({rpc:,} req/client)")

    all_latencies = []
    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=NUM_CLIENTS) as pool:
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
        f"[4/4] Mixed workload  –  {KEY_SPACE:,}-key space  ({TOTAL_REQUESTS:,} requests)"
    )
    # Pre-populate with random keys so GETs can hit real data
    print("  Pre-populating keys...", end=" ", flush=True)
    s = make_connection()
    for i in range(min(KEY_SPACE, 2000)):
        s.sendall(encode_cmd("SET", f"key:{i}", random_value()))
        recv_response(s)
    print("done")

    latencies_set = []
    latencies_get = []

    t0 = time.perf_counter()
    for _ in range(TOTAL_REQUESTS):
        key = random_key()
        if random.random() < 0.2:
            cmd = encode_cmd("SET", key, random_value())
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(s)
            latencies_set.append(time.perf_counter() - ts)
        else:
            cmd = encode_cmd("GET", key)
            ts = time.perf_counter()
            s.sendall(cmd)
            recv_response(s)
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

    print(DIVIDER)
    print("Summary")
    print(f"  Single-client sequential :  {r1:>10,.0f} req/s")
    print(f"  Pipelining (x{PIPELINE_SIZE})          :  {r2:>10,.0f} req/s")
    print(f"  {NUM_CLIENTS} concurrent clients (SET) :  {r3:>10,.0f} req/s")
    print(f"  Mixed keyspace           :  {r4:>10,.0f} req/s")
    if r1 > 0:
        print(f"\n  Pipeline speedup  : {r2/r1:.1f}x")
        print(f"  Concurrency speedup: {r3/r1:.1f}x")
    print(DIVIDER + "\n")


if __name__ == "__main__":
    main()
