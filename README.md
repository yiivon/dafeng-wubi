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

## 为什么不一样

- **隐私本地**:`history.db` (你的全部输入历史) 永远不离开本机。
  跨设备同步走 git 私有仓库,只同步聚合统计,不传原始输入。
  默认连本地 git push 都关着,要明确开两道开关才会有任何网络流量。
- **不卡你**:候选窗的硬时限是 30 ms。daemon 慢/挂了,IME 直接走
  原 RIME,绝不让 LLM 拖累打字。
- **真五笔**:学到的词按真五笔编码规则合成短语码。`今天我们` 学到了
  就是 `wgtw`,`这个` 是 `ypwh`——你日后用真五笔打法就能调出来。
- **可拒绝**:每个 commit 都在你的 SQLite 里,不喜欢的词直接 SQL
  删掉。无云、无账号、无锁定。

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

1. 菜单栏鼠须管 → 重新部署
2. Ctrl + ` 选 "大风五笔 86"
3. 开始打字 — daemon 在后台学你打的词组,周末跑一次
   `dafeng-cli learn` 把学到的词推回字典

## 日常用法

```bash
# 查 daemon 健康
./build/src/cli/dafeng-cli stats

# 查最近输入
./build/src/cli/dafeng-cli history --limit 30

# 跑学习轮(每周一次,或想起来跑)
./build/src/cli/dafeng-cli learn --min-freq 2
# 之后菜单栏鼠须管 → 重新部署,新词就生效了
```

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
                          dafeng-cli learn (周期性):
                          history → n-gram + 拆字短语发现 →
                          按真五笔规则合成短语码 →
                          写 dafeng_learned.dict.yaml →
                          (可选) git commit 到 userdata 仓库
                                    ↓
                          下次重新部署,RIME 重编 dict,
                          学到的词作为正常候选出现
```

## 架构 / 设计文档

- [DESIGN.md](DESIGN.md) — 完整架构,性能预算,隐私边界,phase 拆分
- [docs/cheat-sheet.md](docs/cheat-sheet.md) — 日常使用 / 故障排查
- [docs/phase-2-summary.md](docs/phase-2-summary.md) — Phase 2 实现纪要
- [docs/phase-2.1-verification.md](docs/phase-2.1-verification.md) — IPC 骨架验证
- [docs/phase-2.2.md](docs/phase-2.2.md) — Reranker 抽象 + MLX 接入
- [docs/phase-2.4.md](docs/phase-2.4.md) — 学习子系统
- [CLAUDE.md](CLAUDE.md) — 项目宪法(给协作者看的硬约束)

## 性能

实测(M-series, RelWithDebInfo, 全套 144 tests):

```
IPC P99 (in-process)    : 14 µs   (DESIGN 预算 5 000 µs)
Reranker P99 (deterministic): 3 µs (DESIGN 预算 30 000 µs)
ctest                    : 144 / 144 pass in 1.7 s
学习轮 (60+ commits)     : 30 ms 全跑完
```

## 当前阶段 / 路线图

- ✅ **Phase 2 完成** —— IPC、reranker 抽象、observability、SQLite history、
  n-gram 学习、新词发现、五笔规则编码、git 同步骨架。
- ✅ **Phase 3.1 完成** —— LICENSE + install.sh + CI + 用户级文档。
- 🚧 **Phase 3.2(下一站)** —— 真 LLM:把 Qwen 2.5 0.5B 的 transformer
  forward pass 接进 MLX backend。目前 `--backend mlx` 已通,只是 scoring 还
  delegate 到 deterministic。
- 📋 **Phase 3.3** —— Windows port(`endpoint_win.cc` / `paths_win.cc` 真实
  实现 + Named Pipe 后端 + 服务自启)。
- 📋 **Phase 3.4** —— GUI 监控/编辑面板(SwiftUI)。

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
