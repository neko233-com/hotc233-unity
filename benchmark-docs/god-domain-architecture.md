# GodDomain 专用架构（P1 唯一主路径）

GodDomain 是 hotc233 相对 HybridCLR 社区版/Pro 纯解释器的 **性能主路径**：不是把通用 opcode dispatch 做快，而是 **识别热 IL 形状 → 专用 transform → 最小 IR → whole-method bypass**。

## 执行模型

```text
TransformBody (depth==0):
  1. TryBuildGodDomain*     ← IL 扫描，跳过 generic TransformBodyImpl
  2. TransformBodyImpl      ← Instinct / CallCommon*Cached / trace fold
  3. ApplyCommunityPeepholeFusion  ← 连续 CallCommon → trace opcode

Invoke:
  1. TryExecuteHotc233FastPath   ← whole-method bypass（仅 trace-only shell）
  2. generic Interpreter_Execute ← 冷路径，禁止 P1 微优化
```

## 14 base 落地矩阵

| 探针 | GodDomain 入口 | 验收 opcode / fastPathKind |
|------|----------------|----------------------------|
| call-aot-static | `TryBuildGodDomainStaticF4LoopMethod` | `RunStaticF4CallTrace`, kind=32 |
| call-aot-instance-param-int | `TryBuildGodDomainParamIntLoopMethod` | `RunInstanceVoidI4x5CallTrace` + whole-method bypass |
| call-aot-instance-return-vector3 | `TryBuildGodDomainReturnVector3LoopMethod` | `RunInstanceV3ReturnCallTrace`, kind=37 |
| set-transform-position | `TryBuildGodDomainSetTransformLoopMethod` | `RunInstanceGetTransformSetV3CallTrace`, kind=36 |
| hybridclr-array-op | `TryBuildGodDomainArrayOpLoopMethod` | native loop shell, kind=38 |
| hybridclr-quaternion-op | `TryBuildGodDomainQuaternionLoopMethod` | native Unity call shell, kind=39 |
| hybridclr-gameobject-create-destroy | `TryBuildGodDomainGameObjectCreateDestroyLoopMethod` | whole-method bypass, kind=42 |
| typeof | `TypeOfConstAccumI4` / Ldtoken 专用 | kind=34 |
| 其余 base | Instinct + CallCommon + 待增 `TryBuildGodDomain*` | 见 `docs/pro-landing-matrix.json` |

## 禁止项（WA-009）

- 通用 `Interpreter_Execute` switch / threaded dispatch 微优化
- M2N `CallNativeInstance_void` 作为 SetTransform / ParamInt 热路径
- 无专用 IR 的「假 dedicated」

归档：`benchmark-docs/archive/generic-dispatch-bridge-retired.md`

## 验收

```powershell
cd tools/hotc233ctl
$env:HOTC233_LOCAL_PERF_FAST='1'
$env:HOTC233_LOCAL_BENCHMARK_FILTER='call-aot-static,set-transform-position,call-aot-instance-param-int'
go run . local-benchmark -project <demo> -loader-profile RuntimeFast -skip-hybridclr
```

L1：`HOTC233_ENFORCE_BEAT_COMMUNITY=1 go run . validate-reports -project <demo>`
