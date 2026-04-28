-- librime-lua entry point. Without this file, `lua_filter@*<name>` directives
-- in schema YAML find no module — that's the failure mode that makes a
-- correctly deployed schema do absolutely nothing at typing time.
--
-- Lives at <user_dir>/rime.lua. Two responsibilities:
--   1. Set up `package.cpath` so `require("dafeng_lua_bridge")` finds our
--      `.so` under <user_dir>/lua/. (path for .lua files is already set by
--      librime-lua; cpath for .so/.dll is not.)
--   2. Export the filter modules that schemas reference, by name.

local home = os.getenv("HOME")
if home and home ~= "" then
  -- Mac default. Windows port will need a separate branch later.
  local lua_dir = home .. "/Library/Rime/lua"
  package.cpath = lua_dir .. "/?.so;" .. package.cpath
end

-- Defensive load: if dafeng_filter fails to require (Lua bridge .so
-- incompatible, file missing, etc), substitute a passthrough so the IME
-- doesn't break — DESIGN §1's hard rule: never let our extension stall
-- input.
local function passthrough(translation, env)
  for cand in translation:iter() do yield(cand) end
end

local function load_filter()
  local ok, m = pcall(require, "dafeng_filter")
  if ok and type(m) == "table" then return m end
  if log and log.warning then
    log.warning("rime.lua: dafeng_filter not loadable: " .. tostring(m))
  end
  return {
    init = function(env) end,
    fini = function(env) end,
    func = passthrough,
  }
end

return {
  dafeng_filter = load_filter(),
}
