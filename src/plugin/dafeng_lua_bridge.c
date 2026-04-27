// Lua C extension that exposes the dafeng client to a librime Lua filter.
//
// Two builds of this file are produced:
//   - dafeng_lua_bridge.dylib (Mac) / .dll (Win): deployed alongside the
//     librime user dir; built with `-undefined dynamic_lookup` on Mac so
//     Lua C-API symbols resolve to the host (Squirrel/Weasel) at runtime.
//   - dafeng_lua_bridge_static (test only): linked with a vendored Lua
//     static library so the bridge can be exercised in unit tests without
//     a running Squirrel.

#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>

#include "dafeng/client_c.h"

#define DAFENG_CLIENT_MT "dafeng.client"

static dafeng_client_t** check_client_ud(lua_State* L, int idx) {
  return (dafeng_client_t**)luaL_checkudata(L, idx, DAFENG_CLIENT_MT);
}

static int l_client_create(lua_State* L) {
  const char* addr = luaL_optstring(L, 1, NULL);
  dafeng_client_t* c = dafeng_client_create(addr);
  if (c == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "client_create failed");
    return 2;
  }
  dafeng_client_t** ud = (dafeng_client_t**)lua_newuserdata(
      L, sizeof(dafeng_client_t*));
  *ud = c;
  luaL_getmetatable(L, DAFENG_CLIENT_MT);
  lua_setmetatable(L, -2);
  return 1;
}

static int l_client_gc(lua_State* L) {
  dafeng_client_t** ud = (dafeng_client_t**)lua_touserdata(L, 1);
  if (ud != NULL && *ud != NULL) {
    dafeng_client_destroy(*ud);
    *ud = NULL;
  }
  return 0;
}

// client:rerank(code, ctx, candidates_table, app_id, timeout_ms) -> table | nil
//
// On success returns a table { indices = {1-based ints}, latency_us = N,
// model_version = M }. On timeout / failure returns nil.
static int l_rerank(lua_State* L) {
  dafeng_client_t** ud = check_client_ud(L, 1);
  if (*ud == NULL) {
    lua_pushnil(L);
    return 1;
  }

  const char* code = luaL_checkstring(L, 2);
  const char* ctx = luaL_optstring(L, 3, "");
  luaL_checktype(L, 4, LUA_TTABLE);
  const char* app = luaL_optstring(L, 5, "");
  int timeout_ms = (int)luaL_optinteger(L, 6, 30);

  size_t n = lua_rawlen(L, 4);
  if (n == 0) {
    // Nothing to rerank — return empty result so the caller can short-circuit.
    lua_createtable(L, 0, 2);
    lua_createtable(L, 0, 0);
    lua_setfield(L, -2, "indices");
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "latency_us");
    return 1;
  }

  const char** cand_ptrs = (const char**)malloc(sizeof(const char*) * n);
  if (cand_ptrs == NULL) {
    lua_pushnil(L);
    return 1;
  }

  // Push every candidate string onto the Lua stack so they stay alive across
  // the C call. We pop them all together after the call.
  for (size_t i = 0; i < n; ++i) {
    lua_rawgeti(L, 4, (lua_Integer)(i + 1));
    cand_ptrs[i] = luaL_checkstring(L, -1);
  }

  dafeng_rerank_result result;
  memset(&result, 0, sizeof(result));
  int ok = dafeng_client_rerank(*ud, code, ctx, cand_ptrs, n, app, timeout_ms,
                                  &result);

  lua_pop(L, (int)n);  // pop candidate strings
  free(cand_ptrs);

  if (!ok) {
    lua_pushnil(L);
    return 1;
  }

  lua_createtable(L, 0, 3);
  lua_createtable(L, (int)result.count, 0);
  for (size_t i = 0; i < result.count; ++i) {
    // Convert 0-based C index to 1-based Lua index.
    lua_pushinteger(L, (lua_Integer)result.indices[i] + 1);
    lua_rawseti(L, -2, (lua_Integer)(i + 1));
  }
  lua_setfield(L, -2, "indices");
  lua_pushinteger(L, (lua_Integer)result.latency_us);
  lua_setfield(L, -2, "latency_us");
  lua_pushinteger(L, (lua_Integer)result.model_version);
  lua_setfield(L, -2, "model_version");

  dafeng_rerank_result_free(&result);
  return 1;
}

// client:record_commit(code, text, ctx)
static int l_record_commit(lua_State* L) {
  dafeng_client_t** ud = check_client_ud(L, 1);
  if (*ud == NULL) return 0;
  const char* code = luaL_checkstring(L, 2);
  const char* text = luaL_checkstring(L, 3);
  const char* ctx = luaL_optstring(L, 4, "");
  dafeng_client_record_commit(*ud, code, text, ctx);
  return 0;
}

// client:is_connected() -> boolean
static int l_is_connected(lua_State* L) {
  dafeng_client_t** ud = check_client_ud(L, 1);
  lua_pushboolean(L, *ud != NULL && dafeng_client_is_connected(*ud));
  return 1;
}

// client:reset_connection()
static int l_reset_connection(lua_State* L) {
  dafeng_client_t** ud = check_client_ud(L, 1);
  if (*ud != NULL) dafeng_client_reset_connection(*ud);
  return 0;
}

static const luaL_Reg kClientMethods[] = {
    {"rerank", l_rerank},
    {"record_commit", l_record_commit},
    {"is_connected", l_is_connected},
    {"reset_connection", l_reset_connection},
    {NULL, NULL},
};

static const luaL_Reg kModuleFuncs[] = {
    {"client_create", l_client_create},
    {NULL, NULL},
};

#ifdef _WIN32
__declspec(dllexport)
#endif
int luaopen_dafeng_lua_bridge(lua_State* L) {
  // Metatable used as both the userdata's mt and the method namespace.
  luaL_newmetatable(L, DAFENG_CLIENT_MT);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  luaL_setfuncs(L, kClientMethods, 0);
  lua_pushcfunction(L, l_client_gc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);

  luaL_newlib(L, kModuleFuncs);
  return 1;
}
