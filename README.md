# ⚡ DrutaDB

An event-driven, single-threaded, in-memory key-value data store written in C++23. DrutaDB implements an event-driven networking architecture using POSIX sockets and I/O multiplexing, making it a fast, lightweight, and Redis-like database.

## 🏗️ Architecture & Technical Details

- **Event-Driven Concurrency:** Utilizes the POSIX `poll()` API for I/O multiplexing. This allows the server to handle multiple concurrent TCP client connections efficiently on a single thread without the context-switching overhead of a thread-per-connection model.

- **Custom RESP Parser:** Implements a state-machine-based parser for the **Redis Serialization Protocol (RESP)**. The parser safely processes incoming network byte streams, handling TCP packet fragmentation and partial reads natively.

- **Data Structures:** The underlying datastore relies on standard C++ containers (`std::map`, `std::deque`, `std::string`) wrapped in a custom `RedisValue` struct to support multiple data types.

- **Connection Management:** Maintains an active socket watchlist with O(1) connection teardown using back-swapping, minimizing latency during client disconnects.

- **Lazy Eviction:** Supports key Time-to-Live (TTL). Expired keys are lazily evaluated and evicted upon read attempts (`GET`).

## 🛠️ Supported Commands

DrutaDB currently supports a subset of standard commands over TCP port `6379`:

- `PING`
- `ECHO <message>`
- `SET <key> <val> EX <ttl in seconds>` (Supports `EX` for seconds and `PX` for milliseconds TTL)
- `GET <key>`
- `RPUSH <list_name> <element1> <element2> ...` (List data structure)
- `LRANGE <list_name> <start_index> <end_index>` (Get elements within range from list)

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

```
--- 

## 🧪 Test Scripts

Shell scripts for testing are added in the ```test``` folder.
Run them from root of folder by following command:

```bash
chmod +x ./test.sh
```
(for initial test, just ```./test.sh``` works for later runs)

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
