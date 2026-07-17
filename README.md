# Multiplayer Snake Game Server

A systems programming project that implements a multiplayer Snake game server in C. The project focuses on the lower-level mechanics behind real-time networked applications: TCP sockets, binary protocols, concurrent client handling, shared game state, and resource management in a Unix-like environment.

## Project Goals

The goal of this project was to build a multiplayer game server from the systems layer up. Instead of relying on a web framework, event framework, or high-level runtime, the server directly uses POSIX APIs to manage networking and concurrency.

The project was designed to answer questions such as:

- How does a TCP server accept and manage multiple simultaneous clients?
- How should shared game state be protected when many clients send input concurrently?
- How can a compact binary protocol represent game events and board state?
- How should a server handle partial reads, partial writes, disconnects, invalid messages, and shutdown paths?

## What It Does

The server hosts a shared Snake game board for multiple clients. Each client connects over TCP, sends a JOIN message, receives an assigned player ID, and then sends direction updates while the server manages the game state.

The server is responsible for:

- Accepting TCP client connections.
- Assigning each client a snake/player ID.
- Maintaining a shared board with snake positions, apples, growth, collisions, and player death.
- Receiving movement commands from clients.
- Advancing the game on timed ticks.
- Broadcasting serialized game-state updates to connected clients.
- Cleaning up client state when players leave or disconnect.

## Architecture

The implementation uses a thread-per-client design:

- The main server thread creates the listening socket and accepts incoming TCP connections.
- Each accepted client is handled by a dedicated `pthread`.
- A separate game loop thread advances the board at fixed intervals.
- A shared `server_t` structure stores the listening socket, board state, client file descriptors, player mappings, and running state.
- A mutex protects shared board and client mapping state from concurrent access.

This architecture was intentionally chosen because it exposes the core concurrency challenges directly. It requires careful reasoning about blocking I/O, thread lifetimes, lock boundaries, shared state, and cleanup behavior.

## Networking Design

The server uses POSIX TCP sockets directly:

- `socket()` creates the listening socket.
- `bind()` attaches the socket to a port.
- `listen()` marks it as a server socket.
- `accept()` waits for incoming clients.
- `recv()` and `send()` transfer binary protocol messages.

Because TCP is a byte stream, the implementation includes helper functions for exact-length reads and full-buffer writes. This avoids assuming that a single `recv()` or `send()` call will process an entire protocol message.

## Protocol Design

The project uses a compact binary protocol instead of text commands or JSON. Client messages are fixed-size 2-byte packets, while server messages serialize events such as:

- WELCOME
- GAME_STATE
- PLAYER_DEAD
- GAME_OVER
- ERROR

This design keeps the protocol small and deterministic, while also requiring explicit serialization/deserialization logic and careful byte-level handling.

## Concurrency Design

The server coordinates several kinds of concurrent activity:

- The accept loop creates new client handler threads.
- Client threads receive direction updates and lifecycle messages.
- The game loop thread periodically advances the board and broadcasts state.
- Shared game data is protected with a `pthread_mutex_t`.

The main concurrency challenge was deciding where locks should be held. The server needs to protect shared state, but it should avoid holding a mutex during potentially blocking socket operations. This led to a design where game state is copied or serialized while protected, then network sends are performed outside the critical section when possible.

## Game Logic

The project includes the core mechanics needed for a multiplayer Snake simulation:

- Board initialization and wall placement.
- Deterministic apple placement using a seed.
- Snake spawning and removal.
- Direction buffering.
- Snake movement and growth.
- Collision detection with walls, snakes, and self.
- Death handling and player cleanup.
- Game-state serialization for clients.

## Tools And Technologies

- C
- POSIX sockets
- TCP/IP
- POSIX threads (`pthreads`)
- Mutex synchronization
- Binary protocol serialization
- Make
- Criterion unit testing
- ThreadSanitizer-style concurrency debugging
- Linux/Unix development environment
- Git

## Build And Run

From the `SNAKE_HW` directory:

```bash
make
```

Run the server with a required port:

```bash
bin/snake_server -p 8080
```

Optional arguments include board size, seed, and max players:

```bash
bin/snake_server -p 8080 -b 20 -s 42 -m 4
```

Run the unit test binary:

```bash
make test
bin/snake_test
```