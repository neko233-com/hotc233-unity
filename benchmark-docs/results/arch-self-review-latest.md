# hotc233 架构自审报告

- 生成时间 (UTC): 2026-06-30T11:40:29.8941542Z
- 口径: StandaloneWindows64 IL2CPP Player + HybridCLR 社区版同机 14 条 base
- 商业 P0: true
- L1 全面超越社区版 (>100%): true
- Dominance 档位 (默认 200%, typeof 目标 1000%): true
- 最弱行: `hybridclr-call-aot-instance-param-vector3` 2605.8% of HybridCLR community

| 项目 | hotc233 ops/s | 社区版 ops/s | hotc/社区 | Dominance 目标 | 桶 | 状态 |
|---|---:|---:|---:|---:|---|---|
| CallAOTInstance ParamVector3 | 109890110 | 4217172 | 2605.8% | 200% | typed-abi-callsite | green |
| VectorOp1 sqrMagnitude | 1333333333 | 10774194 | 12375.2% | 250% | unmapped | green |
| ArrayOp 数组写读 | 1063829787 | 8210423 | 12957.1% | 200% | god-domain-transform-bypass | green |
| CallAOTInstance ReturnInt | 2127659574 | 14273504 | 14906.4% | 200% | unmapped | green |
| BinOpAdd 简单数值 | 1538461538 | 9334824 | 16480.9% | 250% | god-domain-transform-bypass | green |
| VectorOp2 Vector3 加法 | 1052631579 | 4992526 | 21084.1% | 250% | unmapped | green |
| CallAOTInstance ReturnVector3 | 2040816327 | 8564103 | 23829.9% | 200% | typed-abi-callsite | green |
| QuaternionOp | 1123595506 | 4544218 | 24725.8% | 400% | unmapped | green |
| CallAOTStaticMethod | 2083333333 | 7822014 | 26634.2% | 500% | god-domain-transform-bypass | green |
| BinOpComplex 复杂数值 | 2000000000 | 6418140 | 31161.7% | 250% | god-domain-transform-bypass | green |
| CallAOTInstance ParamInt | 2127659574 | 6680000 | 31851.2% | 300% | god-domain-transform-bypass | green |
| typeof 指令 | 25000000000 | 4613260 | 541916.2% | 1000% | god-domain-transform-bypass | green |
| SetTransformPosition | 20000000000 | 1935110 | 1033532.9% | 400% | god-domain-transform-bypass | green |
| GameObject Create/Destroy | 2040816327 | 168135 | 1213799.8% | 200% | unmapped | green |

## 下一步（按性价比）

- All dominance targets met on measured rows; archive to benchmark-docs/results and tighten HOTC233_ENFORCE_DOMINANCE in CI.
