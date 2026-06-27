# hotc233 架构自审报告

- 生成时间 (UTC): 2026-06-27T16:20:06.1193378Z
- 口径: StandaloneWindows64 IL2CPP Player + HybridCLR 社区版同机 14 条 base
- 商业 P0: false
- L1 全面超越社区版 (>100%): false
- Dominance 档位 (默认 200%, typeof 目标 1000%): false
- 最弱行: `hybridclr-set-transform-position` 42.6% of HybridCLR community

| 项目 | hotc233 ops/s | 社区版 ops/s | hotc/社区 | Dominance 目标 | 桶 | 状态 |
|---|---:|---:|---:|---:|---|---|
| SetTransformPosition | 810286 | 1902050 | 42.6% | 400% | god-domain-transform-bypass | dominance-gap |
| CallAOTInstance ParamInt | 4276569 | 5719178 | 74.8% | 300% | god-domain-transform-bypass | dominance-gap |
| CallAOTInstance ParamVector3 | 3370333 | 3947991 | 85.4% | 200% | typed-abi-callsite | dominance-gap |
| CallAOTInstance ReturnVector3 | 8146341 | 8226601 | 99.0% | 200% | typed-abi-callsite | dominance-gap |

## 下一步（按性价比）

- [P1/god-domain-transform-bypass] SetTransformPosition: 42.6% vs dominance 400% (~9.4x needed) → whole-method fast path + offline trace in god-domain-transform-bypass bucket
- [P1/god-domain-transform-bypass] CallAOTInstance ParamInt: 74.8% vs dominance 300% (~4.0x needed) → whole-method fast path + offline trace in god-domain-transform-bypass bucket
- [P1/typed-abi-callsite] CallAOTInstance ParamVector3: 85.4% vs dominance 200% (~2.3x needed) → whole-method fast path + offline trace in typed-abi-callsite bucket
- [P1/typed-abi-callsite] CallAOTInstance ReturnVector3: 99.0% vs dominance 200% (~2.0x needed) → whole-method fast path + offline trace in typed-abi-callsite bucket
- Run: go run ./tools/hotc233ctl arch-self-review -project . -loader-profile RuntimeFast
