# 大风五笔 — 日常使用 Cheat Sheet

> 这一份给已经把 L3 走通(可以正常打字 + xupw 出弹窗)的你,作为
> 长期使用 / 维护手册。所有命令默认在 repo 根目录执行。

## 一句话现状

- IME = Squirrel + 我们的 schema(大风五笔 86)
- 候选重排 daemon 跑在后台,P99 < 30 µs(预算的 0.1%)
- 你打字 → 自动落到 `history.db`(本机,不离线、不上传、0600 权限)
- `dafeng-cli learn` → 把 history 中频繁的拆字组合编码成五笔短语码,
  写进 `dafeng_learned.dict.yaml`
- 重新部署 RIME → 学到的词组立即可用作候选

---

## 1. daemon 自启(强烈建议装一次)

```bash
./packaging/macos/install_launchagent.sh
```

装完后:
- 每次 macOS 登录自动起 daemon
- 崩了自动重启(节流间隔 10s)
- 日志:`~/Library/Logs/dafeng-daemon.out.log` / `.err.log`

### 暂停 / 恢复(省电)

笔记本电池吃紧、或临时不想后台跑 LLM:

```bash
dafeng-cli pause     # 停 daemon。IME 自动降级到原生 RIME,继续能打字
dafeng-cli resume    # 重新拉起来
```

`pause` 会等 daemon 真正退出再返回(避免立即 resume 时 launchctl I/O race)。
两条命令都是幂等的:已停了再 pause、已起着再 resume,只 print 状态不报错。

### 切换后端(LLM ↔ deterministic)

如果想保持 daemon 跑(为了 stats、history、auto-learn),只是不要 LLM:

```bash
# 切到 deterministic(占用 ~5MB,vs LLM ~600MB)
bash packaging/macos/install_launchagent.sh \
  $(pwd)/build-llama/src/daemon/dafeng-daemon

# 切回 LLM
DAFENG_BACKEND=llama_cpp \
DAFENG_MODEL_PATH="$HOME/Library/Application Support/Dafeng/models/Qwen2.5-0.5B-Instruct-GGUF/qwen2.5-0.5b-instruct-q4_k_m.gguf" \
  bash packaging/macos/install_launchagent.sh \
  $(pwd)/build-llama/src/daemon/dafeng-daemon
```

`dafeng-cli stats` 会显示当前后端 (`deterministic (v1)` vs `llama_cpp (v3)`)。

### 完全卸载

```bash
dafeng-cli pause
rm ~/Library/LaunchAgents/com.dafeng.daemon.plist
```

## 2. 日常巡检(三条命令)

```bash
./build/src/cli/dafeng-cli ping     # daemon 活着吗
./build/src/cli/dafeng-cli stats    # rerank/commit 计数 + 延迟
./build/src/cli/dafeng-cli history  # 最近 commits
```

`stats` 期望看到:
- `rerank model: deterministic (v1)`(MLX 接好后会变成 v2)
- `rerank requests` / `commit events` 在持续增长
- `errors: 0`
- `rerank max (us)` 远小于 30000

## 3. 学习新词(每周一次,或想看了就看)

```bash
# 干跑,只看 learn 出什么、不写盘:
./build/src/cli/dafeng-cli learn --min-freq 2 --dry-run

# 真跑,写到 ~/Library/Rime/dafeng_learned.dict.yaml + git commit:
./build/src/cli/dafeng-cli learn --min-freq 2

# 然后菜单栏鼠须管 → 重新部署
```

调阈值:打字少时 `--min-freq 1` 看更全;打字多了用 `--min-freq 3` 减噪。

## 4. 看学到了什么

```bash
# 给人看的清单(频率 + 时间戳):
cat ~/Library/Application\ Support/Dafeng/userdata/learned_words.yaml

# 给 RIME 用的字典:
cat ~/Library/Rime/dafeng_learned.dict.yaml

# 直接查 SQLite history:
sqlite3 ~/Library/Application\ Support/Dafeng/history.db \
  'SELECT code,text,COUNT(*) FROM commits GROUP BY code,text ORDER BY COUNT(*) DESC LIMIT 30;'
```

## 5. 跨设备同步(可选)

`~/Library/Application Support/Dafeng/userdata/` 是一个 Git 仓库,
默认只本地 commit,**不 push**。push 需要双重显式开关:

1. 配置 push URL(私有仓库):
   ```bash
   cd ~/Library/Application\ Support/Dafeng/userdata
   git remote add origin git@github.com:你的账号/dafeng-userdata.git
   ```
2. 在 daemon config / learning round config 里把 `push_to_remote=true`
   (Phase 2.4 没有 CLI 开关,Phase 3 加)。

## 6. 出问题去哪查

| 现象 | 第一站日志 |
|---|---|
| daemon 不工作 | `~/Library/Logs/dafeng-daemon.err.log` |
| Squirrel 部署失败 | `/private/var/folders/.../T/rime.squirrel/rime.squirrel.ERROR` |
| 学到的词不出 | `~/Library/Rime/dafeng_learned.dict.yaml` 是不是空的;部署后 build 缓存里有没有 `dafeng_learned.table.bin` |
| commit 没记 | `dafeng-cli stats` 看 `commit events` 是不是涨;否则 daemon 死了 |
| IME 卡顿 | 杀 daemon 看是否变快;如果变快 → 重排路径有 regression |

## 7. 结构地图(出了诡异问题时,你或我快速回忆)

```
                 ┌────────────────────┐
                 │   Squirrel (IME)   │
                 │   librime + Lua    │
                 └────────┬───────────┘
                          │  IPC (UDS, MessagePack, P99 14µs)
                          ▼
                 ┌────────────────────┐
                 │  dafeng-daemon     │  rerank (热) + commit logger (冷)
                 └────────┬───────────┘
                          │
              ┌───────────┴────────────┐
              ▼                        ▼
   history.db (0600,本机)        userdata/ (Git 仓库)
   (raw input,不离线)            ├ personal_ngram.bin
                                 ├ learned_words.yaml
                                 └ dafeng_learned.dict.yaml ← RIME 读这个
```

每次 `dafeng-cli learn`:扫 history → ngram + 新词发现 + WubiCodec
按规则编码 → 写 `dafeng_learned.dict.yaml` → git commit。

## 8. 升级 / 重装时不会丢什么

- `~/Library/Application Support/Dafeng/`(数据)—— 你的
- `~/Library/Rime/dafeng_learned.dict.yaml`(daemon 写的)—— 你的
- `~/Library/Rime/wubi86_dafeng.dict.yaml`(combined dict 模板)——
  deploy.sh 会覆盖,无所谓
- `~/Library/Rime/wubi86_dafeng.schema.yaml`——同上
- `~/Library/Rime/lua/dafeng_*` 和 `dafeng_lua_bridge.so`——同上
- `~/Library/Rime/rime.lua`——同上

deploy.sh 安全幂等。daemon binary 重编后不需要重新部署 schema(只
当 schema/lua/dict 文件本身改了才需要)。

## 9. 我没做、你可能想做的

- **MLX 真模型接进 reranker**:`tools/download_model.py` 拉
  Qwen 2.5 0.5B 4-bit,然后 daemon `--backend mlx --model-path ...`。
  目前 mlx wrapper 还是 deterministic fallback 在打分,真 transformer
  forward pass 是 Phase 2.2.F follow-up。
- **学习轮自动周期跑**:Phase 3 的 cron / idle 触发。现在手动
  `dafeng-cli learn`。
- **Windows 端**:全部 stub 已就绪,Endpoint/Path 加 Win 实现就能跑。
- **删过激词**:暂无 UI,直接 sqlite3 删 commits 行 + 删
  `dafeng_learned.dict.yaml` 对应行 + 重新部署。
