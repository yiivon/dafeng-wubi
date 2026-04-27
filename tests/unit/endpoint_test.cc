#include "dafeng/endpoint.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "dafeng/paths.h"

namespace dafeng {
namespace {

using std::chrono::milliseconds;

std::string MakeTempSocketPath() {
  // mkstemp gives us a unique name; we delete it and use the path for the
  // socket. The test uses a short path to stay under sun_path's 104 chars.
  char tmpl[] = "/tmp/dafeng-endpoint-test-XXXXXX";
  int fd = ::mkstemp(tmpl);
  if (fd >= 0) ::close(fd);
  ::unlink(tmpl);
  return tmpl;
}

TEST(EndpointTest, ConnectFailsWhenNoServer) {
  auto path = MakeTempSocketPath();
  auto conn = ConnectEndpoint(path);
  EXPECT_EQ(conn, nullptr);
}

TEST(EndpointTest, ListenAndConnect) {
  auto path = MakeTempSocketPath();
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
  auto path = MakeTempSocketPath();
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
  auto path = MakeTempSocketPath();
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
  auto path = MakeTempSocketPath();
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
  ::setenv("DAFENG_DATA_DIR", "/tmp/dafeng-test-override", 1);
  EXPECT_EQ(GetDataDir(), std::filesystem::path("/tmp/dafeng-test-override"));
  ::unsetenv("DAFENG_DATA_DIR");
}

TEST(PathsTest, DaemonAddressRespectsOverride) {
  ::setenv("DAFENG_DAEMON_ADDR", "/tmp/foo.sock", 1);
  EXPECT_EQ(GetDaemonAddress(), "/tmp/foo.sock");
  ::unsetenv("DAFENG_DAEMON_ADDR");
}

TEST(PathsTest, DefaultDaemonAddressIsUnderDataDir) {
  ::unsetenv("DAFENG_DAEMON_ADDR");
  ::setenv("DAFENG_DATA_DIR", "/tmp/dafeng-test-default", 1);
  EXPECT_EQ(GetDaemonAddress(), "/tmp/dafeng-test-default/daemon.sock");
  ::unsetenv("DAFENG_DATA_DIR");
}

}  // namespace
}  // namespace dafeng
