#pragma once

// Cross-platform IPC endpoint abstraction. macOS uses Unix domain sockets,
// Windows uses Named Pipes — implementations live in src/common, behind this
// interface. No platform headers leak through this file.
//
// The interface is intentionally narrow: callers always read or write whole
// frames (length-prefixed MessagePack), never byte streams.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "dafeng/api.h"

namespace dafeng {

class DAFENG_API Connection {
 public:
  virtual ~Connection() = default;

  // Read exactly `size` bytes into `buf`. Returns false on timeout, peer
  // close, or error. Partial reads are restarted internally.
  virtual bool ReadFull(uint8_t* buf, std::size_t size,
                        std::chrono::milliseconds timeout) = 0;

  // Write exactly `size` bytes from `buf`. Returns false on timeout or
  // error. Partial writes are restarted internally.
  virtual bool WriteAll(const uint8_t* buf, std::size_t size,
                        std::chrono::milliseconds timeout) = 0;

  virtual void Close() = 0;
  virtual bool IsConnected() const = 0;
};

class DAFENG_API EndpointServer {
 public:
  virtual ~EndpointServer() = default;

  // Block until a client connects, until Shutdown() is called, or until the
  // server hits an unrecoverable error. Returns nullptr in the latter cases.
  virtual std::unique_ptr<Connection> Accept() = 0;

  // Wake any pending Accept() call so it returns nullptr. Idempotent and
  // safe to call from a different thread than Accept's.
  virtual void Shutdown() = 0;
};

// Factory: bind a server endpoint at `address`. On macOS, `address` is a
// filesystem path for a UDS; on Windows it is a `\\.\pipe\name` string.
DAFENG_API std::unique_ptr<EndpointServer> ListenEndpoint(
    const std::string& address);

// Factory: connect to a server at `address`. Returns nullptr if the server
// is not reachable — caller should treat this as "daemon not running" and
// proceed with the IME-only fallback path.
DAFENG_API std::unique_ptr<Connection> ConnectEndpoint(
    const std::string& address);

}  // namespace dafeng
