# hotc233 与 HybridCLR Pro 差距说明

更新时间：2026-06-27  
策略总纲：[comparison-policy.md](comparison-policy.md)  
最新数据：[results/latest-hotc-vs-hybridclr.md](results/latest-hotc-vs-hybridclr.md)

---

## 一句话结论

hotc233 **尚未**达到 HybridCLR Pro 纯解释巅峰；同机社区版对比中数值/Vector/数组/AOT call 仍普遍落后。差距在解释器架构，不在 Tuanjie 编译。

| 问题 | 答案 |
|---|---|
| 能否宣称 Pro 全能力等价 | **不能** — 商业能力多数 `partial` |
| 性能达标线 | **Pro 上限 ~76.9% native IL2CPP** |
| 社区版是否目标 | **不是终态** — 但是 **L1 绝对门槛：14/14 条必须全面快于社区版**；任一条未赢即方向错误 |

---

## 架构差距映射

| Pro 公开能力 | hotc233 状态 | 巅峰路径 |
|---|---|---|
| 标准解释优化 | in-progress | Stage A/B/C typed IR |
| 离线指令优化 | partial | typed IR lowering |
| 完全泛型共享 | partial | Stage D + 探针矩阵 |
| 元数据优化 | partial | metadata 三表 10–25% |
| typeof 类 token 优化 | **已验证** | 推广 transform resolve 模式 |

---

## Execute 对齐（2026-06-27）

- Community 模式：`Execute()` 与 HybridCLR OSS v8.11.0 一致的无 fast-path 前导 dispatch。
- **保留** hotc233 优势：`RegVector3Add` / `RegVector3SqrMag`、`System.Math Min/Max` intrinsic、`typeof` 优化。
- 验收：跑 `benchmark` 后读 `results/latest-hotc-vs-hybridclr.md`，14 条 base 逐行对比社区版与 Pro 目标。

跟踪 opcode 差异：

```powershell
go run ./tools/hotc233ctl hybridclr-diff-opcodes -project .
```

参考文档：

- https://www.hybridclr.cn/docs/business/basicoptimization
- https://www.hybridclr.cn/docs/business/fullgenericsharing
- https://www.hybridclr.cn/docs/business/metadataoptimization
