#include "dafeng/client.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <utility>
#include <variant>
#include <vector>

#include "dafeng/endpoint.h"
#include "dafeng/logging.h"
#include "dafeng/paths.h"
#include "dafeng/protocol.h"

namespace dafeng {

namespace {

using std::chrono::milliseconds;
using std::chrono::steady_clock;

milliseconds Remaining(const steady_clock::time_point& deadline) {
  auto rem = std::chrono::duration_cast<milliseconds>(deadline -
                                                      steady_clock::now());
  if (rem.count() < 0) return milliseconds{0};
  return rem;
}

// RecordCommit deadline. Tight because the IME has already shown the user
// their character — anything more than a few ms is just bookkeeping.
constexpr milliseconds kCommitWriteDeadline{50};

}  // namespace

class DafengClient::Impl {
 public:
  explicit Impl(std::string address) : address_(std::move(address)) {}

  std::optional<RerankResponse> Rerank(const RerankRequest& req,
                                        milliseconds timeout) {
    const auto deadline = steady_clock::now() + timeout;
    std::lock_guard<std::mutex> lock(mu_);

    if (!EnsureConnectedLocked()) return std::nullopt;

    RerankRequest req_with_id = req;
    req_with_id.request_id = next_request_id_.fetch_add(1);

    const auto frame = EncodeFrame(EncodeBody(req_with_id));
    if (!conn_->WriteAll(frame.data(), frame.size(), Remaining(deadline))) {
      ResetLocked();
      return std::nullopt;
    }

    uint8_t header[kFrameHeaderBytes];
    if (!conn_->ReadFull(header, sizeof(header), Remaining(deadline))) {
      ResetLocked();
      return std::nullopt;
    }
    const auto body_len = ReadFrameLength(header, sizeof(header));
    if (!body_len) {
      ResetLocked();
      return std::nullopt;
    }

    std::vector<uint8_t> body(*body_len);
    if (!conn_->ReadFull(body.data(), body.size(), Remaining(deadline))) {
      ResetLocked();
      return std::nullopt;
    }

    auto msg = DecodeBody(body.data(), body.size());
    if (!msg) {
      ResetLocked();
      return std::nullopt;
    }
    auto* resp = std::get_if<RerankResponse>(&*msg);
    if (resp == nullptr) {
      // Unexpected reply type. Treat as a protocol error and reset.
      ResetLocked();
      return std::nullopt;
    }
    return *resp;
  }

  void RecordCommit(const std::string& code, const std::string& text,
                    const std::string& ctx) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!EnsureConnectedLocked()) return;

    CommitEvent ev;
    ev.code = code;
    ev.committed_text = text;
    ev.context_before = ctx;

    const auto frame = EncodeFrame(EncodeBody(ev));
    if (!conn_->WriteAll(frame.data(), frame.size(), kCommitWriteDeadline)) {
      ResetLocked();
    }
  }

  bool IsConnected() const {
    std::lock_guard<std::mutex> lock(mu_);
    return conn_ != nullptr && conn_->IsConnected();
  }

  void ResetConnection() {
    std::lock_guard<std::mutex> lock(mu_);
    ResetLocked();
  }

  const std::string& Address() const { return address_; }

 private:
  bool EnsureConnectedLocked() {
    if (conn_ && conn_->IsConnected()) return true;
    conn_ = ConnectEndpoint(address_);
    if (!conn_) {
      DAFENG_LOG_DEBUG("client: connect to %s failed", address_.c_str());
      return false;
    }
    return true;
  }

  void ResetLocked() {
    if (conn_) conn_->Close();
    conn_.reset();
  }

  mutable std::mutex mu_;
  std::unique_ptr<Connection> conn_;
  std::atomic<uint32_t> next_request_id_{1};
  std::string address_;
};

DafengClient::DafengClient(std::string address)
    : impl_(std::make_unique<Impl>(std::move(address))) {}

DafengClient::~DafengClient() = default;

DafengClient& DafengClient::Instance() {
  static DafengClient inst(GetDaemonAddress());
  return inst;
}

std::optional<RerankResponse> DafengClient::Rerank(const RerankRequest& req,
                                                    milliseconds timeout) {
  return impl_->Rerank(req, timeout);
}

void DafengClient::RecordCommit(const std::string& code,
                                 const std::string& committed_text,
                                 const std::string& context_before) {
  impl_->RecordCommit(code, committed_text, context_before);
}

bool DafengClient::IsConnected() const { return impl_->IsConnected(); }

void DafengClient::ResetConnection() { impl_->ResetConnection(); }

const std::string& DafengClient::Address() const { return impl_->Address(); }

}  // namespace dafeng
