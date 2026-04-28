-- librime Lua filter. Reranks candidates through the dafeng daemon and
-- never blocks the IME past the configured timeout (default 30 ms — see
-- DESIGN §3). The actual ordering math lives in dafeng_rerank.compute_order
-- which is pure-Lua and unit-tested independently of librime.
--
-- Schema configuration (wubi86_dafeng.schema.yaml):
--   dafeng_options:
--     enabled: true
--     timeout_ms: 30
--     max_candidates_to_rerank: 10
--     protect_top_n: 3              # top-N protection (DESIGN §9.2 R5)
--     context_chars: 20

local ok_bridge, dafeng = pcall(require, "dafeng_lua_bridge")
local ok_rerank, dafeng_rerank = pcall(require, "dafeng_rerank")

local M = {}

local function trim_context(s, max_chars)
  if not s then return "" end
  -- Approximate: trim by byte length to ~max_chars * 4 (UTF-8 worst case).
  local cap = max_chars * 4
  if #s > cap then return string.sub(s, -cap) end
  return s
end

-- Hook the commit_notifier so we can ship every committed text to the
-- daemon. Without this, history.db never grows and new-word discovery
-- has nothing to chew on. Wrapped in pcall — librime-lua API surface
-- varies across versions and we never want a missing method to take
-- down the IME.
local function attach_commit_hook(env)
  if env._dafeng_commit_hook_attached then return end
  local notifier = env.engine.context.commit_notifier
  if not notifier or not notifier.connect then return end
  local ok = pcall(function()
    notifier:connect(function(ctx)
      pcall(function()
        local committed = ctx:get_commit_text() or ""
        local code = ctx.input or ""
        local hist = ctx.commit_history
        local before = ""
        if hist and hist.repr then
          before = trim_context(hist:repr() or "", env.options.context_chars)
        end
        if committed ~= "" and env.client then
          env.client:record_commit(code, committed, before)
        end
      end)
    end)
  end)
  if ok then env._dafeng_commit_hook_attached = true end
end

function M.init(env)
  env.client = nil
  env.options = {
    enabled = false,
    timeout_ms = 30,
    max_rerank = 10,
    protect_top_n = 3,
    context_chars = 20,
  }

  if not ok_bridge or not dafeng then
    if log and log.warning then
      log.warning("dafeng_filter: dafeng_lua_bridge not loadable; passthrough")
    end
    return
  end
  if not ok_rerank or not dafeng_rerank then
    if log and log.warning then
      log.warning("dafeng_filter: dafeng_rerank not loadable; passthrough")
    end
    return
  end

  local config = env.engine.schema.config
  local opt_enabled = config:get_bool("dafeng_options/enabled")
  if opt_enabled == nil then opt_enabled = true end

  env.options.enabled = opt_enabled
  env.options.timeout_ms = config:get_int("dafeng_options/timeout_ms") or 30
  env.options.max_rerank = config:get_int(
      "dafeng_options/max_candidates_to_rerank") or 10
  env.options.protect_top_n = config:get_int(
      "dafeng_options/protect_top_n") or 3
  env.options.context_chars = config:get_int(
      "dafeng_options/context_chars") or 20

  local c, err = dafeng.client_create(nil)
  if c then
    env.client = c
    attach_commit_hook(env)
  else
    if log and log.warning then
      log.warning("dafeng_filter: client_create failed: " .. tostring(err))
    end
    env.options.enabled = false
  end
end

function M.fini(env)
  env.client = nil
end

function M.func(translation, env)
  local buffered = {}
  for cand in translation:iter() do table.insert(buffered, cand) end
  local n = #buffered
  if n == 0 then return end

  if not env.options.enabled or not env.client or not dafeng_rerank then
    for _, c in ipairs(buffered) do yield(c) end
    return
  end

  -- librime-lua's exposed APIs vary across versions; current_segment() is
  -- one method-style call that doesn't exist in Squirrel 1.1.x's bundled
  -- librime-lua. Use env.engine.context.input directly — for wubi this is
  -- 1-4 letters, the whole input IS the code.
  local code = ""
  pcall(function() code = env.engine.context.input or "" end)

  local ctx = ""
  pcall(function()
    local hist = env.engine.context.commit_history
    if hist and hist.repr then
      ctx = trim_context(hist:repr() or "", env.options.context_chars)
    end
  end)

  local cand_texts = {}
  for i, c in ipairs(buffered) do cand_texts[i] = c.text end

  local ok_co, order_or_err = pcall(dafeng_rerank.compute_order,
                                      buffered, code, ctx, "",
                                      cand_texts, env.options, env.client)
  if not ok_co then
    for _, c in ipairs(buffered) do yield(c) end
    return
  end
  local order = order_or_err

  for _, idx in ipairs(order) do
    yield(buffered[idx])
  end
end

return M
