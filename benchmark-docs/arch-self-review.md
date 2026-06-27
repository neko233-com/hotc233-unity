# 架构自审（全自动）

更新时间：2026-06-27

## 目标档位（对齐 HybridCLR Pro 量级）

| 档位 | 含义 | 默认门禁 |
|---|---|---|
| **L1** | 14 条 base 全面快于社区版 (`>100%`) | `HOTC233_ENFORCE_BEAT_COMMUNITY=1` |
| **Dominance** | 相对社区版 **2–10×**（typeof 目标 **1000%**） | `HOTC233_ENFORCE_DOMINANCE=1` |
| **L2** | 达到 Pro 纯解释器目标区间 | `HOTC233_ENFORCE_PERFORMANCE=1` |

「只赢社区版 100%」不够；Pro 在 typeof 等行可达社区版 **10×**，hotc233 终态必须按 **Dominance** 档位推进。

## 一键自审

```powershell
cd tools/hotc233ctl

# 商业 P0 + 同机 14 条 base + 架构队列 + Dominance 报告
go run . arch-self-review -project D:\Code\neko233-Projects\unity-hotc233-demo `
  -loader-profile RuntimeFast `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo `
  -editor "D:\IDE\TuanJieEditor\2022.3.62t10\Editor\Tuanjie.exe"

# 硬门禁（CI / 发版前）
$env:HOTC233_ENFORCE_DOMINANCE='1'
go run . arch-self-review -project D:\Code\neko233-Projects\unity-hotc233-demo -loader-profile RuntimeFast
```

## 输出

- 临时：`Assets/EditorForBuild/Generated/arch-self-review-report.{json,md}`
- 归档：`benchmark-docs/results/arch-self-review-latest.{json,md}`
- 性能源表：`performance-local-hotc-vs-hybridclr-base.*`

## 自审循环

1. `arch-self-review` 生成最弱行 + 架构桶队列（`pro-landing-matrix.json` `architecturePhases`）。
2. 只改 **whole-method fast path / offline trace / typed ABI**；禁止未绿桶上的 opcode 微优化。
3. 每完成一个 Dominance 桶：`local-benchmark` 全表 → 归档 → **git push 一个版本**。
4. Dominance 全绿后再开 `HOTC233_ENFORCE_PERFORMANCE=1` 追 Pro 文档区间。

## 环境变量

| 变量 | 作用 |
|---|---|
| `HOTC233_ENFORCE_DOMINANCE=1` | Dominance 未达标则 exit 1 |
| `HOTC233_ARCH_SKIP_BENCHMARK=1` | 只读已有 `performance-local-*` 报告 |
| `HOTC233_ENFORCE_BEAT_COMMUNITY=1` | L1 绝对门槛 |

Dominance 目标定义：`docs/pro-landing-matrix.json` → `communityDominanceTargetsPercent`。
