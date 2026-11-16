# Mini-Redis (Persistent Key-Value Store in C)

A single-threaded, event-driven in-memory key-value store built from scratch in C on Linux using `epoll`, progressive rehashing hash tables, an $O(\log N)$ min-heap TTL scheduler, and Append-Only File (AOF) crash recovery.

## Architecture
- **Event Loop (`src/main.c`)**: Non-blocking TCP server powered by Linux `epoll` handling concurrent connections on a single thread.
- **Wire Protocol (`src/protocol.c`)**: Length-prefixed binary framing supporting `SET`, `GET`, `DEL`, and `EXPIRE`.
- **Hash Table (`src/hashtable.c`)**: Custom FNV-1a chained hash table with incremental progressive rehashing (`ht[0]` $\rightarrow$ `ht[1]`) to avoid resize latency spikes.
- **TTL Expiration (`src/heap.c`)**: Cross-indexed binary min-heap enabling $O(\log N)$ expiration updates and dynamic `epoll_wait` timeouts.
- **Persistence (`src/aof.c`)**: Write-ahead Append-Only File (`appendonly.aof`) replayed automatically on startup for crash recovery.

## Quick Start

### Build & Run Server
```bash
make
./mini-redis