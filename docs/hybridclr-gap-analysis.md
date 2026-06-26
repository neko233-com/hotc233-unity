# hotc233 与 HybridCLR Pro 差距说明

更新时间：2026-06-26  
总纲：[`performance-peak-plan.md`](performance-peak-plan.md)

---

## 一句话结论

hotc233 **尚未**达到 HybridCLR Pro 纯解释巅峰；`typeof` 已超目标，数值/Vector/数组/AOT call 仍差 **4.35x–13.86x**。差距在解释器架构，不在 Tuanjie 编译。

| 问题 | 答案 |
|---|---|
| 能否宣称 Pro 全能力等价 | **不能** — 商业能力多数 `partial` |
| 性能达标线 | **Pro 上限 ~76.9% native IL2CPP**，不是 floor ~7.8% |
| 社区版是否目标 | **不是** — 8.11.0 同机表仅校准 benchmark 环境 |

---

## 同机 WebGL 参考（2026-06-26T12:40:59Z）

独立项目：`D:/Code/Tuanjie-Projects/hybridclr-benchmark-demo`（HybridCLR 8.11.0）

| hotc233 operation | hotc / 社区版 | hotc / Pro 目标 | 还需 | Stage |
|---|---:|---:|---:|---|
| hybridclr-binop-complex | 53.0% | 7.2% | 13.86x | A |
| hybridclr-binop-add | 72.9% | 9.9% | 10.08x | A |
| hybridclr-call-aot-instance-param-vector3 | 71.3% | 9.7% | 10.30x | B |
| hybridclr-array-op | 85.2% | 11.6% | 8.63x | C |
| hybridclr-quaternion-op | 88.3% | 23.0% | 4.35x | A |
| hybridclr-call-aot-static-method | 82.5% | 55.0% | 1.82x | B |
| hybridclr-typeof | 358.1% | 243.5% | **已超** | 监控 |

报告：`Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.json`

---

## 架构差距映射

| Pro 公开能力 | hotc233 状态 | 巅峰路径 |
|---|---|---|
| 标准解释优化 | in-progress | Stage A/B/C typed IR |
| 离线指令优化 | partial | 仅 typed IR lowering，见 WA-007 |
| 完全泛型共享 | partial | Stage D + 探针矩阵 |
| 元数据优化 | partial | metadata 三表 10–25% |
| typeof 类 token 优化 | **已验证** | 推广 transform resolve 模式 |

---

## GC / 内存

WebGL RuntimeFast：`game-low-gc-frame` 与 `game-memory-stability` 当前 `gc0=0, retainedDelta=0`。  
性能未达标 ≠ 内存泄漏；巅峰路线必须 **低 GC + 低 metadata 峰值** 同时满足 Pro 公开预算（~700KB 指令、~1.2MB/线程）。

---

## 对外口径

在 [`performance-peak-plan.md`](performance-peak-plan.md) 完成定义满足前：

- 不说「已超越 HybridCLR Pro」
- 不说「社区版已对齐即成功」
- 性能表必须含 hotc233 实测、社区版参考、Pro 目标、native IL2CPP

参考文档：

- https://www.hybridclr.cn/docs/business/basicoptimization
- https://www.hybridclr.cn/docs/business/fullgenericsharing
- https://www.hybridclr.cn/docs/business/metadataoptimization
