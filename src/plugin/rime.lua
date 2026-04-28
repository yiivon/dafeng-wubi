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
  package.cpath = home .. "/Library/Rime/lua/?.so;" .. package.cpath
end

-- Defensive load: if dafeng_filter fails to require (Lua bridge .so
-- incompatible, file missing, etc), substitute a passthrough so the IME
-- doesn't break — DESIGN §1's hard rule: never let our extension stall
-- input.
local function passthrough(translation, env)
  for cand in translation:iter() do yield(cand) end
end

local ok, mod = pcall(require, "dafeng_filter")
if not ok or type(mod) ~= "table" then
  if log and log.warning then
    log.warning("rime.lua: dafeng_filter not loadable: " .. tostring(mod))
  end
  mod = nil
end

-- librime-lua in Squirrel 1.1.x only honors function-style filters —
-- {init, fini, func} module tables parse but the lifecycle hooks never
-- fire. Wrap our module: lazy-init on first call, then dispatch to
-- module's func. env is per-session so init runs once per IME context.
local function dafeng_filter(translation, env)
  if mod == nil then return passthrough(translation, env) end
  if not env._dafeng_initialized then
    if mod.init then mod.init(env) end
    env._dafeng_initialized = true
  end
  return mod.func(translation, env)
end

return {
  dafeng_filter = dafeng_filter,
}
