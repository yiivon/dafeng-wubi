-- librime Lua filter. Reranks candidates through the dafeng daemon and
-- never blocks the IME past the configured timeout (default 30 ms — see
-- DESIGN §3). The actual ordering math lives in dafeng_rerank.compute_order
-- which is pure-Lua and unit-tested independently of librime.
--
-- Schema configuration (wubi.schema.yaml):
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
    log.warning("dafeng_filter: dafeng_lua_bridge not loadable; passthrough only")
    return
  end
  if not ok_rerank or not dafeng_rerank then
    log.warning("dafeng_filter: dafeng_rerank not loadable; passthrough only")
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
  else
    log.warning("dafeng_filter: client_create failed: " .. tostring(err))
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

  -- Build rerank inputs.
  local code = ""
  local seg = env.engine.context:current_segment()
  if seg then
    code = env.engine.context.input:sub(seg.start + 1, seg._end) or ""
  end

  local ctx = ""
  local hist = env.engine.context.commit_history
  if hist and hist.repr then
    ctx = trim_context(hist:repr() or "", env.options.context_chars)
  end

  local cand_texts = {}
  for i, c in ipairs(buffered) do cand_texts[i] = c.text end

  local order = dafeng_rerank.compute_order(buffered, code, ctx, "",
                                             cand_texts, env.options,
                                             env.client)

  for _, idx in ipairs(order) do
    yield(buffered[idx])
  end
end

return M
