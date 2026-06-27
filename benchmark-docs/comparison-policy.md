# 性能对标策略

更新时间：2026-06-27

## 北极星（唯一终态）

**HybridCLR Pro 纯解释器 + 公开商业能力。**

| 维度 | 目标 |
|---|---|
| 性能终态 | 14 条官方 base benchmark：`hotc233PercentOfProfessionalTarget >= 100%` |
| 内存 | Pro 公开预算：指令 ~700KB、线程 ~1.2MB、元数据节省 10–25% |
| 能力 | `docs/pro-landing-matrix.json` 商业项逐步 `landed` |

## 社区版：绝对必须全面超越（不是参考线）

hotc233 运行时 **从 HybridCLR 社区版代码 fork 改造**。因此：

- **同机、同环境、同 14 条 benchmark 下，hotc233 必须在每一条 base 行上全面快于 HybridCLR 8.11.0 社区版**（`hotc233OpsPerSecond > hybridClrOpsPerSecond`，即 `hotc233PercentOfHybridClr > 100%`）。
- **任一条慢于或等于社区版，即判定为方向错误**——不是「差 10% 可接受」，而是解释器基础层还没做到 fork 应有的下限，必须先回头对齐社区版已具备的 Execute / transform / callsite / 数组快路径，再推进 Pro 架构。
- 社区版性能偏低，**绝不是** hotc233 的成功标准；但 **连社区版都赢不了，说明当前改动路线有问题**，禁止用「我们在追 Pro」回避社区版回归。

```text
验收层次（由严到宽，每一层都必须过）：

  L1  全面超越社区版（绝对门槛，14/14 条 base 必须 hotc233 更快）
  L2  逼近 / 达到 Pro 纯解释目标（架构终态）
  L3  商业能力与内存预算（pro-landing-matrix + Pro 公开内存口径）
```

## 同机对比要求

1. 同一台物理机、同一 Tuanjie 小版本、同一 IL2CPP Player 平台；WebGL 专项才要求同一浏览器。
2. hotc233 与 HybridCLR 各用 **独立 Unity 工程**（本 demo **零** HybridCLR 安装；社区版只在 `hybridclr-benchmark-demo`）。
3. 本机轻量对比表必须同时包含：hotc233 实测、HybridCLR 社区版实测、Pro 目标估算。
4. WebGL 专项对比表再额外包含原生 WebGL IL2CPP，并归档到 `benchmark-docs/results/latest-hotc-vs-hybridclr.md`。

## 验收门禁

`hotc233ctl validate-reports` 检查：

- `RuntimeFast` loader profile
- base 表 14 行齐全
- `benchmark-docs/results/latest-hotc-vs-hybridclr.json` 存在且未 stale

**社区版全面超越（L1）**：

- 默认：归档对比表中 **任一条** `hotc233PercentOfHybridClr <= 100%` 时在日志 **警告** 列出。
- 设置 `HOTC233_ENFORCE_BEAT_COMMUNITY=1`（或 `HOTC233_ENFORCE_COMMUNITY_FLOOR=1` 兼容旧名）时：**任一条未全面超越社区版即失败**。

**Pro 目标（L2）**：

- 设置 `HOTC233_ALLOW_PRO_TARGET_GAP=1` 仅表示允许继续生成报告做迭代，**不代表** Pro 或社区版验收通过。

## 优化路线（稳定性优先）

```text
Stage A  Typed Register IR
Stage B  Typed ABI Callsite
Stage C  Typed Array Memory
Stage D  Pro 商业能力
```

若 L1 未过：优先 Execute 对齐、社区版已有快路径、opcode/transform 差异（`hybridclr-diff-opcodes`），**禁止**跳过 L1 直接堆 Pro 实验 pass。

## 对外口径

- 不说「已超越 HybridCLR Pro」，除非 L2 全绿。
- 不说「社区版不重要」——社区版是 **绝对必须赢** 的 fork 基线。
- 日常性能表必须三列：hotc233、社区版实测、Pro 目标；WebGL 专项表再加入 native IL2CPP。
