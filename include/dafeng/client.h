#pragma once

// IME-side IPC client. Designed to be safe to call from inside librime's
// filter pipeline:
//   - Rerank() always returns within `timeout` milliseconds. Failure modes
//     (daemon down, connect failed, decode error, timeout) all collapse to
//     std::nullopt — the caller's contract is "use original ordering".
//   - RecordCommit() is fire-and-forget; it never blocks the IME on a
//     daemon-side response.
//   - DafengClient is thread-safe — internal mutex guards the underlying
//     connection.
//   - On any IO error the connection is reset and the next call lazily
//     reconnects, so daemon restarts are transparent.

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "dafeng/api.h"
#include "dafeng/types.h"

namespace dafeng {

class DAFENG_API DafengClient {
 public:
  // Process-wide singleton bound to the default daemon address.
  static DafengClient& Instance();

  // Direct construction — for tests, `dafeng-cli`, and the Lua bridge.
  explicit DafengClient(std::string address);
  ~DafengClient();

  DafengClient(const DafengClient&) = delete;
  DafengClient& operator=(const DafengClient&) = delete;

  // Hot path. Returns nullopt on timeout / connect failure / decode error
  // — the caller MUST treat nullopt as "use the original candidate order".
  // request_id on the input is ignored; the client assigns its own.
  std::optional<RerankResponse> Rerank(const RerankRequest& req,
                                        std::chrono::milliseconds timeout);

  // Cold path. Never blocks on a response.
  void RecordCommit(const std::string& code,
                    const std::string& committed_text,
                    const std::string& context_before);

  // Cheap probe. Returns true iff a connection is currently established.
  bool IsConnected() const;

  // Force a reconnect on the next call. Mainly useful after a known daemon
  // restart so the next Rerank doesn't pay the failure-then-reconnect cost.
  void ResetConnection();

  const std::string& Address() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dafeng
