# 对比纯 RIME

大风五笔以 [RIME](https://rime.im) 为底,但加了一个独立的 daemon 进程
和它周边的全栈基础设施。这份文档把"我们多了什么"按用户能感受到的层
次列全。每条都带源码位置,方便点过去。

> **TL;DR**:纯 RIME 是「码 → 候选」的查表系统;大风五笔在它上面加了
> **两个反馈环**(rerank + learn),外加**三道工程护栏**(30ms 硬切、
> 隐私本地、全自动)。

---

## A. 输入体验(打字时直接感受到的)

| | 大风五笔 | 纯 RIME |
|---|---|---|
| 1 | **上下文感知重排** — 看光标前 N 字给候选重排 [src/plugin/dafeng_rerank.lua](../src/plugin/dafeng_rerank.lua) | 只看当前码,无上下文 |
| 2 | **真 LLM 评分** — Qwen 2.5 0.5B Q4 + Metal 实时打分 [src/daemon/reranker_llamacpp.cc](../src/daemon/reranker_llamacpp.cc) | 静态字频 |
| 3 | **30ms 硬时限降级** — daemon 慢/挂了,IME 直接走原 RIME [src/plugin/dafeng_filter.lua](../src/plugin/dafeng_filter.lua) | 无 SLA 概念 |
| 4 | **前 N 位保护** — 默认前 3 位永不动,肌肉记忆稳定 [schemas/wubi86_dafeng.schema.yaml](../schemas/wubi86_dafeng.schema.yaml) | 频率排序覆盖任何位置 |
| 5 | **拆字短语回归** — 你拆开打的"开干",`gafg` 真五笔编码会自动出现 [src/daemon/wubi_codec.cc](../src/daemon/wubi_codec.cc) | 用户必须手动造词 |

## B. 学习与适应(后台帮你的)

| | 大风五笔 | 纯 RIME |
|---|---|---|
| 6 | **持久化输入历史** SQLite + WAL [src/daemon/history_store.cc](../src/daemon/history_store.cc) | 只有内存 userdb |
| 7 | **N-gram 自适应** + chained discovery(时间窗口拼相邻提交) [src/daemon/ngram.cc](../src/daemon/ngram.cc) | 无 |
| 8 | **新词频次发现** — `min_freq` 阈值 + ASCII / 重复噪声过滤 [src/daemon/new_word.cc](../src/daemon/new_word.cc) | 无 |
| 9 | **真五笔规则编码** — 2字 `AaAbBaBb` / 3字 `AaBaCaCb` / 4+字 `AaBaCaZa` [src/daemon/wubi_codec.cc](../src/daemon/wubi_codec.cc) | 用户手写 |
| 10 | **完全自动化学习** — 空闲 30s+ 自动跑学 + 重新部署 + 桌面通知 [src/daemon/auto_learn.cc](../src/daemon/auto_learn.cc) | 必须手动点"重新部署" |

## C. 隐私与数据控制

| | 大风五笔 | 纯 RIME |
|---|---|---|
| 11 | **path-escape 守卫** + 0600 / Win ACL 文件权限 [src/daemon/history_store.cc](../src/daemon/history_store.cc) | 无 |
| 12 | **聚合同步,raw 永不出本机** — git 只同步 ngram 聚合 + 学到的词 | userdb 是个文件夹,要么全同步要么不同步 |
| 13 | **Git push 默认 OFF** — 要主动开两道开关 [src/daemon/git_sync.h](../src/daemon/git_sync.h) | 无网络隐私边界 |
| 14 | **GUI 词典编辑** — Inspector 一键删错词 [apps/inspector/Sources/DafengInspector/LearnedDictView.swift](../apps/inspector/Sources/DafengInspector/LearnedDictView.swift) | SQL / 文本编辑 userdb |

## D. 跨平台 / 跨设备

| | 大风五笔 | 纯 RIME |
|---|---|---|
| 15 | **跨平台 IPC 抽象** — Mac UDS + Win Named Pipe 同接口 [include/dafeng/endpoint.h](../include/dafeng/endpoint.h) | N/A |
| 16 | **多机自动 git 同步用户词库** [src/daemon/git_sync.cc](../src/daemon/git_sync.cc) | 自己 rsync userdb |
| 17 | **Windows 端骨架就位**(Named Pipe + ConsoleCtrlHandler + paths_win) [src/common/endpoint_win.cc](../src/common/endpoint_win.cc) | RIME 自己有 Weasel,但需各自配置 |

## E. 可观测性

| | 大风五笔 | 纯 RIME |
|---|---|---|
| 18 | **运行时 stats** — uptime / latency mean&max / 计数 / 错误 / 模型版本 [src/cli/main.cc](../src/cli/main.cc) `stats` | 几乎不可观测 |
| 19 | **SwiftUI 监控面板** — 状态 / 历史 / 学到的词三个 tab [apps/inspector/](../apps/inspector/) | 无 |
| 20 | **降级日志** `~/Library/Logs/dafeng-filter.log` [src/plugin/dafeng_rerank.lua](../src/plugin/dafeng_rerank.lua) | 无 |
| 21 | **CLI 工具集** `ping/stats/history/rerank/commit/learn/setup` [src/cli/main.cc](../src/cli/main.cc) | RIME 没有 CLI |
| 22 | **Daemon 健康探测** [src/cli/main.cc](../src/cli/main.cc) `ping` | 无 |

## F. 工程化 / 部署

| | 大风五笔 | 纯 RIME |
|---|---|---|
| 23 | **双进程隔离** — daemon 死了 IME 不死 [src/daemon/main.cc](../src/daemon/main.cc) | 单进程 |
| 24 | **launchd 自启 + KeepAlive** [packaging/macos/com.dafeng.daemon.plist](../packaging/macos/com.dafeng.daemon.plist) | 无 |
| 25 | **三通道分发** `.pkg` / Homebrew / install.sh,自包含 dylib bundling [packaging/macos/build-pkg.sh](../packaging/macos/build-pkg.sh) | 用户自己装 |
| 26 | **Customize 面板 LLM 复选框** — 装时勾选要不要下 390MB Qwen [packaging/macos/build-pkg.sh](../packaging/macos/build-pkg.sh) | N/A |
| 27 | **Mac+Windows CI 矩阵** + 152 单元/集成/基准测试 [.github/workflows/ci.yml](../.github/workflows/ci.yml) | RIME 自己有,但我们这层是新增 |
| 28 | **可插拔推理后端** — mock / deterministic / mlx / llama_cpp 同 IPC 接口 [src/daemon/services.h](../src/daemon/services.h) | 无 |
| 29 | **C ABI 客户端** — Lua bridge 跨 librime 版本工作 [include/dafeng/client_c.h](../include/dafeng/client_c.h) | 无 |

## G. 性能 / 实时性保证

| 指标 | 实测 | DESIGN 预算 |
|---|---|---|
| IPC P99(往返,in-process) | **14 µs** | 5 000 µs |
| Reranker P99(deterministic) | **3 µs** | 30 000 µs |
| Reranker P99(llama_cpp 冷启动) | **~17 ms** | 30 000 µs |
| Reranker P99(llama_cpp 热路径) | **~5 ms** | 30 000 µs |
| 学习轮(60+ commits 全跑完) | **30 ms** | — |
| ctest | **152 / 152** pass in ~2 s | — |

[tests/benchmarks/](../tests/benchmarks/) 里有 `IpcPingpongBudget` 和
`RerankerLatencyBudgetDeterministic` 两个 budget gate,跑在每次 CI,超
预算就直接 red。

---

## 一句话总结

**纯 RIME 是 "码 → 候选" 的查表系统;大风五笔在它上面加了两个反馈环**:

1. **rerank 反馈** — 看你前面打了什么 → LLM 给候选打分 → 排序
2. **learn 反馈** — 看你拆字打的字 → 自动算编码 → 写回字典 → 自动部署

**外加三道工程护栏**:
- **30ms 硬切** — 慢/挂了不卡你
- **隐私本地** — raw 永不出本机
- **全自动** — 从安装到日常使用,你只要打字
