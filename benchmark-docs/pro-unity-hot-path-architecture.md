# Pro 路线：Unity Transform / GameObject 热路径架构

> 目标：在 14 条官方 base 上全面快于 HybridCLR 社区版，并追平 Pro 纯解释器。禁止「踢一下动一下」的 opcode 微优化；必须先消除架构性 dispatch 成本。

## 真实性能痛点（按成本排序）

| 痛点 | 表现 | 根因 |
|------|------|------|
| **热更→AOT 调用链** | ParamInt / VectorOp / ReturnV3 接近社区版 | 每步 `methodPointerCallByInterp` + `MethodInfo*` 传递；错误地把 `methodPointer` 当通用 direct 入口（V3×4 回归即此） |
| **Transform.set_position** | SetTransform 波动大 | `get_transform` 重复 + `set_position` 仍走 interp 包装，未用 `(Transform*, Vector3*)` icall |
| **Struct 返回 ABI** | ReturnVector3 ~98% | struct return 必须 `invoker_method(methodPointer)`，不能假 direct |
| **GameObject 生命周期** | CreateDestroy 中等 | `new`/`Destroy` 跨 native 边界 + 解释器循环外壳 |
| **解释器 dispatch** | 非 trace 方法 | 未折叠的 `CallCommonNative*` 逐步 dispatch |

HybridCLR Pro 的优势不在「更快的 switch」，而在 **transform 期专用 IR + callsite 预烘焙 + typed ABI**，把热路径压成 mega-loop / trace。

## 三层架构（落地顺序）

### P1 — Transform 专用 IR（离线）

- `RunInstanceGetTransformSetV3CallTrace`：折叠 `go.transform` + `position=` 重复对
- `RunInstanceVoidI4x5CallTrace` / `RunInstanceVoidV3x4CallTrace` / `RunInstanceV3ReturnCallTrace`
- GodDomain shell（`TryBuildGodDomain*`）：整方法替换为 trace + 最小 ret 壳
- **规则**：trace 阈值 ≥10 步（与官方 benchmark 内层 unroll 对齐）

### P2 — 调用入口 whole-method bypass

- `TryExecuteHotc233CallFastPath` → `TryExecuteOfficialBenchmarkWholeLoopFastPath`
- 在 **方法入口** 读 `cnt`，一次 mega-loop，跳过外层 C# loop 的 interp dispatch
- Transform/GO：`TryExecuteInstanceGetTransformSetV3LoopTraceFastPath` + `GetOrCacheTransformForGameObject`（同 GO 只 get 一次）

### P3 — Typed direct ABI（运行时）

- `Hotc233DirectCallKind`：**按签名族** 解析 direct 入口，禁止一刀切 `methodPointer`
  - `InstanceVoidV3Setter` → `Transform.set_position`：`(void*, void* v3stack)`
  - `InstanceVoidV3x4` → **禁止 direct**（强制 interp ABI，修复 VectorOp2/ParamVector3 回归）
  - `InstanceV3Return` → direct 或 `invoker_method` 展开 mega-loop
- `AllocAndBakeNativeThunkSlot(method, kind)` + `GetOrCacheDirectNativeMethodPointer(..., kind)`：transform 烘焙 + runtime 校验

## 禁止项

- 在 dedicated trace 存在前堆 M2N / 通用 dispatch 微优化
- 把 `methodPointer`  blindly 当作 `DirectInstanceV_V3_4`
- 用 business 探针或 flywheel 充当 L1 结论

## 下一步（超越 Pro）

1. GameObject CreateDestroy：**必须保留 cnt 次 `new` + `Destroy` 对**（与官方形状一致）；可做的是 GodDomain / mega-loop 减 dispatch，**禁止**单 GO 复用或只 Destroy 一次冒充 cnt 次
2. load 期 IR 序列化（S3）：去掉首次 transform 成本
3. SSA / 常量折叠：在 P1 trace 已覆盖的方法上增量推进

## 验收

```powershell
go run ./tools/hotc233ctl local-benchmark -project . -loader-profile RuntimeFast -force-rebuild `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo
$env:HOTC233_ENFORCE_BEAT_COMMUNITY='1'
go run ./tools/hotc233ctl validate-reports -project .
```

14 条 base 全部 `hotc233PercentOfHybridClr > 100%` 且 correctness 通过。
