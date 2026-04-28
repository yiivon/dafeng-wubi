-- Pure-Lua reranking logic. Extracted from dafeng_filter.lua so it can be
-- unit-tested without librime.
--
-- The flow:
--   buffered = [c1, c2, ..., cN]                   (in librime order)
--   protect_top_n = 3
--   max_rerank   = 10
--
-- Yield order:
--   [c1, c2, c3]  -- protected (muscle memory; never moves)
--   reranked permutation of [c4 .. c_min(13,N)]
--   [c14 .. cN]   -- tail in original order

local M = {}

-- One-line append to ~/Library/Logs/dafeng-filter.log on every daemon
-- fallback (timeout / nil / pcall error). Lets us correlate "the IME
-- felt slow at 16:42" against actual daemon-unavailable events without
-- flipping debug flags. Cheap enough that we don't bother rate-limiting.
local function log_fallback(reason, code, n)
  local home = os.getenv("HOME")
  if not home or home == "" then return end
  local path = home .. "/Library/Logs/dafeng-filter.log"
  local f = io.open(path, "a")
  if not f then return end
  -- ISO-ish timestamp; second precision is fine for human correlation.
  local ts = os.date("%Y-%m-%dT%H:%M:%S")
  f:write(string.format("[%s] code=%s n=%d reason=%s\n",
                        ts, code or "", n or 0, reason or ""))
  f:close()
end

-- Returns a list of 1-based indices into `buffered`, in the order the
-- librime filter should yield them.
--
-- Args:
--   buffered: array of any (only #buffered is read; pass-through identity)
--   code, ctx, app: rerank inputs
--   cand_texts: array of strings; cand_texts[i] is buffered[i].text
--   options: { enabled, protect_top_n, max_rerank, timeout_ms }
--   client: dafeng client userdata (or nil to skip rerank)
function M.compute_order(buffered, code, ctx, app, cand_texts, options, client)
  local n = #buffered
  local out = {}
  if n == 0 then return out end

  -- Bypass when the filter is off, the bridge isn't loaded, or there's no
  -- client; preserves librime's order verbatim.
  if not options.enabled or not client or not options.max_rerank or
      options.max_rerank < 1 then
    for i = 1, n do out[i] = i end
    return out
  end

  local protect_n = math.max(0, math.min(options.protect_top_n or 0, n))
  local rerank_start = protect_n + 1
  local rerank_end = math.min(protect_n + options.max_rerank, n)

  -- 1. Protected prefix in original order.
  for i = 1, protect_n do out[#out + 1] = i end

  -- 2. Reranked block.
  if rerank_end >= rerank_start then
    local sub = {}
    for i = rerank_start, rerank_end do
      sub[i - rerank_start + 1] = cand_texts[i] or ""
    end
    local ok_r, result = pcall(function()
      return client:rerank(code, ctx, sub, app or "", options.timeout_ms or 30)
    end)
    local pcall_err = nil
    if not ok_r then
      pcall_err = tostring(result)
      result = nil
    end
    if result and result.indices and #result.indices > 0 then
      local emitted = {}
      for _, lua_sub_idx in ipairs(result.indices) do
        local actual = lua_sub_idx + protect_n
        if actual >= rerank_start and actual <= rerank_end then
          out[#out + 1] = actual
          emitted[actual] = true
        end
      end
      -- Defensive: emit any rerank-block index the daemon dropped.
      for i = rerank_start, rerank_end do
        if not emitted[i] then out[#out + 1] = i end
      end
    else
      -- Daemon timeout / failure / nil: original order for the rerank slice.
      -- Log so a user reporting "felt slow at 16:42" gives us a timestamp
      -- to correlate against (instead of silently degrading to RIME default).
      local n_sub = rerank_end - rerank_start + 1
      if pcall_err then
        log_fallback("err: " .. pcall_err, code, n_sub)
      elseif result == nil then
        log_fallback("nil (daemon timeout/unreachable)", code, n_sub)
      else
        log_fallback("empty indices", code, n_sub)
      end
      for i = rerank_start, rerank_end do out[#out + 1] = i end
    end
  end

  -- 3. Tail beyond max_rerank.
  for i = rerank_end + 1, n do out[#out + 1] = i end

  return out
end

return M
