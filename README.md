# 大风五笔 (Dafeng Wubi)

> 一个**懂你**的本地五笔输入法。
> 你怎么打,它怎么学,数据**不离开**你的电脑。

[![CI](https://github.com/kevin/dafeng-wubi/actions/workflows/ci.yml/badge.svg)](https://github.com/kevin/dafeng-wubi/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![macOS](https://img.shields.io/badge/macOS-14%2B-lightgrey.svg)]()

---

## 它是什么

基于 [RIME](https://rime.im) / Squirrel 的五笔输入法,加了一个本地常驻的
"大脑"daemon,做两件事:

1. **重排候选** —— 你打的码出来的字按上下文重新排,常用的、连贯的往前。
2. **学新词** —— 你拆开打的字组合(比如 `xu` 选 弹 + `pw` 选 窗),
   它发现这是你想要的 `弹窗`,自动把它当作五笔短语码 `xupw` 写进
   词库,下次直接出。

不是把你的键盘流上传给云端 AI 的那种"智能",是在你机器里跑、用
你自己历史数据训练你自己的五笔。

## 为什么不一样(对比纯 RIME)

完整 32 项见 [docs/vs-vanilla-rime.md](docs/vs-vanilla-rime.md)。这里
是用户能直接感受到的几个核心点:

- **上下文感知重排** —— 看你光标前 N 字给候选重新排;LLM 后端用
  Qwen 2.5 0.5B Q4 + Metal 实时打分。纯 RIME 只看当前码、用静态字频。
- **真五笔学新词** —— 你拆开打的"开干"会被识别,按真五笔规则合成
  `gafg` 写进字典。`今天我们 → wgtw`、`这个 → ypwh` 都来自这套规则。
  纯 RIME 必须用户手动造词。
- **全自动学习 + 部署** —— daemon 在你空闲 30s+ 时自己跑 learning
  round + 调 `rime_deployer` 重建二进制 + 桌面通知。**你只要打字,新
  词自动出现**;不再需要手动 `dafeng-cli learn` + 点"重新部署"。
- **30 ms 硬切** —— daemon 慢/挂了 IME 直接走原 RIME 候选,绝不卡输入。
- **隐私本地**:`history.db` 永远不离开本机。git 同步默认关闭,即使开
  也只同步聚合统计 + 学到的词典,**不**传 raw 输入。
- **可观测**:CLI `dafeng-cli stats / history / learn`,SwiftUI 桌面
  Inspector(状态 / 历史 / 学到的词三 tab),纯 RIME 这一层基本是黑盒。
- **可拒绝**:每条 commit 在你 SQLite 里,不喜欢的词 Inspector 一键
  删,或直接 SQL。无云、无账号、无锁定。

## 安装(macOS,三种方式任选)

需要 macOS 14+,Apple Silicon 或 Intel。Squirrel 输入法必须先装好
(`brew install --cask squirrel` 然后在"系统设置 → 键盘 → 输入源"启用)。

### A. `.pkg` 双击安装(给非开发者)

从 [Releases](https://github.com/kevin/dafeng-wubi/releases) 下载
对应架构的 `.pkg`,双击。安装界面里有 **"Customize" 面板**,可以勾选
**"LLM rerank backend (~390 MB download)"** 开启 LLM 后端
(下载 Qwen 2.5 0.5B Q4 模型,装完自动切到 LLM)。

### B. Homebrew(给 brew 用户)

```bash
brew install <owner>/dafeng/dafeng-wubi
dafeng-cli setup                        # 基础(deterministic 后端)
# 想要 LLM:
python3 -m pip install --user 'huggingface_hub[cli]'
hf download Qwen/Qwen2.5-0.5B-Instruct-GGUF \
    --include qwen2.5-0.5b-instruct-q4_k_m.gguf \
    --local-dir "$HOME/Library/Application Support/Dafeng/models/Qwen2.5-0.5B-Instruct-GGUF"
dafeng-cli setup --backend llama_cpp
```

### C. 源码编译(给开发者 / 贡献者)

```bash
git clone https://github.com/kevin/dafeng-wubi.git
cd dafeng-wubi
./install.sh                            # 一条命令搞定
```

`install.sh` 会装 brew 依赖、拉 wubi86 词库、编 dafeng、部署、装 launchd。

### 任何方式装完后

1. 菜单栏鼠须管 → 重新部署(**仅一次** — 让 RIME 加载我们的 schema)
2. Ctrl + ` 选 "大风五笔 86"
3. 开始打字 — 之后**完全不用管**。daemon 在后台学你打的词组,空闲
   时自己跑一轮学习 + 重新部署 + 桌面通知,新词就出现在候选里。

## 日常用法

通常你**不需要**手动跑任何 CLI。下面这些是你想看里面发生了什么时用的:

```bash
# 查 daemon 健康(uptime / latency / 计数 / 错误)
dafeng-cli stats

# 查最近输入
dafeng-cli history --limit 30

# 看后台自动学习的进度(grep daemon log)
tail -f ~/Library/Logs/dafeng-daemon.err.log | grep auto-learn

# 强制立即跑一轮学习(若不想等空闲)
dafeng-cli learn --min-freq 2
```

桌面 GUI 入口:`/Applications/Dafeng Inspector.app` —— 三个 tab 看
状态、输入历史、学到的词,可以一键删错词。

完整速查见 [docs/cheat-sheet.md](docs/cheat-sheet.md)。

## 数据流(给爱看的人)

```
你按键
  ↓
Squirrel/librime → 五笔候选
  ↓
我们的 Lua filter → 找 daemon 重排(30 ms 超时降级)
  ↓
你选词,Squirrel commit
  ↓
Lua hook → daemon 写 history.db (本机 SQLite, 0600)
                                    ↓
                          daemon 后台 auto-learn(空闲 30s+ 时):
                          history → n-gram + 拆字短语发现 →
                          按真五笔规则合成短语码 →
                          写 dafeng_learned.dict.yaml →
                          调 rime_deployer 重建 prism/table →
                          (可选) git commit 到 userdata 仓库 →
                          桌面通知"已学到 N 个新词"
                                    ↓
                          下次切输入法即生效,新词作为正常候选出现
```

## 架构 / 设计文档

- [DESIGN.md](DESIGN.md) — 完整架构,性能预算,隐私边界,phase 拆分
- [docs/vs-vanilla-rime.md](docs/vs-vanilla-rime.md) — **完整 32 项功能对比纯 RIME**
- [docs/cheat-sheet.md](docs/cheat-sheet.md) — 日常使用 / 故障排查
- [docs/phase-2-summary.md](docs/phase-2-summary.md) — Phase 2 实现纪要
- [docs/phase-2.1-verification.md](docs/phase-2.1-verification.md) — IPC 骨架验证
- [docs/phase-2.2.md](docs/phase-2.2.md) — Reranker 抽象 + MLX 接入
- [docs/phase-2.4.md](docs/phase-2.4.md) — 学习子系统
- [docs/phase-3.2.md](docs/phase-3.2.md) — 真 LLM 接入(llama.cpp + Qwen)
- [docs/phase-3.3.md](docs/phase-3.3.md) — Windows port scaffold
- [docs/phase-3.4.md](docs/phase-3.4.md) — SwiftUI 监控面板
- [docs/phase-3.5.md](docs/phase-3.5.md) — 三通道分发 + self-contained pkg
- [CLAUDE.md](CLAUDE.md) — 项目宪法(给协作者看的硬约束)

## 性能

实测(M-series, RelWithDebInfo):

```
IPC P99 (in-process)             : 14 µs    (DESIGN 预算 5 000 µs)
Reranker P99 (deterministic)     :  3 µs    (DESIGN 预算 30 000 µs)
Reranker P99 (llama_cpp 热路径)  :  ~5 ms   (DESIGN 预算 30 000 µs)
ctest                             : 152 / 152 pass in ~2 s
学习轮 (60+ commits)              : 30 ms 全跑完
```

## 当前阶段 / 路线图

- ✅ **Phase 2** —— IPC、reranker 抽象、observability、SQLite history、
  n-gram 学习、新词发现、五笔规则编码、git 同步骨架。
- ✅ **Phase 3.1** —— LICENSE + install.sh + CI + 用户级文档。
- ✅ **Phase 3.2** —— 真 LLM:Qwen 2.5 0.5B Q4 GGUF 通过 llama.cpp +
  Metal 接入,P99 ~5 ms 热路径。详见 [docs/phase-3.2.md](docs/phase-3.2.md)。
- 🚧 **Phase 3.3 进行中** —— Windows port 框架已落地:[endpoint_win.cc](src/common/endpoint_win.cc)
  真实 Named Pipe(OVERLAPPED I/O + 超时 + 跨线程 shutdown);
  [paths_win.cc](src/common/paths_win.cc) 真实 `SHGetKnownFolderPath` +
  per-user pipe 命名;daemon main `SetConsoleCtrlHandler`。
  CI (`windows-latest`) 已加,编译验证通过。还差:Weasel 集成、NSIS
  安装包、Scheduled Task 自启、端到端真机回归。详见 [docs/phase-3.3.md](docs/phase-3.3.md)。
- ✅ **Phase 3.4** —— SwiftUI 监控面板 [大风五笔检查器](apps/inspector/)
  随 `.pkg` 装到 `/Applications/Dafeng Inspector.app`,三个 tab:状态、
  输入历史、已学到的词(含删除 + 重新部署提示)。详见 [docs/phase-3.4.md](docs/phase-3.4.md)。
- ✅ **Phase 3.5 + 3.5+** —— `.pkg` / Homebrew / install.sh 三通道分发,
  自包含 dylib bundling(不需要目标机器有 brew)。详见 [docs/phase-3.5.md](docs/phase-3.5.md)。
- ✅ **自动学习(刚完成)** —— daemon 后台 idle-watcher 线程,空闲时
  自动跑 LearningRound + `rime_deployer` + 桌面通知。用户从此**只需打字**,
  不用再手动 `learn` 或重新部署。详见 [src/daemon/auto_learn.h](src/daemon/auto_learn.h)。

## 贡献

读 [CONTRIBUTING.md](CONTRIBUTING.md)。简短版:

- 一次一个逻辑改动 + 测试覆盖
- C++17 + clang-format + `pragma once`
- 不接收任何把 `history.db` 上传远程的 PR
- 不接收 telemetry / 拨家用 / 云端依赖在热路径

## 隐私声明

这个项目**不**做以下事情:

- 不上传你的输入历史
- 不做崩溃数据回传
- 不收集使用统计
- 不需要账号 / 注册 / 激活

git 同步是**默认关闭**的可选功能,需要你主动配置一个**自己的**
私有 git 仓库 URL **并且**显式打开 `push_to_remote=true` 才会发出
任何网络请求。即使打开,同步的也只是聚合频率统计 + 用户词典,
**不**包含 raw input。

## 鸣谢

- [RIME](https://rime.im) / [Squirrel](https://github.com/rime/squirrel) ——
  这个项目骑在他们的引擎上
- [rime-wubi](https://github.com/rime/rime-wubi) —— wubi86 码表
- [Apple MLX](https://github.com/ml-explore/mlx) —— 本地 LLM 推理
- [msgpack](https://msgpack.org/) / [SQLite](https://www.sqlite.org) /
  [libgit2](https://libgit2.org/) / [Lua](https://www.lua.org)

## License

[Apache License 2.0](LICENSE) — 见 [NOTICE](NOTICE) 中的第三方依赖清单。
