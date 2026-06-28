# 性能对标策略

更新时间：2026-06-27

## 落地顺序（先商业、后性能）

| 轨 | 内容 | 默认 CI |
|---|---|---|
| **P0 商业** | Hotfix、热重载、加固、访问控制、Assembly 缓存、栈诊断等 | `validate-reports` **默认只卡此项** |
| **P1 性能架构** | callsite 预烘焙 → typed ABI → register/array IR | `local-benchmark` + 显式 `HOTC233_ENFORCE_PERFORMANCE=1` |
| **P2 性能细节** | peephole、单 opcode 微调 | **禁止**在 P1 桶未绿前做 |

**200% 规则**：任一条 base 行 `hotc233PercentOfHybridClr < 50%`（慢于社区版一倍以上）→ **架构方向错误**，停止细节优化，换桶并写入 `pro-wrong-answer-notebook.md`。

## 北极星（唯一终态）

**HybridCLR Pro 纯解释器 + 公开商业能力。**

| 维度 | 目标 |
|---|---|
| 商业 P0 | `pro-landing-matrix.json` 核心项 `landed`（默认门禁） |
| 性能终态 | 14 条官方 base：`hotc233PercentOfProfessionalTarget >= 100%` |
| 内存 | Pro 公开预算：指令 ~700KB、线程 ~1.2MB、元数据节省 10–25% |

## 社区版：绝对必须全面超越（性能轨，非默认 CI）

hotc233 运行时 **从 HybridCLR 社区版代码 fork 改造**。因此：

- **同机、同环境、同 14 条 benchmark 下，hotc233 必须在每一条 base 行上全面快于 HybridCLR 8.11.0 社区版**（`hotc233OpsPerSecond > hybridClrOpsPerSecond`，即 `hotc233PercentOfHybridClr > 100%`）。
- **任一条慢于或等于社区版，即判定为方向错误**——不是「差 10% 可接受」，而是解释器基础层还没做到 fork 应有的下限，必须先回头对齐社区版已具备的 Execute / transform / callsite / 数组快路径，再推进 Pro 架构。
- 社区版性能偏低，**绝不是** hotc233 的成功标准；但 **连社区版都赢不了，说明当前改动路线有问题**，禁止用「我们在追 Pro」回避社区版回归。

```text
验收层次（商业默认 / 性能显式）：

  P0  商业能力 8 项（validate-reports 默认）
  L1  全面超越社区版（HOTC233_ENFORCE_BEAT_COMMUNITY=1）
  Dominance  Pro 量级碾压社区版（HOTC233_ENFORCE_DOMINANCE=1；typeof 1000% 等）
  L2  逼近 / 达到 Pro 纯解释目标（HOTC233_ENFORCE_PERFORMANCE=1 + 架构桶全绿）
```

一键自审：`go run ./tools/hotc233ctl arch-self-review`（见 `benchmark-docs/arch-self-review.md`）。

## 同机对比要求

1. 同一台物理机、同一 Tuanjie 小版本、同一 IL2CPP Player 平台；WebGL 专项才要求同一浏览器。
2. hotc233 与 HybridCLR 各用 **独立 Unity 工程**（本 demo **零** HybridCLR 安装；社区版只在 `hybridclr-benchmark-demo`）。
3. 本机轻量对比表必须同时包含：hotc233 实测、HybridCLR 社区版实测、Pro 目标估算。
4. WebGL 专项对比表再额外包含原生 WebGL IL2CPP，并归档到 `benchmark-docs/results/latest-hotc-vs-hybridclr.md`。
5. 每次对标后按 [`benchmark-docs/reporting-requirements.md`](reporting-requirements.md) 向负责人汇报全量 14 行表，并注明 `HOTC233_ENABLE_OPCODE_PROFILER` / `-hotc233-opcode-profile` 是否开启。

## 验收门禁

`hotc233ctl validate-reports` **默认**检查：

- `verification-report.json` 商业 8 项 + 功能/对比报告
- `feature-report.md` 商业段（标准解释/离线/泛型 WebGL/元数据体积项不阻塞 P0）

**性能轨**（显式 `HOTC233_ENFORCE_PERFORMANCE=1`）额外检查：

- `RuntimeFast` loader profile
- base 表 14 行、GC/memory 快照
- `benchmark-docs/results/latest-hotc-vs-hybridclr.json` 存在且未 stale

**社区版全面超越（L1）**：

- 默认：归档对比表中 **任一条** `hotc233PercentOfHybridClr <= 100%` 时在日志 **警告** 列出。
- 设置 `HOTC233_ENFORCE_BEAT_COMMUNITY=1` 时：**任一条未全面超越社区版即失败**。

**Pro 目标（L2）**：

- 设置 `HOTC233_ALLOW_PRO_TARGET_GAP=1` 仅表示允许继续生成报告做迭代，**不代表** Pro 或社区版验收通过。

## 优化路线（专用架构优先，禁止 dispatch 微优化）

**已废弃为性能路线**：通用 `Interpreter_Execute` dispatch 优化、M2N 桥接热路径、threaded dispatch、在错误 IR 上修 interp fallback、`PRO_EXPERIMENTAL_TRANSFORM`、RegI32 全局 lowering。归档说明：`benchmark-docs/archive/generic-dispatch-bridge-retired.md`。

```text
P0  商业能力                     Hotfix/热重载/加固/访问控制/Assembly 缓存/栈诊断
P1  14 base 专用 transform       TryBuildGodDomain* / trace fold / Instinct → minimal IR
P2  whole-method execute bypass  TryExecuteHotc233FastPath；fastPathKind 验收
P3  Typed ABI（V3/Quaternion）   struct-by-value direct；禁止 M2N 占热路径
P4  冷路径通用解释器            仅正确性；禁止 P1 微优化堆在 switch 上
```

若某 base 行 profile 仍为通用 dispatch opcode 或 `fastPathKind=1`：**只推进 P1 专用 builder**，禁止改 Execute switch case。

权威矩阵：`benchmark-docs/god-domain-architecture.md`。

## 对外口径

- 不说「已超越 HybridCLR Pro」，除非 L2 全绿。
- 不说「社区版不重要」——社区版是 **绝对必须赢** 的 fork 基线（性能轨）。
- 商业能力可独立宣称「已落地」，与 L1/L2 性能轨分离。
- 日常性能表必须三列：hotc233、社区版实测、Pro 目标；WebGL 专项表再加入 native IL2CPP。
