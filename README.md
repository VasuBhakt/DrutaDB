# DrutaDB

A high-performance, single-threaded, in-memory key-value data store written in C++23. DrutaDB implements an event-driven networking architecture using POSIX sockets and I/O multiplexing, making it a fast, lightweight, and Redis-like database.

## Architecture & Technical Details

- **Event-Driven Concurrency:** Utilizes the POSIX `poll()` API for I/O multiplexing. This allows the server to handle multiple concurrent TCP client connections efficiently on a single thread without the context-switching overhead of a thread-per-connection model.

- **Custom RESP Parser:** Implements a state-machine-based parser for the **Redis Serialization Protocol (RESP)**. The parser safely processes incoming network byte streams, handling TCP packet fragmentation and partial reads natively.

- **Data Structures:** The underlying datastore relies on standard C++ containers (`std::map`, `std::deque`, `std::string`) wrapped in a custom `RedisValue` struct to support multiple data types.

- **Connection Management:** Maintains an active socket watchlist with O(1) connection teardown using back-swapping, minimizing latency during client disconnects.

- **Lazy Eviction:** Supports key Time-to-Live (TTL). Expired keys are lazily evaluated and evicted upon read attempts (`GET`).

## Supported Commands

DrutaDB currently supports a subset of standard commands over TCP port `6379`:

- `PING`
- `ECHO`
- `SET` (Supports `EX` for seconds and `PX` for milliseconds TTL)
- `GET`
- `RPUSH`

## Supported Environments

Due to its reliance on standard POSIX APIs (`poll`, `socket`, `arpa/inet.h`), DrutaDB is highly portable and natively supported in the following environments:

- **Linux**
- **macOS / FreeBSD**
- **Windows Subsystem for Linux (WSL)**

_(Note: While `poll()` provides excellent portability across Unix-like systems, scaling to extreme concurrent loads (e.g., C10K) would require swapping the multiplexer to OS-specific APIs like Linux's `epoll` or macOS's `kqueue`.)_

## Build & Run

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

## Usage Example

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
```
