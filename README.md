# ⚡ DrutaDB

An event-driven, single-threaded, in-memory key-value data store written in C++23. DrutaDB implements an event-driven networking architecture using POSIX sockets and I/O multiplexing, making it a fast, lightweight, and Redis-like database.

## 🏗️ Architecture & Technical Details

- **Event-Driven Concurrency:** Utilizes the POSIX `poll()` API for I/O multiplexing. This allows the server to handle multiple concurrent TCP client connections efficiently on a single thread without the context-switching overhead of a thread-per-connection model.

- **Custom RESP Parser:** Implements a state-machine-based parser for the **Redis Serialization Protocol (RESP)**. The parser safely processes incoming network byte streams, handling TCP packet fragmentation and partial reads natively.

- **Data Structures:** The underlying datastore relies on standard C++ containers (`std::map`, `std::deque`, `std::string`) wrapped in a custom `DrutaValue` struct to support polymorphic data types.

- **O(1) LRU Eviction:** Implements a **Hash Map + Doubly Linked List** hybrid to track key recency. This architecture ensures that cache hits, updates, and evictions all occur in constant time, maintaining deterministic performance under high memory pressure.

- **Connection Management:** Maintains an active socket watchlist with O(1) connection teardown using back-swapping, minimizing latency during client disconnects.

- **Lazy Eviction:** Supports key Time-to-Live (TTL). Expired keys are lazily evaluated and evicted upon read attempts (`GET`).

- **AOF Persistence & Durability:** Implements Append-Only File (AOF) logging to ensure data durability across server restarts. Mutating commands are serialized into RESP format and committed to disk after successful execution.

- **Smart AOF Rewriting:** Features a growth-based trigger mechanism to prevent unbounded log growth. It compresses the current memory state into a minimal sequence of commands using an atomic file-swap strategy to ensure zero data corruption.

## 🛠️ Supported Commands

DrutaDB currently supports a subset of standard commands over TCP port `6379`:

- `PING`
- `ECHO <message>`
- `SET <key> <val> EX <ttl in seconds>` (Supports `EX` for seconds and `PX` for milliseconds TTL)
- `GET <key>`
- `RPUSH <list_name> <element1> <element2> ...` (List data structure)
- `LRANGE <list_name> <start_index> <end_index>` (Get elements within range from list)
- `LPUSH <list_name> <element1> <element2> ...` (Prepend elements to list)
- `LPOP <list_name> [count]` (Remove and return elements from head)
- `RPOP <list_name> [count]` (Remove and return elements from tail)
- `DEL <key1> <key2> ...` (Delete one or more keys)
- `LLEN <list_name>` (Get list length)
- `FLUSHDB --sure` (remove all keys from store, --sure flag is necessary to prevent accidental deletions)

## 💾 Persistence (AOF)

DrutaDB ensures data persistence using an **Append-Only File (AOF)**:

- **Logging Strategy:** Commands are logged to `data/drutadb.aof` in standard RESP format.
- **State Recovery:** During startup, the server replays the AOF through the internal `RespParser` to restore the memory state.
- **Rewrite Trigger:** To optimize disk space, the server triggers a rewrite when the AOF exceeds 10KB and has doubled in size relative to the previous base.
- **Crash Safety:** The rewrite process generates a temporary file and utilizes `std::filesystem::rename` for an atomic swap, ensuring that a crash during the rewrite never results in data loss or corruption.

## 🧠 LRU Cache & Memory Management

DrutaDB implements a memory-aware eviction policy designed for deterministic performance and strict memory bounds:

- **$O(1)$ LRU Implementation**: Utilizes a dual-layered architecture combining a `std::map` for coordinate lookups and a custom Doubly Linked List for access tracking. This ensures that both cache hits (`touch`) and evictions occur in constant time.
- **Physical Heap Accounting**: Rather than tracking object counts, DrutaDB calculates actual heap consumption. It accounts for `std::string` capacity, `std::deque` internal node pointers (`sizeof(void*)`), and the overhead of the `DrutaNode` structure.
- **Incremental Memory Updates**: Memory deltas for complex types (like LIST) are calculated incrementally during mutation (`LPUSH`, `RPOP`). This avoids costly $O(N)$ full-container scans, maintaining high throughput for large collections.
- **RAII-Based Ownership**: Employs `std::unique_ptr` for primary node ownership within the `kv_store`. The LRU list maintains non-owning observer pointers, ensuring safe, leak-free teardowns during rapid eviction cycles.
- **Memory-Triggered Eviction**: Features a strict 64MB ceiling (configurable). When exceeded, the database executes a synchronous "evict-until-safe" loop, purging the least recently used entries until the heap returns within safe bounds.

## 🏎️ Performance & Benchmarking

DrutaDB is optimized for high-throughput, low-latency workloads. Below are representative results from the internal benchmarking suite (`test/benchmark.py`):

| Metric                                | Result            |
| :------------------------------------ | :---------------- |
| **Single-client Sequential SET**      | **~21,000 req/s** |
| **Pipelined SET (x50 batch)**         | **~97,000 req/s** |
| **Concurrent Clients (10 clients)**   | **~41,500 req/s** |
| **Mixed Keyspace (70/30 Read/Write)** | **~20,800 req/s** |
| **List Operations (RPUSH/LPOP)**      | **~20,000 req/s** |

### Key Optimizations:

- **`everysec` AOF Policy:** Implements a time-buffered disk flush (every 1 second), maintaining a p99 tail latency of 0.31ms and sustained 10k+ req/s under concurrent load.
- **TCP_NODELAY:** Disables Nagle's algorithm to eliminate the delayed-ACK penalty, enabling **4.8x higher throughput** for pipelined operations, compared to sequential baseline.

> [!NOTE]
> **Benchmark Environment:** Tests conducted on an **12th Gen Intel(R) Core(TM) i5-1235U** with peak throughput measured during Turbo Boost. Results may vary based on CPU thermal limits and background process interference.

## 🌍 Supported Environments

Due to its reliance on standard POSIX APIs (`poll`, `socket`, `arpa/inet.h`), DrutaDB is highly portable and natively supported in the following environments:

- **Linux**
- **macOS / FreeBSD**
- **Windows Subsystem for Linux (WSL)**

_(Note: While `poll()` provides excellent portability across Unix-like systems, scaling to extreme concurrent loads (e.g., C10K) would require swapping the multiplexer to OS-specific APIs like Linux's `epoll` or macOS's `kqueue`.)_

## 🚀 Build & Run

**Requirements:**

- CMake 3.13+
- A C++23 compatible compiler (e.g., GCC 13+)
- Pthreads

```bash
mkdir build
cd build
cmake ..
make
./drutadb
```

## 💻 Usage Example

You can interact with DrutaDB using the standard `redis-cli`:

```bash
$ redis-cli -p 6379
127.0.0.1:6379> PING
PONG
127.0.0.1:6379> SET session "active" EX 5
OK
127.0.0.1:6379> GET session
"active"
127.0.0.1:6379> RPUSH mylist "item1" "item2"
(integer) 2
127.0.0.1:6379> LRANGE mylist 0 1
1) "item1"
2) "item2"

... and so on
```

---

## 🧪 Test Scripts

Shell scripts for testing are added in the `test` folder.
Run them from root of folder by following command:

```bash
chmod +x ./test.sh
```

(for initial test, just `./test.sh` works for later runs)

---

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE.txt) file for details.

---

## 🙏 Acknowledgments

This project was built as a deep dive into **low-level systems engineering, specifically focusing on POSIX socket programming, TCP/IP networking, and I/O multiplexing.** A special thanks to the [CodeCrafters](https://codecrafters.io/) "Build Your Own Redis" challenge, which served as an excellent structural guide and testing environment during the initial development and learning phase of this database.

---

## 🤝 Contributing

Contributions are welcome! Open issues or submit pull requests to improve the project! <br>

---

## 🤓 Fun Fact

The inspiration for the name of this project comes from the Sanskrit word **_druta_**, which means Fast or Quick.
