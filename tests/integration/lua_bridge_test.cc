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

  // Wire up package.path to include src/plugin/ so `require("dafeng_rerank")`
  // resolves to src/plugin/dafeng_rerank.lua.
  void EnableSrcPluginRequire() {
    std::string s = "package.path = '";
    s += DAFENG_REPO_ROOT;
    s += "/src/plugin/?.lua;' .. package.path";
    RunLua(s);
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

// ---- dafeng_rerank.compute_order tests --------------------------------------
//
// These verify the pure-Lua reranking helper. They use a "fake client" stub
// so the tests don't depend on the real daemon for ordering decisions —
// only on the helper's slicing / protection / fallback math.

TEST_F(LuaBridgeFixture, RerankBypassWhenDisabled) {
  EnableSrcPluginRequire();
  RunLua(R"(
    local R = require('dafeng_rerank')
    local buf = { {text='a'}, {text='b'}, {text='c'} }
    local order = R.compute_order(buf, '', '', '', {'a','b','c'},
                                   { enabled=false, max_rerank=10 }, nil)
    assert(#order == 3 and order[1] == 1 and order[2] == 2 and order[3] == 3,
           'expected identity order, got ' .. table.concat(order, ','))
  )");
}

TEST_F(LuaBridgeFixture, RerankBypassWhenClientNil) {
  EnableSrcPluginRequire();
  RunLua(R"(
    local R = require('dafeng_rerank')
    local buf = { {text='a'}, {text='b'} }
    local order = R.compute_order(buf, '', '', '', {'a','b'},
                                   { enabled=true, max_rerank=10 }, nil)
    assert(#order == 2 and order[1] == 1 and order[2] == 2)
  )");
}

TEST_F(LuaBridgeFixture, ProtectTopNPinsPrefixToOriginalOrder) {
  EnableSrcPluginRequire();
  RunLua(R"(
    local R = require('dafeng_rerank')
    -- Fake client that always returns reverse permutation of given sub.
    local fake_client = {
      rerank = function(self, code, ctx, sub, app, timeout_ms)
        local n = #sub
        local idx = {}
        for i = 1, n do idx[i] = n - i + 1 end
        return { indices = idx, latency_us = 0 }
      end,
    }
    local buf = {}
    for i = 1, 6 do buf[i] = {text='c'..i} end
    local texts = {}
    for i = 1, 6 do texts[i] = 'c'..i end
    local order = R.compute_order(buf, '', '', '', texts,
                                   { enabled=true, max_rerank=10, protect_top_n=2 },
                                   fake_client)
    -- Top 2 protected: order = [1,2, then reverse of {3,4,5,6}]
    assert(order[1] == 1, 'order[1]=' .. tostring(order[1]))
    assert(order[2] == 2, 'order[2]=' .. tostring(order[2]))
    assert(order[3] == 6 and order[4] == 5 and order[5] == 4 and order[6] == 3,
           'rerank block wrong: ' .. table.concat(order, ','))
  )");
}

TEST_F(LuaBridgeFixture, ProtectTopNZeroMeansAllReranked) {
  EnableSrcPluginRequire();
  RunLua(R"(
    local R = require('dafeng_rerank')
    local fake_client = {
      rerank = function(self, c, x, sub, a, t)
        local n = #sub
        local idx = {}
        for i = 1, n do idx[i] = n - i + 1 end
        return { indices = idx, latency_us = 0 }
      end,
    }
    local buf = { {text='a'}, {text='b'}, {text='c'} }
    local order = R.compute_order(buf, '', '', '', {'a','b','c'},
                                   { enabled=true, max_rerank=10, protect_top_n=0 },
                                   fake_client)
    assert(order[1] == 3 and order[2] == 2 and order[3] == 1)
  )");
}

TEST_F(LuaBridgeFixture, MaxRerankCapsTheReorderedSlice) {
  EnableSrcPluginRequire();
  RunLua(R"(
    local R = require('dafeng_rerank')
    local fake_client = {
      rerank = function(self, c, x, sub, a, t)
        local n = #sub
        local idx = {}
        for i = 1, n do idx[i] = n - i + 1 end
        return { indices = idx, latency_us = 0 }
      end,
    }
    -- 8 candidates, protect 0, max_rerank 5 -> reorder [1..5], pass [6..8].
    local buf, texts = {}, {}
    for i = 1, 8 do buf[i] = {text='c'..i}; texts[i] = 'c'..i end
    local order = R.compute_order(buf, '', '', '', texts,
                                   { enabled=true, max_rerank=5, protect_top_n=0 },
                                   fake_client)
    -- First 5 reversed, then 6,7,8 in original order.
    assert(order[1] == 5 and order[2] == 4 and order[3] == 3 and
           order[4] == 2 and order[5] == 1)
    assert(order[6] == 6 and order[7] == 7 and order[8] == 8)
  )");
}

TEST_F(LuaBridgeFixture, RerankFailureFallsBackToOriginal) {
  EnableSrcPluginRequire();
  RunLua(R"(
    local R = require('dafeng_rerank')
    local fake_client = {
      rerank = function() return nil end,  -- simulate timeout
    }
    local buf = { {text='a'}, {text='b'}, {text='c'} }
    local order = R.compute_order(buf, '', '', '', {'a','b','c'},
                                   { enabled=true, max_rerank=10, protect_top_n=0 },
                                   fake_client)
    assert(order[1] == 1 and order[2] == 2 and order[3] == 3,
           'expected original order on timeout, got ' .. table.concat(order, ','))
  )");
}

TEST_F(LuaBridgeFixture, RerankWithRealDaemonRespectsProtection) {
  // Drives the actual daemon (mock-reverse) through compute_order with
  // protect_top_n=2 -> first 2 candidates pinned, the rest reversed.
  EnableSrcPluginRequire();
  std::string script = "local R = require('dafeng_rerank')\n"
                        "local b = require('dafeng_lua_bridge')\n";
  script += "local c = b.client_create('" + sock_ + "')\n";
  script += R"(
    local buf, texts = {}, {}
    for i = 1, 5 do buf[i] = {text='c'..i}; texts[i] = 'c'..i end
    local order = R.compute_order(buf, 'g', '', '', texts,
                                   { enabled=true, max_rerank=10, protect_top_n=2,
                                     timeout_ms=500 },
                                   c)
    -- First 2 pinned, rest is reversed [3,4,5] -> [5,4,3].
    assert(order[1] == 1 and order[2] == 2,
           'protect failed: ' .. table.concat(order, ','))
    assert(order[3] == 5 and order[4] == 4 and order[5] == 3,
           'rerank failed: ' .. table.concat(order, ','))
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
