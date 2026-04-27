// Validates the Lua C bridge in isolation: spawns an in-process daemon,
// loads the bridge into a vendored Lua state via package.preload, and runs
// Lua scripts that exercise client_create / rerank / record_commit. This
// catches bridge bugs before they hit a real Squirrel deployment.

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <unistd.h>

#include <gtest/gtest.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

int luaopen_dafeng_lua_bridge(lua_State* L);
}

#include "dafeng/endpoint.h"
#include "server.h"
#include "services.h"

namespace {

std::string MakeTempSocketPath() {
  char tmpl[] = "/tmp/dafeng-luabr-XXXXXX";
  int fd = ::mkstemp(tmpl);
  if (fd >= 0) ::close(fd);
  ::unlink(tmpl);
  return tmpl;
}

class LuaBridgeFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    sock_ = MakeTempSocketPath();
    auto endpoint = dafeng::ListenEndpoint(sock_);
    ASSERT_NE(endpoint, nullptr);
    server_ = std::make_unique<dafeng::Server>(
        std::move(endpoint), dafeng::MakeMockReverseRerankService(),
        dafeng::MakeNullCommitLogger());
    server_thread_ = std::thread([this] { server_->Run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    L_ = luaL_newstate();
    ASSERT_NE(L_, nullptr);
    luaL_openlibs(L_);

    // package.preload['dafeng_lua_bridge'] = luaopen_dafeng_lua_bridge
    lua_getglobal(L_, "package");
    lua_getfield(L_, -1, "preload");
    lua_pushcfunction(L_, luaopen_dafeng_lua_bridge);
    lua_setfield(L_, -2, "dafeng_lua_bridge");
    lua_pop(L_, 2);
  }

  void TearDown() override {
    if (L_) {
      lua_close(L_);
      L_ = nullptr;
    }
    if (server_) server_->Shutdown();
    if (server_thread_.joinable()) server_thread_.join();
  }

  void RunLua(const std::string& script) {
    int rc = luaL_dostring(L_, script.c_str());
    if (rc != LUA_OK) {
      FAIL() << "Lua error: " << lua_tostring(L_, -1);
      lua_pop(L_, 1);
    }
  }

  std::string sock_;
  std::unique_ptr<dafeng::Server> server_;
  std::thread server_thread_;
  lua_State* L_ = nullptr;
};

TEST_F(LuaBridgeFixture, ModuleLoadsAndExposesClientCreate) {
  RunLua(R"(
    local m = require('dafeng_lua_bridge')
    assert(type(m) == 'table', 'module should be a table')
    assert(type(m.client_create) == 'function', 'client_create missing')
  )");
}

TEST_F(LuaBridgeFixture, ClientCreateReturnsUserdata) {
  std::string script = "local m = require('dafeng_lua_bridge')\n";
  script += "local c = m.client_create('" + sock_ + "')\n";
  script += "assert(c ~= nil, 'client_create returned nil')\n";
  script += "assert(type(c) == 'userdata', 'client should be userdata')\n";
  RunLua(script);
}

TEST_F(LuaBridgeFixture, RerankReturnsReversedOneIndexedPermutation) {
  std::string script = "local m = require('dafeng_lua_bridge')\n";
  script += "local c = m.client_create('" + sock_ + "')\n";
  script += R"(
    local r = c:rerank('ggll', '你好', {'A', 'B', 'C', 'D'}, '', 500)
    assert(r ~= nil, 'rerank returned nil')
    assert(type(r.indices) == 'table', 'indices missing')
    assert(#r.indices == 4, 'expected 4 indices, got ' .. tostring(#r.indices))
    -- Mock reverse: 0-based [3,2,1,0] -> 1-based [4,3,2,1]
    assert(r.indices[1] == 4, 'indices[1] = ' .. tostring(r.indices[1]))
    assert(r.indices[2] == 3, 'indices[2] = ' .. tostring(r.indices[2]))
    assert(r.indices[3] == 2, 'indices[3] = ' .. tostring(r.indices[3]))
    assert(r.indices[4] == 1, 'indices[4] = ' .. tostring(r.indices[4]))
    assert(type(r.latency_us) == 'number', 'latency_us missing')
  )";
  RunLua(script);
}

TEST_F(LuaBridgeFixture, RerankReturnsNilOnMissingDaemon) {
  RunLua(R"(
    local m = require('dafeng_lua_bridge')
    local c = m.client_create('/tmp/no-such-dafeng-socket')
    local r = c:rerank('x', '', {'a', 'b'}, '', 50)
    assert(r == nil, 'expected nil for missing daemon, got ' .. tostring(r))
  )");
}

TEST_F(LuaBridgeFixture, EmptyCandidatesReturnsEmptyIndices) {
  std::string script = "local m = require('dafeng_lua_bridge')\n";
  script += "local c = m.client_create('" + sock_ + "')\n";
  script += R"(
    local r = c:rerank('x', '', {}, '', 100)
    assert(r ~= nil, 'expected non-nil for empty cand list')
    assert(type(r.indices) == 'table')
    assert(#r.indices == 0, 'expected 0 indices')
  )";
  RunLua(script);
}

TEST_F(LuaBridgeFixture, RecordCommitReturnsNothingAndDoesNotBlock) {
  std::string script = "local m = require('dafeng_lua_bridge')\n";
  script += "local c = m.client_create('" + sock_ + "')\n";
  script += R"(
    c:record_commit('wo', '我', '今天')
    -- A subsequent rerank should still work on the same connection.
    local r = c:rerank('a', '', {'x', 'y'}, '', 500)
    assert(r ~= nil and #r.indices == 2)
  )";
  RunLua(script);
}

TEST_F(LuaBridgeFixture, IsConnectedReflectsState) {
  std::string script = "local m = require('dafeng_lua_bridge')\n";
  script += "local c = m.client_create('" + sock_ + "')\n";
  script += R"(
    -- Force a successful round-trip so the connection is established.
    local r = c:rerank('a', '', {'x'}, '', 500)
    assert(r ~= nil)
    assert(c:is_connected() == true, 'expected is_connected after rerank')
    c:reset_connection()
    assert(c:is_connected() == false, 'expected disconnected after reset')
  )";
  RunLua(script);
}

}  // namespace
