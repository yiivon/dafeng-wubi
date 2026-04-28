#include "dafeng/endpoint.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "dafeng/paths.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace dafeng {
namespace {

using std::chrono::milliseconds;

void SetTestEnv(const char* key, const char* value) {
#if defined(_WIN32)
  ::_putenv_s(key, value);
#else
  ::setenv(key, value, 1);
#endif
}

void UnsetTestEnv(const char* key) {
#if defined(_WIN32)
  ::_putenv_s(key, "");
#else
  ::unsetenv(key);
#endif
}

// Platform-appropriate, unique endpoint address for a single test.
// macOS: short UDS path under /tmp (sun_path is capped at 104 chars).
// Windows: a `\\.\pipe\…` name, which can be up to 256 chars but we keep
// it short for clarity.
std::string MakeTempEndpoint() {
  std::random_device rd;
  std::uniform_int_distribution<uint32_t> dist;
  char buf[64];
#if defined(_WIN32)
  std::snprintf(buf, sizeof(buf), "\\\\.\\pipe\\dafeng-test-%08x", dist(rd));
#else
  std::snprintf(buf, sizeof(buf), "/tmp/dafeng-endpoint-test-%08x.sock",
                dist(rd));
  ::unlink(buf);  // ensure clean slate for the bind
#endif
  return buf;
}

TEST(EndpointTest, ConnectFailsWhenNoServer) {
  auto path = MakeTempEndpoint();
  auto conn = ConnectEndpoint(path);
  EXPECT_EQ(conn, nullptr);
}

TEST(EndpointTest, ListenAndConnect) {
  auto path = MakeTempEndpoint();
  auto server = ListenEndpoint(path);
  ASSERT_NE(server, nullptr);

  std::atomic<bool> got_connection{false};
  std::thread server_thread([&] {
    auto conn = server->Accept();
    if (conn) got_connection = true;
  });

  // Brief beat to let the server enter Accept().
  std::this_thread::sleep_for(milliseconds(10));

  auto client = ConnectEndpoint(path);
  ASSERT_NE(client, nullptr);

  server_thread.join();
  EXPECT_TRUE(got_connection);
}

TEST(EndpointTest, RoundTripBytes) {
  auto path = MakeTempEndpoint();
  auto server = ListenEndpoint(path);
  ASSERT_NE(server, nullptr);

  const std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o', 0xFF, 0x00};

  std::thread server_thread([&] {
    auto conn = server->Accept();
    ASSERT_NE(conn, nullptr);
    std::vector<uint8_t> buf(payload.size());
    ASSERT_TRUE(conn->ReadFull(buf.data(), buf.size(), milliseconds(500)));
    EXPECT_EQ(buf, payload);
    // Echo back.
    ASSERT_TRUE(conn->WriteAll(buf.data(), buf.size(), milliseconds(500)));
  });

  std::this_thread::sleep_for(milliseconds(10));

  auto client = ConnectEndpoint(path);
  ASSERT_NE(client, nullptr);
  ASSERT_TRUE(client->WriteAll(payload.data(), payload.size(),
                                milliseconds(500)));
  std::vector<uint8_t> echo(payload.size());
  ASSERT_TRUE(client->ReadFull(echo.data(), echo.size(), milliseconds(500)));
  EXPECT_EQ(echo, payload);

  server_thread.join();
}

TEST(EndpointTest, ReadFullTimesOut) {
  auto path = MakeTempEndpoint();
  auto server = ListenEndpoint(path);
  ASSERT_NE(server, nullptr);

  std::thread server_thread([&] {
    auto conn = server->Accept();
    // Hold the connection without sending anything; let the client time out.
    std::this_thread::sleep_for(milliseconds(150));
  });

  std::this_thread::sleep_for(milliseconds(10));
  auto client = ConnectEndpoint(path);
  ASSERT_NE(client, nullptr);

  uint8_t b = 0;
  auto t0 = std::chrono::steady_clock::now();
  EXPECT_FALSE(client->ReadFull(&b, 1, milliseconds(30)));
  auto elapsed = std::chrono::duration_cast<milliseconds>(
                     std::chrono::steady_clock::now() - t0)
                     .count();
  // Allow scheduler slop; main assertion is that we returned in time.
  EXPECT_GE(elapsed, 25);
  EXPECT_LT(elapsed, 100);

  server_thread.join();
}

TEST(EndpointTest, ShutdownUnblocksAccept) {
  auto path = MakeTempEndpoint();
  auto server = ListenEndpoint(path);
  ASSERT_NE(server, nullptr);

  std::atomic<bool> returned{false};
  std::thread server_thread([&] {
    auto conn = server->Accept();
    EXPECT_EQ(conn, nullptr);
    returned = true;
  });

  std::this_thread::sleep_for(milliseconds(20));
  EXPECT_FALSE(returned.load());
  server->Shutdown();
  server_thread.join();
  EXPECT_TRUE(returned.load());
}

TEST(PathsTest, DataDirRespectsOverride) {
#if defined(_WIN32)
  const char* kOverride = "C:\\dafeng-test-override";
#else
  const char* kOverride = "/tmp/dafeng-test-override";
#endif
  SetTestEnv("DAFENG_DATA_DIR", kOverride);
  EXPECT_EQ(GetDataDir(), std::filesystem::path(kOverride));
  UnsetTestEnv("DAFENG_DATA_DIR");
}

TEST(PathsTest, DaemonAddressRespectsOverride) {
#if defined(_WIN32)
  const char* kOverride = "\\\\.\\pipe\\dafeng-test-foo";
#else
  const char* kOverride = "/tmp/foo.sock";
#endif
  SetTestEnv("DAFENG_DAEMON_ADDR", kOverride);
  EXPECT_EQ(GetDaemonAddress(), kOverride);
  UnsetTestEnv("DAFENG_DAEMON_ADDR");
}

#if !defined(_WIN32)
// Mac/Unix only: the default daemon address is `<datadir>/daemon.sock`,
// derived from GetDataDir(). On Windows the default is a per-user pipe
// name `\\.\pipe\dafeng-daemon-<user>` and isn't a function of DataDir,
// so the equivalence doesn't hold.
TEST(PathsTest, DefaultDaemonAddressIsUnderDataDir) {
  UnsetTestEnv("DAFENG_DAEMON_ADDR");
  SetTestEnv("DAFENG_DATA_DIR", "/tmp/dafeng-test-default");
  EXPECT_EQ(GetDaemonAddress(), "/tmp/dafeng-test-default/daemon.sock");
  UnsetTestEnv("DAFENG_DATA_DIR");
}
#else
// Windows: default address has the per-user pipe form.
TEST(PathsTest, DefaultDaemonAddressIsPerUserPipe) {
  UnsetTestEnv("DAFENG_DAEMON_ADDR");
  const auto addr = GetDaemonAddress();
  EXPECT_EQ(addr.find("\\\\.\\pipe\\dafeng-daemon-"), 0u);
  // Suffix must be at least one sanitized char.
  EXPECT_GT(addr.size(), std::string("\\\\.\\pipe\\dafeng-daemon-").size());
}
#endif

}  // namespace
}  // namespace dafeng
