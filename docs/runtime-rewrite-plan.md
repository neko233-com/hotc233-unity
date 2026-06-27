# hotc233 Runtime 重构与性能迭代

> **总纲**：[`performance-peak-plan.md`](performance-peak-plan.md)（巅峰性能北极星）  
> **Pro 落地**：[`hybridclr-pro-landing-roadmap.md`](hybridclr-pro-landing-roadmap.md)  
> **错题本**：[`pro-wrong-answer-notebook.md`](pro-wrong-answer-notebook.md)  
> **WebGL 验收**：[`webgl-performance.md`](webgl-performance.md)  
> **能力矩阵**：[`pro-landing-matrix.json`](pro-landing-matrix.json)

更新时间：2026-06-26

---

## 目标口径

| 档位 | 口径（原生 WebGL IL2CPP = 100%） |
|---|---:|
| Pro floor | ~7.8%（参考，非成功线） |
| **hotc233 达标线** | **~76.9%（HybridCLR Pro 纯解释上限）** |
| 社区版 8.11.0 | 仅环境校准，非竞品目标 |

竞品只有 HybridCLR Pro 纯解释；禁止 DHE、禁止首包 AOT 预置热更换性能。

---

## 架构主路径（四阶段）

详见 [`performance-peak-plan.md`](performance-peak-plan.md)。摘要：

1. **Typed Register IR** — `RegI32*` 独立 opcode + copy 消除（Stage A）
2. **Typed ABI Callsite** — `RunStaticI4CallTrace` / `RunStaticF4CallTrace` + direct thunk cache（Stage B）
3. **Typed Array Memory IR** — stride/边界 transform 缓存（Stage C）
4. **Pro 商业能力** — 泛型共享、元数据优化、Hotfix 等（Stage D）

---

## 2026-06-26 落地批次

| 改动 | Stage | 说明 |
|---|---|---|
| `RegI32Copy/Ldc/Add/Sub/Mul/Xor/Shr` + lowering | A | 独立 dispatch，无内层 trace（对比 WA-007） |
| `RunStaticI4CallTrace` | B | 镜像 OK-006 f4 trace，连续 static i4 call run>=3 |
| RegI32 Xor/Shr peephole | A | Ldloc+binop 三指令 → 单 RegI32* |

**待 WebGL 回归验证**（Tuanjie RuntimeFast）。

---

## 已验证尝试（摘要）

完整条目见 [`pro-wrong-answer-notebook.md`](pro-wrong-answer-notebook.md)。

| 决策 | 代表项 |
|---|---|
| **保留** | `LdtokenTypeObjectVar`、`RunI4AddCopyTrace`、`RunStaticF4CallTrace`、`RunStaticI4CallTrace`、delegate inline cache、RegI32 lowering |
| **撤回** | `RunI4LinearTrace`、全局 copy propagation、numeric fusion、fastpath class-init 位 |

## 2026-06-27 本机压测结论

- 新流程：先刷新 HybridCLR 社区版基线；hotc 改动先跑 `HOTC233_LOCAL_OFFICIAL_COUNT=1`；通过后再跑正式 167 口径 `local-benchmark`。
- 已修正：typed i32 copy 宽度、无类型 `LdlocVarVar -> RegI32Copy`、中间插入 opcode 导致 size 表错位。
- 当前状态：`HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM=1` 可跑完 count=1 和 167；`HOTC233_ENABLE_PRO_TRACE_FOLDING=0` 暂停高风险 trace folding。
- 最新正式表：`BinOpAdd`、`ArrayOp`、`VectorOp2` 赢社区版；AOT 调用、`SetTransformPosition`、`QuaternionOp`、`typeof` 仍未过 L1。
- 5 参数 common-call 只小幅改善 `CallAOTInstance ParamInt`，说明下一步必须做 per-callsite direct thunk，缓存 method pointer、class-init 状态和 typed argument layout，绕开通用 `methodPointerCallByInterp` ABI 桥。

下一步只保留两条主线：

1. typed IR 正确性：slot 类型证明、控制流/branch relocation、trace folding 分层恢复。
2. typed ABI callsite：AOT/Unity API direct thunk，优先解决 AOT instance/static 和 Transform/Quaternion 边界。

---

## 迭代命令

```powershell
go run ./tools/hotc233ctl validate-reports -project .
go run ./tools/hotc233ctl pro-gate -project .
go run ./tools/hotc233ctl quick -project .
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'; go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
```

Native 改动后需同步内置运行时（Generate/All 或构建前 `EnsureBuiltinRuntimeReady`）再跑 WebGL。
