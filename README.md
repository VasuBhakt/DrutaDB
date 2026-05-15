# ⚡ DrutaDB

An event-driven, single-threaded, in-memory key-value data store and real-time message broker written in C++23. DrutaDB implements an event-driven networking architecture using POSIX sockets and I/O multiplexing, making it a fast, lightweight, and Redis-compatible database.

## 🏗️ Architecture & Technical Details

- **Event-Driven Concurrency:** Utilizes the POSIX `poll()` API for I/O multiplexing. This allows the server to handle multiple concurrent TCP client connections efficiently on a single thread without the context-switching overhead of a thread-per-connection model.

- **Custom RESP Parser:** Implements a state-machine-based parser for the **Redis Serialization Protocol (RESP)**. The parser safely processes incoming network byte streams, handling TCP packet fragmentation and partial reads natively.

- **Data Structures:** The underlying datastore relies on standard C++ containers (`std::map`, `std::deque`, `std::string`, `std::set`) wrapped in a custom `DrutaValue` struct to support polymorphic data types.

- **O(1) LRU Eviction:** Implements a **Hash Map + Doubly Linked List** hybrid to track key recency. This architecture ensures that cache hits, updates, and evictions all occur in constant time, maintaining deterministic performance under high memory pressure.

- **Connection Management:** Maintains an active socket watchlist with O(1) connection teardown using back-swapping, minimizing latency during client disconnects.

- **Lazy Eviction:** Supports key Time-to-Live (TTL). Expired keys are lazily evaluated and evicted upon read attempts (`GET`).

- **AOF Persistence & Durability:** Implements Append-Only File (AOF) logging to ensure data durability across server restarts. Mutating commands are serialized into RESP format and committed to disk after successful execution.

- **Smart AOF Rewriting:** Features a growth-based trigger mechanism to prevent unbounded log growth. It compresses the current memory state into a minimal sequence of commands using an atomic file-swap strategy to ensure zero data corruption.

- **O(1) Message Broadcasting:** Implements a Pub/Sub engine with $O(1)$ fan-out. Messages are serialized into RESP exactly once and broadcasted directly to all subscribers' socket descriptors, minimizing redundant memory allocations and CPU cycles during high-traffic events.

- **Resource Safety & Cleanup:** Protects against resource exhaustion with a configurable subscription limit per client. Includes an automated cleanup policy that purges subscriber state upon connection teardown to prevent "ghost" subscriptions.

## 🧩 Supported Data Structures and Commands

DrutaDB supports the following data structures as values :

- **STRINGS** : String values implemented using `std::string`. Operations supported :
  - **SET** `<key> <val>` **EX** `<ttl in seconds>` (Used for setting a string value, supports `EX` for seconds ttl, `PX` for milliseconds).
  - **GET** `<key>`

- **LIST** : Values stored in list format, imitating Redis lists. Operations supported :
  - **RPUSH** `<list_name> <element_1> <element_2> ...` (Push into list from the right or back side, initialize list if does not exist).
  - **LPUSH** `<list_name> <element_1> <element_2> ...` (Push into list from the left or front side, initialize list if does not exist).
  - **LPOP** `<list_name> [count]` (Remove and return elements from front).
  - **RPOP** `<list_name> [count]` (Remove and return elements from back).
  - **LRANGE** `<list_name> <start_index> <end_index>` (Get elements within range from list).
  - **LLEN** `<list_name>` (Get list length)

- **HASH** : Stores data in object-like format, imitating Redis Hashes. Implemented using a dual data-structure policy, wherein it is initialized as a `std::vector<std::pair<std::string, std::string>>` and is maintained as a vector until it has more than `DRUTA_MAX_KEY` (usually 64) keys, upon which it is converted into a map structure internally using `std::map<std::string,std::string>`. This was done in order to limit the memory footprint of smaller hashes, inspired by **Redis' Listpack Implementation**.
  - **HSET** `<hash_name> <field_1> <value_1> <field_2> <value_2> ...` (Set elements in hash; create hash if not present. The values are stored as `field_1: value_1 field_2:value_2` under `hash_name` key).
  - **HGET** `<hash_name> <key>` (Get value).
  - **HGETALL** `<hash_name>` (Get all key-value pairs under `hash_name` in the format `<key1> <value1> <key2> <value2>`).
  - **HDEL** `<hash_name> <key_1> <key_2> ...` (deletes keys from hash).
  - **HLEN** `<hash_size>` (number of keys in hash).

- **SET** : Stores unique data in set format, imitating Redis Sets.
  - **SADD** `<set_name> <val_1> <val_2>` (Create or add to a set).
  - **SCARD** `<set_name>` (Returns cardinality of set).
  - **SISMEMBER** `<set_name> <key>` (Checks whether element are part of the set).
  - **SMEMBERS** `<set_name>` (Returns all elements in set).
  - **SMISMEMBER** `<set_name> <key1> <key2> ...`(Checks whether elements are part of the set).
  - **SINTER** `<set1> <set2> <set3>...` (Returns intersection of given sets).
  - **SUNION** `<set1> <set2> <set3>...` (Returns union of given sets).
  - **SDIFF** `<set1> <set2> <set3>...` (Returns difference of first set with other sets).
  - **SDEL** `<val1> <val2> ...` (Deletes values from set).

- **PUB/SUB** : Real-time message broadcasting system.
  - **SUBSCRIBE** `<channel_1> <channel_2> ...` (Subscribes the client to the specified channels).
  - **UNSUBSCRIBE** `[channel_1] [channel_2] ...` (Unsubscribes the client from the given channels, or all channels if none are specified).
  - **PUBLISH** `<channel> <message>` (Posts a message to the given channel and returns the number of subscribers that received it).

Other commands which are supported:

- **PING** - Returns `PONG`.
- **ECHO** `<message>` - Returns `<message>`
- **DEL** `<key_1> <key_2> ...` - Deletes keys from store.
- **FLUSHDB** `--sure` - Removes all keys from store. Use `--sure` flag for safer flushing.

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

## 📡 Real-Time Pub/Sub Engine

DrutaDB features a high-performance messaging engine designed for real-time data broadcasting with a focus on resource safety and efficiency:

- **$O(1)$ Broadcasting Logic**: The engine serializes messages into RESP format exactly **once** per publish operation. It then iterates through the subscriber list and writes the pre-built buffer directly to each socket, eliminating redundant CPU cycles and memory allocations during high-fanout events.
- **Resource Protection (Limits)**: Implements a `MAX_SUBSCRIPTIONS_PER_CLIENT` limit (Default: 128) to protect the server from heap-spraying attacks or runaway client scripts.
- **Zero-Ghost Subscriber Policy**: A dedicated cleanup handler is triggered on socket teardown. It performs a reverse lookup in the client's subscription set to purge all channel entries, ensuring that disconnected clients never leak memory or contaminate the broadcast loop.
- **Idempotent State Management**: Subscription logic verifies state before incrementing counters, ensuring that repeated `SUBSCRIBE` commands for the same channel are handled gracefully without inflating memory usage.

## 🏎️ Performance & Benchmarking

DrutaDB is optimized for high-throughput, low-latency workloads. Below are representative results from the internal benchmarking suite (`test/benchmark.py`):

| Metric                                | Result             |
| :------------------------------------ | :----------------- |
| **Single-client Sequential SET**      | **~21,000 req/s**  |
| **Pipelined SET (x50 batch)**         | **~105,000 req/s** |
| **Concurrent Clients (50 clients)**   | **~33,500 req/s**  |
| **Mixed Keyspace (70/30 Read/Write)** | **~20,800 req/s**  |
| **List Operations (RPUSH/LPOP)**      | **~20,000 req/s**  |
| **Hash Operations (HSET/HGET)**       | **~19,000 req/s**  |

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

Shell scripts and Python Benchmark file for testing are added in the `test` folder.
Run them from root of project by following command:

```bash
./test/test.sh
./test/pubsub_test.sh
./test/eviction_test.sh
python3 test/benchmark.py
```

(for initial shell script test, run `chmod +x <filename>`)

---

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE.txt) file for details.

---

## 🤝 Contributing

Contributions are welcome! Open issues or submit pull requests to improve the project! <br>

---

## 🤓 Fun Fact

The inspiration for the name of this project comes from the Sanskrit word **_druta_**, which means Fast or Quick.
