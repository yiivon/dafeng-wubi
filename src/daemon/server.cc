#include "server.h"

#include <chrono>
#include <optional>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "dafeng/logging.h"
#include "dafeng/protocol.h"

namespace dafeng {

namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

// Read timeout for the next frame on an idle client. Long enough to feel
// like a keep-alive, short enough that a wedged peer can't pin a worker
// indefinitely. Phase 2.3 will replace this with explicit heartbeats.
constexpr milliseconds kIdleReadTimeout = milliseconds{60'000};

// Once we've seen a frame header, the body must arrive promptly.
constexpr milliseconds kBodyReadTimeout = milliseconds{5'000};

// Server-side write timeout. Generous enough that a momentarily slow client
// doesn't kill the connection, tight enough that we don't pile up writers.
constexpr milliseconds kWriteTimeout = milliseconds{5'000};

}  // namespace

Server::Server(std::unique_ptr<EndpointServer> endpoint,
               std::unique_ptr<IRerankService> reranker,
               std::unique_ptr<ICommitLogger> logger)
    : endpoint_(std::move(endpoint)),
      reranker_(std::move(reranker)),
      logger_(std::move(logger)),
      start_time_(std::chrono::steady_clock::now()) {}

Server::~Server() = default;

void Server::Run() {
  DAFENG_LOG_INFO("server: entering accept loop");
  while (true) {
    auto conn = endpoint_->Accept();
    if (!conn) break;
    std::thread([this, c = std::move(conn)]() mutable {
      HandleConnection(std::move(c));
    }).detach();
  }
  DAFENG_LOG_INFO("server: accept loop exited");
}

void Server::Shutdown() {
  if (endpoint_) endpoint_->Shutdown();
}

StatsResponse Server::GetStats() const {
  StatsResponse s;
  s.rerank_count = rerank_count_.load(std::memory_order_relaxed);
  s.ping_count = ping_count_.load(std::memory_order_relaxed);
  s.commit_count = commit_count_.load(std::memory_order_relaxed);
  s.error_count = error_count_.load(std::memory_order_relaxed);
  s.rerank_latency_sum_us =
      rerank_latency_sum_us_.load(std::memory_order_relaxed);
  s.rerank_latency_max_us =
      rerank_latency_max_us_.load(std::memory_order_relaxed);
  s.rerank_model_version =
      rerank_model_version_.load(std::memory_order_relaxed);
  s.uptime_sec = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - start_time_)
          .count());
  return s;
}

void Server::HandleConnection(std::unique_ptr<Connection> conn) {
  std::vector<uint8_t> header(kFrameHeaderBytes);
  while (conn->IsConnected()) {
    if (!conn->ReadFull(header.data(), header.size(), kIdleReadTimeout)) break;

    const auto len_opt = ReadFrameLength(header.data(), header.size());
    if (!len_opt) {
      DAFENG_LOG_WARN("server: invalid frame length, closing connection");
      break;
    }

    std::vector<uint8_t> body(*len_opt);
    if (!conn->ReadFull(body.data(), body.size(), kBodyReadTimeout)) {
      DAFENG_LOG_WARN("server: body read failed, closing connection");
      break;
    }

    auto msg_opt = DecodeBody(body.data(), body.size());
    if (!msg_opt) {
      DAFENG_LOG_WARN("server: malformed message, closing connection");
      break;
    }

    std::optional<Message> reply;
    if (auto* req = std::get_if<RerankRequest>(&*msg_opt)) {
      auto resp = reranker_->Rerank(*req);
      // Update counters (relaxed; cross-thread visibility is best-effort).
      rerank_count_.fetch_add(1, std::memory_order_relaxed);
      rerank_latency_sum_us_.fetch_add(resp.latency_us,
                                        std::memory_order_relaxed);
      uint32_t prev_max =
          rerank_latency_max_us_.load(std::memory_order_relaxed);
      while (resp.latency_us > prev_max &&
             !rerank_latency_max_us_.compare_exchange_weak(
                 prev_max, resp.latency_us, std::memory_order_relaxed)) {
      }
      rerank_model_version_.store(resp.model_version,
                                   std::memory_order_relaxed);
      reply = Message{std::move(resp)};
    } else if (auto* ping = std::get_if<PingMessage>(&*msg_opt)) {
      PongMessage pong;
      pong.request_id = ping->request_id;
      ping_count_.fetch_add(1, std::memory_order_relaxed);
      reply = Message{pong};
    } else if (auto* ev = std::get_if<CommitEvent>(&*msg_opt)) {
      logger_->Record(*ev);
      commit_count_.fetch_add(1, std::memory_order_relaxed);
      // No reply — fire and forget.
    } else if (auto* sreq = std::get_if<StatsRequest>(&*msg_opt)) {
      auto stats = GetStats();
      stats.request_id = sreq->request_id;
      reply = Message{std::move(stats)};
    } else {
      error_count_.fetch_add(1, std::memory_order_relaxed);
      DAFENG_LOG_WARN("server: unexpected client-bound message type");
    }

    if (reply) {
      const auto frame = EncodeFrame(EncodeBody(*reply));
      if (!conn->WriteAll(frame.data(), frame.size(), kWriteTimeout)) {
        DAFENG_LOG_WARN("server: response write failed, closing connection");
        break;
      }
    }
  }
}

}  // namespace dafeng
