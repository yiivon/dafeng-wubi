# CLAUDE.md

> 这个文件是给 Claude Code 看的项目宪法。每次会话开始时,Claude Code 会自动读取此文件。
> 维护者:Kevin。修改前请确保改动经过深思熟虑,不要让 Claude Code 自己改这个文件。

---

## 项目身份

**大风五笔 (Dafeng Wubi)** — 一个跨平台、能学习、能预测的五笔输入法。

- **代号 / 命名空间**:`dafeng`
- **目标平台**:macOS (优先) + Windows
- **核心理念**:用得爽 > 技术炫技。LLM 是加分项,不是主角。

完整设计请读 `DESIGN.md`。本文档只讲**怎么干活**。

---

## 不可妥协的核心原则

按重要性排序。冲突时上面优先。

### 1. 用户输入永不被 LLM 拖慢

- `dafeng_filter` 等待 daemon 响应的超时**硬上限 30ms**
- 超时必须**安静降级**,用 RIME 原始候选,不弹窗、不警告
- 任何让用户感知到"卡顿"的修改都视为 bug,不论 LLM 准确性如何
- **判断标准**:就算 daemon 完全宕机,IME 也必须照常工作,只是没有智能加成

### 2. 隐私边界绝对不破

- `history.db` 含原始输入历史,**永远不能进 Git 仓库**,不能上传任何远程
- 只有 `userdata/` 目录里的脱敏词条可以同步
- 不写任何遥测、统计上报、崩溃数据回传(除非明确征得用户同意)
- 写代码时若涉及网络请求,先停下来确认是否符合此原则

### 3. 五笔用户的肌肉记忆不能破坏

- 候选前 3 位的位置变动率每周必须 < 10%
- 已经稳定使用过 N 次的词(默认 N=5),不参与 LLM 重排
- 学习子系统对高频词必须保守,只对中低频词积极调整

### 4. 双进程边界严格

- IME 进程不加载 LLM 模型、不开线程池、不依赖 Python
- daemon 进程不直接操作 IME UI、不读取按键事件
- 二者只通过 IPC 通信,接口在 `include/dafeng/protocol.h` 里定义
- 想跨界?先开 issue 讨论,不要直接改

---

## 技术栈与版本约束

| 类别 | 选型 | 备注 |
|------|------|------|
| 语言 | C++17 | 不用 C++20,librime 还没准备好 |
| 构建 | CMake ≥ 3.20 | |
| Mac 推理 | MLX (Apple) | Phase 2.2 引入 |
| Win 推理 | llama.cpp | 后期引入,先抽象接口 |
| 序列化 | MessagePack (msgpack-c) | 不用 JSON、不用 protobuf |
| 数据库 | SQLite (vendored) | history.db 用 |
| Git | libgit2 | daemon 内置 |
| 测试 | GoogleTest + 自建 benchmark | |
| 模型 | Qwen 2.5 0.5B Instruct, 4-bit AWQ | 热路径首选 |

**严禁未经讨论引入**:Boost (除 header-only 的小工具)、Qt、Electron、任何带 GC 的语言运行时进入热路径。

---

## 仓库与目录约定

```
dafeng-wubi/                  # 主仓库 (本仓库)
├── CLAUDE.md                 # 你正在读的文件
├── DESIGN.md                 # 完整设计文档
├── README.md                 # 给人类用户看的
├── include/dafeng/           # 公共头文件 (插件 + daemon 共享)
├── src/
│   ├── plugin/               # librime filter (跑在 IME 进程)
│   ├── client/               # IPC 客户端库 (跑在 IME 进程)
│   ├── daemon/               # daemon 主程序 (独立进程)
│   └── common/               # 两边共享的协议、日志、工具
├── schemas/                  # RIME schema YAML
├── tests/{unit,integration,benchmarks}/
├── tools/                    # Python 工具脚本 (量化、benchmark 报告)
└── third_party/              # 全部用 git submodule

dafeng-userdata/              # 独立私有仓库 (用户词库,跨设备同步)
```

**严格约束**:
- `src/plugin/` 和 `src/client/` 里的代码**不能**依赖 `src/daemon/` 的任何东西
- `src/common/` 不能反向依赖任何上层目录
- 头文件只放在 `include/`,源文件只放在 `src/`,不要在 `src/` 里放 `.h`

---

## 常用命令

### 构建

```bash
# Mac (Apple Silicon)
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DDAFENG_ENABLE_MLX=ON
cmake --build build -j

# Windows (PowerShell)
cmake -B build -G "Visual Studio 17 2022" `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DDAFENG_ENABLE_LLAMA_CPP=ON
cmake --build build --config RelWithDebInfo
```

### 测试

```bash
# 单元测试
ctest --test-dir build --output-on-failure

# IPC 性能 benchmark
./build/tests/benchmarks/ipc_pingpong --iterations 1000

# 端到端推理 benchmark (Phase 2.2 后可用)
./build/dafeng-daemon --benchmark --model models/qwen2.5-0.5b-q4.mlx
```

### 调试 daemon

```bash
# 前台运行,看日志
./build/dafeng-daemon --foreground --log-level debug

# 检查 IPC 是否通
./build/dafeng-cli ping
```

### librime 联调

```bash
# 部署 schema 到本地 RIME (Mac)
cp schemas/wubi.schema.yaml ~/Library/Rime/
cp build/src/plugin/librime_dafeng.dylib ~/Library/Rime/
# 在系统输入法菜单里点 "重新部署"

# Windows
copy schemas\wubi.schema.yaml %APPDATA%\Rime\
copy build\src\plugin\rime_dafeng.dll %APPDATA%\Rime\
```

---

## 任务执行约定

### 开始任务前

1. 先读 `DESIGN.md` 对应章节,确认方案没漂移
2. 看 `docs/adr/` 里有没有相关决策记录
3. 跑一次现有测试,确认 baseline 是绿的
4. 如果任务涉及性能,先记录当前 benchmark 数据

### 任务进行中

1. **小步提交**:每个逻辑独立的改动一个 commit,commit message 用中文或英文都行,但要写清楚"做了什么 + 为什么"
2. **测试先行,但务实**:核心逻辑必须有单测,UI 类、IPC 类可以先用集成测试覆盖。不要为了 100% 覆盖率写无意义的 mock
3. **遇到不确定的设计问题,停下来问**,不要自己拍板。具体看下面"决策升级"
4. **不修改的不要碰**:不要顺手"重构"无关代码、不要顺手升级依赖版本、不要顺手改 lint 规则
5. 改了任何**接口**(headers / 协议 / schema YAML)后,必须更新 `DESIGN.md` 对应章节

### 任务完成时

1. 跑全部测试 + 相关 benchmark
2. 如果是性能敏感的改动,在 PR / commit message 里贴 benchmark 对比数据
3. 更新 `README.md` 的相关说明(如果用户可见的行为有变化)
4. 列出本次改动**没有覆盖**的边界情况,留给后续

---

## 决策升级 (Escalation)

遇到下面这些情况,**停下来问 Kevin**,不要自己决定:

- 性能不达标,需要修改架构(比如 30ms 跑不进去)
- 引入新的第三方依赖
- 修改 IPC 协议的字段
- 修改 `include/dafeng/` 下的公共头文件
- 修改 schema YAML 的 key 名
- 任何涉及隐私边界的改动(history.db 访问、网络请求等)
- 测试或 benchmark 用例的判定标准变化

升级时附上:
1. 你想做什么
2. 为什么需要这么做
3. 至少 2 个备选方案及取舍
4. 你的推荐及理由

---

## 性能预算 (Reminder)

完整版见 `DESIGN.md` §3。短版:

```
按键 → 候选展示总耗时:< 100ms
其中 LLM 推理:< 30ms 硬切
其中 IPC 往返:< 5ms
其中候选重排:< 1ms
```

**任何 commit 引入回归 > 5ms 都算事故**,需要在 commit message 里说明并讨论。

---

## 风格与代码规范

- 文件编码 UTF-8,LF 换行
- C++ 风格:Google C++ Style 的简化版,见 `.clang-format`
- 命名:类用 `PascalCase`,函数和变量用 `snake_case`,常量用 `kSomeValue`
- 注释:**写"为什么"而不是"是什么"**;接口注释用英文,实现细节注释中英都行
- 不要写无意义的注释(`// increment i`),不要把注释当 commit log 用
- C++ 头文件用 `#pragma once`,不用 include guard 宏
- 错误处理优先用 `std::optional` / `expected`,慎用异常(librime 自身基本不抛)

---

## 已知陷阱

写在这里是因为踩过了。新人(包括 Claude Code)容易撞:

1. **librime 的 `Translation` 是惰性求值的**:不能简单 `std::move`,要按其迭代器协议来。具体看 librime 源码 `src/rime/translation.h`。
2. **macOS IME 进程在沙盒里**:不能随便读 `~/Documents` 等目录,只能访问自己的 Application Support。daemon 没有这个限制,所以涉及文件 IO 的尽量放 daemon 端。
3. **Squirrel 的 `commit_history` 是字符串而非 token 流**:取"光标前 N 字"时要按 utf-8 字符边界切,不能按 byte 切。
4. **MessagePack 的 int 默认是有符号的**:序列化 size_t 之类的要显式 cast。
5. **MLX 模型加载是异步的**:daemon 启动后第一次推理可能慢 200ms+,需要在启动时预热(发一个 dummy 请求)。

---

## 当前阶段

> 维护提示:每完成一个 Phase,更新这一节。

**当前 Phase**:Phase 2.1 (IPC 骨架)
**目标**:打通 IME ↔ daemon 通信,**不含 LLM**
**完成标准**:Mac 上能看到 daemon 在影响候选顺序,IPC 往返 P99 < 5ms

详见 `DESIGN.md` §8.

---

## 联系方式

主要决策者:Kevin。所有"我不确定该不该这么做"的事,直接问。
