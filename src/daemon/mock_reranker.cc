#include "services.h"

#include "history_store.h"

#include <chrono>
#include <utility>

namespace dafeng {

namespace {

class MockReverseReranker final : public IRerankService {
 public:
  RerankResponse Rerank(const RerankRequest& req) override {
    const auto t0 = std::chrono::steady_clock::now();
    RerankResponse resp;
    resp.request_id = req.request_id;
    const int n = static_cast<int>(req.candidates.size());
    resp.reordered_indices.reserve(n);
    for (int i = n - 1; i >= 0; --i) {
      resp.reordered_indices.push_back(i);
    }
    const auto t1 = std::chrono::steady_clock::now();
    resp.latency_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    resp.model_version = 0;
    return resp;
  }

  bool IsReady() const override { return true; }
};

class NullCommitLogger final : public ICommitLogger {
 public:
  void Record(const CommitEvent&) override {}
};

class SqliteCommitLogger final : public ICommitLogger {
 public:
  explicit SqliteCommitLogger(std::shared_ptr<IHistoryStore> store)
      : store_(std::move(store)) {}
  void Record(const CommitEvent& ev) override {
    if (store_) (void)store_->Insert(ev);  // best-effort; cold path
  }
 private:
  std::shared_ptr<IHistoryStore> store_;
};

}  // namespace

std::unique_ptr<IRerankService> MakeMockReverseRerankService() {
  return std::make_unique<MockReverseReranker>();
}

std::unique_ptr<ICommitLogger> MakeNullCommitLogger() {
  return std::make_unique<NullCommitLogger>();
}

std::unique_ptr<ICommitLogger> MakeSqliteCommitLogger(
    std::shared_ptr<IHistoryStore> store) {
  if (!store) return nullptr;
  return std::make_unique<SqliteCommitLogger>(std::move(store));
}

}  // namespace dafeng
