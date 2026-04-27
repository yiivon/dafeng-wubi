-- librime Lua filter that reranks candidates through the dafeng daemon.
-- Falls back to the original ordering on any failure — never blocks the IME
-- past the configured timeout (default 30 ms, see DESIGN §3).
--
-- Schema configuration (wubi.schema.yaml):
--   dafeng_options:
--     enabled: true
--     timeout_ms: 30
--     max_candidates_to_rerank: 10
--     context_chars: 20
--     fallback_silent: true

local ok, dafeng = pcall(require, "dafeng_lua_bridge")

local M = {}

function M.init(env)
  env.client = nil
  env.enabled = false

  if not ok or not dafeng then
    -- Bridge missing — log once and degrade silently afterwards.
    log.warning("dafeng_filter: dafeng_lua_bridge not loadable; passthrough only")
    return
  end

  local config = env.engine.schema.config
  local opt_enabled = config:get_bool("dafeng_options/enabled")
  if opt_enabled == nil then opt_enabled = true end

  env.timeout_ms = config:get_int("dafeng_options/timeout_ms") or 30
  env.max_rerank = config:get_int("dafeng_options/max_candidates_to_rerank") or 10
  env.context_chars = config:get_int("dafeng_options/context_chars") or 20
  env.enabled = opt_enabled

  local c, err = dafeng.client_create(nil)  -- nil = default daemon address
  if c then
    env.client = c
  else
    log.warning("dafeng_filter: client_create failed: " .. tostring(err))
    env.enabled = false
  end
end

function M.fini(env)
  -- Lua GC reclaims env.client via the metatable's __gc.
  env.client = nil
end

local function trim_context(s, max_chars)
  if not s then return "" end
  -- Approximate: trim by byte length to ~max_chars * 4 (UTF-8 worst case).
  local cap = max_chars * 4
  if #s > cap then return string.sub(s, -cap) end
  return s
end

function M.func(translation, env)
  -- Buffer all candidates; reranking only the first env.max_rerank of them.
  local buffered = {}
  for cand in translation:iter() do
    table.insert(buffered, cand)
  end

  local n = #buffered
  if n == 0 then return end

  -- Bypass paths.
  if not env.enabled or not env.client then
    for _, c in ipairs(buffered) do yield(c) end
    return
  end

  -- Construct the request from the head of the candidate list.
  local rerank_count = math.min(n, env.max_rerank)
  local cand_texts = {}
  for i = 1, rerank_count do
    cand_texts[i] = buffered[i].text
  end

  local code = ""
  local seg = env.engine.context:current_segment()
  if seg then
    code = env.engine.context.input:sub(seg.start + 1, seg._end) or ""
  end

  local ctx = ""
  local hist = env.engine.context.commit_history
  if hist and hist.repr then
    ctx = trim_context(hist:repr() or "", env.context_chars)
  end

  local app_id = ""
  -- Squirrel exposes the front-end client_id via env.engine.context.option,
  -- but it's not standardized — leave empty for Phase 2.1.

  local result = env.client:rerank(code, ctx, cand_texts, app_id,
                                    env.timeout_ms)

  if not result or not result.indices or #result.indices == 0 then
    -- Timeout / failure / daemon down: original order.
    for _, c in ipairs(buffered) do yield(c) end
    return
  end

  -- Yield reranked head, then the un-reranked tail in original order.
  local emitted = {}
  for _, idx in ipairs(result.indices) do
    if idx >= 1 and idx <= rerank_count then
      yield(buffered[idx])
      emitted[idx] = true
    end
  end
  -- Defensive: emit any reranked candidate that the daemon dropped (shouldn't
  -- happen, but our contract is "stable on failure").
  for i = 1, rerank_count do
    if not emitted[i] then yield(buffered[i]) end
  end
  -- Tail (beyond max_rerank) keeps original order.
  for i = rerank_count + 1, n do
    yield(buffered[i])
  end
end

-- librime-lua's commit_notifier hook (if available) could push CommitEvent
-- here; deferred to Phase 2.3 because the API surface is non-trivial across
-- librime versions.

return M
