// End-to-end test: run a real Server in-process behind a UDS, drive it from
// DafengClient over the same path. Exercises the full encode -> socket ->
// decode -> handler -> encode -> socket -> decode loop.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>
#include <sys/stat.h>

#include <gtest/gtest.h>

#include "dafeng/client.h"
#include "dafeng/endpoint.h"
#include "dafeng/types.h"
// Daemon-internal headers; available because we link dafeng::daemon_core.
#include "server.h"
#include "services.h"

namespace dafeng {
namespace {

using std::chrono::milliseconds;

std::string MakeTempSocketPath() {
  char tmpl[] = "/tmp/dafeng-int-XXXXXX";
  int fd = ::mkstemp(tmpl);
  if (fd >= 0) ::close(fd);
  ::unlink(tmpl);
  return tmpl;
}

class DaemonHarness {
 public:
  DaemonHarness() {
    sock_ = MakeTempSocketPath();
    auto endpoint = ListenEndpoint(sock_);
    if (!endpoint) std::abort();
    server_ = std::make_unique<Server>(std::move(endpoint),
                                        MakeMockReverseRerankService(),
                                        MakeNullCommitLogger());
    thread_ = std::thread([this]() { server_->Run(); });
    // Brief beat to let the server enter Accept().
    std::this_thread::sleep_for(milliseconds(10));
  }

  ~DaemonHarness() {
    server_->Shutdown();
    if (thread_.joinable()) thread_.join();
  }

  const std::string& sock() const { return sock_; }

 private:
  std::string sock_;
  std::unique_ptr<Server> server_;
  std::thread thread_;
};

TEST(IntegrationTest, RerankReturnsReversedPermutation) {
  DaemonHarness h;
  DafengClient client(h.sock());

  RerankRequest req;
  req.code = "ggll";
  req.context_before = "你好";
  req.candidates = {"A", "B", "C", "D"};

  auto resp = client.Rerank(req, milliseconds(500));
  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(resp->reordered_indices,
            (std::vector<int32_t>{3, 2, 1, 0}));
  EXPECT_GE(resp->latency_us, 0u);
}

TEST(IntegrationTest, RerankAssignsIdsAndPreservesThemInResponse) {
  DaemonHarness h;
  DafengClient client(h.sock());

  RerankRequest r1;
  r1.candidates = {"x"};
  RerankRequest r2;
  r2.candidates = {"y"};

  auto resp1 = client.Rerank(r1, milliseconds(500));
  auto resp2 = client.Rerank(r2, milliseconds(500));
  ASSERT_TRUE(resp1.has_value());
  ASSERT_TRUE(resp2.has_value());
  // Server echoes the request_id back in the response.
  EXPECT_NE(resp1->request_id, 0u);
  EXPECT_NE(resp2->request_id, 0u);
  EXPECT_NE(resp1->request_id, resp2->request_id);
}

TEST(IntegrationTest, RerankReturnsNulloptWhenDaemonAbsent) {
  DafengClient client("/tmp/dafeng-not-a-real-socket-please");
  RerankRequest req;
  req.candidates = {"x"};
  auto resp = client.Rerank(req, milliseconds(50));
  EXPECT_FALSE(resp.has_value());
}

TEST(IntegrationTest, RecordCommitDoesNotRequireResponse) {
  DaemonHarness h;
  DafengClient client(h.sock());
  // Just exercise the path — no observable result, but it must not deadlock
  // or throw.
  client.RecordCommit("g", "感", "今天");
  client.RecordCommit("wo", "我", "感");
  // Subsequent rerank still works on the same connection.
  RerankRequest req;
  req.candidates = {"a", "b"};
  auto resp = client.Rerank(req, milliseconds(500));
  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(resp->reordered_indices, (std::vector<int32_t>{1, 0}));
}

TEST(IntegrationTest, ManyRerankCallsReuseTheSameConnection) {
  DaemonHarness h;
  DafengClient client(h.sock());
  RerankRequest req;
  req.candidates = {"a", "b", "c"};
  for (int i = 0; i < 50; ++i) {
    auto resp = client.Rerank(req, milliseconds(500));
    ASSERT_TRUE(resp.has_value()) << "iter " << i;
    EXPECT_EQ(resp->reordered_indices, (std::vector<int32_t>{2, 1, 0}));
  }
  EXPECT_TRUE(client.IsConnected());
}

TEST(IntegrationTest, FreshClientReconnectsAfterDaemonRestart) {
  // Two daemon lifecycles on the same socket path, fresh client per phase.
  // In-process Server::Shutdown only halts the accept loop — existing
  // worker threads keep serving until peer disconnects — so we cycle the
  // whole Server (destructor unlinks the socket file) between phases.
  auto sock = MakeTempSocketPath();
  RerankRequest req;
  req.candidates = {"x", "y"};

  for (int phase = 0; phase < 2; ++phase) {
    auto ep = ListenEndpoint(sock);
    ASSERT_NE(ep, nullptr) << "phase " << phase;
    Server s(std::move(ep), MakeMockReverseRerankService(),
             MakeNullCommitLogger());
    std::thread t([&]() { s.Run(); });
    std::this_thread::sleep_for(milliseconds(10));

    DafengClient client(sock);
    auto resp = client.Rerank(req, milliseconds(500));
    ASSERT_TRUE(resp.has_value()) << "phase " << phase;
    EXPECT_EQ(resp->reordered_indices, (std::vector<int32_t>{1, 0}))
        << "phase " << phase;

    s.Shutdown();
    t.join();
  }
}

TEST(IntegrationTest, ClientReturnsNulloptAfterReset) {
  // After an explicit ResetConnection, with no daemon listening, Rerank
  // must return nullopt rather than try to reuse a stale fd.
  DafengClient client("/tmp/dafeng-not-a-real-socket-please-2");
  RerankRequest req;
  req.candidates = {"x"};
  EXPECT_FALSE(client.Rerank(req, milliseconds(50)).has_value());
  client.ResetConnection();
  EXPECT_FALSE(client.IsConnected());
  EXPECT_FALSE(client.Rerank(req, milliseconds(50)).has_value());
}

TEST(IntegrationTest, ConcurrentClientsAreSerializedSafely) {
  DaemonHarness h;
  // Two clients hammering the server in parallel — proves the server can
  // accept multiple connections and each gets its own worker thread.
  std::atomic<int> ok{0};
  auto run = [&]() {
    DafengClient client(h.sock());
    RerankRequest req;
    req.candidates = {"a", "b", "c"};
    for (int i = 0; i < 20; ++i) {
      auto r = client.Rerank(req, milliseconds(500));
      if (r && r->reordered_indices == std::vector<int32_t>{2, 1, 0}) ok++;
    }
  };
  std::thread t1(run), t2(run);
  t1.join();
  t2.join();
  EXPECT_EQ(ok.load(), 40);
}

}  // namespace
}  // namespace dafeng
