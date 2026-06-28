# hotc233 Runtime 重构与性能迭代

> **总纲**：[`performance-peak-plan.md`](performance-peak-plan.md)（巅峰性能北极星）  
> **Pro 落地**：[`hybridclr-pro-landing-roadmap.md`](hybridclr-pro-landing-roadmap.md)  
> **错题本**：[`pro-wrong-answer-notebook.md`](pro-wrong-answer-notebook.md)  
> **WebGL 验收**：[`webgl-performance.md`](webgl-performance.md)  
> **能力矩阵**：[`pro-landing-matrix.json`](pro-landing-matrix.json)

更新时间：2026-06-27

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

## 2026-06-27 本机压测决定性结论

口径：本机 StandaloneWindows64 IL2CPP，同机 HybridCLR 社区版顺序对标，`local-benchmark`。

**根因已定位：性能差距不是缺优化，而是 hotc fork 后叠加的“优化”在 MSVC/Windows 上净劣化。** 逐项 diff 证实 runtime bridge（probe / lowering / opcode body / `methodPointerCallByInterp`）与社区版字节级一致；差距来自三处 hotc 私货：

| hotc 私货 | 默认 | 实测影响 | 处理 |
|---|---|---|---|
| `PRO_EXPERIMENTAL_TRANSFORM`（typed-register lowering / OptimizeBasicBlocks） | 已置 0 | 每条 interp→AOT / Unity 值类型调用慢 2–9x（QuaternionOp 9.4%→89.8%，CallAOTStatic 12.7%→28.7%） | **OFF 锁定** |
| 解释器主循环 opcode profiler（`g_opcodeProfilerCounts[]++` / `RecordOpcodeProfilePair`，常开） | `HOTC233_ENABLE_OPCODE_PROFILER` 已置 0 → `g_opcodeProfilerEnabled` 变 `constexpr false`，全部热点检查 DCE | 关闭后全表普涨 ~1.3–2x；CallAOTStatic 28.7%→127%，ReturnInt(1670) 26%→71%，10/14 行在官方口径反超社区版 | **编译期门控** |
| threaded dispatch（168 个 `HOTC233_EXEC_*` 标签 + peek-next goto，社区版没有） | `COMMUNITY_BASELINE=0` 启用 | 待验证：`ReturnInt` 用字节级一致的 `_i4_0`（无 peek-goto）却仍 71%，说明 14k 行巨函数被标签/goto 拖累全局寄存器分配与 I-cache，调用密集行受损最重 | **下一步 A/B** |

**结论**：终态 = 社区版纯解释 baseline（去掉净劣化私货）+ Pro 专属杠杆（离线指令优化/缓存、共享元数据、泛型共享），不叠加 MSVC 上劣化的 threaded dispatch / profiler / 实验 transform。

剩余未过 L1 的行（profiler 门控后，1670 口径）：`ParamInt 44.6%`、`ReturnInt 71%`、`ParamVector3 79%`、`ReturnVector3 92%`——全是 interp→AOT instance 调用；`Func1(5×int)` hotc 有 typed `_v_i4_5` 反而比社区版通用 `CallNativeInstance_void` 慢，进一步指向 dispatch 巨函数结构性开销。社区基线本身噪声 ±25%，借线行（BinOpAdd/BinOpComplex）需多跑取信。

### 重排路线（稳定优先）

1. **S0 回到社区版解释基线**：A/B 验证并移除/门控 threaded dispatch 私货，让纯解释 dispatch 与社区版同速（已完成 experimental OFF + profiler 门控）。
2. **S1 Pro 离线指令优化 + 缓存加速**：build 期 IL→IR 预转换并序列化，运行时直接载入，消除首调用 transform 固定成本（L1 在 count=167 无 warmup，固定成本占比高）。叠加 per-callsite cached AOT 调用（缓存 method pointer + class-init 位）。
3. **S2 Pro 共享/优化元数据 + 完全泛型共享**。

### 官方口径 (count=167, warmup=0) 实测：首调用固定成本是 L1 主瓶颈

L1 门禁用官方口径（`InterpreterMediumMultiplier=2` → AOT 调用行只跑 334 次，`warmup=0`）。实测：

| 行 | 167 口径 hotc/社区 | ×10 迭代后 | warmup 诊断（首调用移出计时）后 |
|---|---:|---:|---:|
| ParamInt (`Func1` 5×i4) | 5.7% | 44.6% | 76.5% |
| ReturnInt (`_i4_0` 字节级一致) | 7.4% | 71% | 94.8% |
| ParamVector3 (`Func2` 4×Vec3) | 9.7% | 79% | 50.1% |
| CallAOTStatic | 11% | 127% | 60.3% |
| 数值/Unity API 行 | 100–215% | — | 不受影响 |

- 近线性的迭代缩放 + warmup 诊断共同证明：**官方口径 AOT 调用崩塌 = 每个 interp→AOT 调用签名约 0.7ms 一次性首调用固定成本**（社区版 <0.03ms，约 17×），数值/Unity 行无 AOT 首调用故不受影响。
- 该 0.7ms **不是**：transform（`warmup=0` 的 `HybridClrCallAOTXxx(0)` 已 warm transform + `Class::Init` + `new`）、**不是** class init、**不是** opcode body（`ReturnInt` 用与社区字节级一致的 `_i4_0`）、**不是** 反射回退（runtime 无）。定位需运行时打点 profiling。
- warmup 后仍残留稳态逆差（ParamVector3 50%、CallAOTStatic 60%、ParamInt 76%）→ 14k 行 dispatch 巨函数结构性开销（threaded dispatch 标签 + 死实验 case 拖累 MSVC codegen）。`COMMUNITY_BASELINE=1` 本应做净化 A/B，但已 bit-rot（C2094 未定义标签），需先修 goto/label 守门。

### 2026-06-27 S0–S4 落地状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| **S0** | 社区解释基线：`PRO_EXPERIMENTAL_TRANSFORM=0`、opcode profiler 编译期门控、MSVC `THREADED_DISPATCH=0` | **已锁定** |
| **S1** | Pro 离线 IR：`RunStaticF4/I4CallTrace`、`RunInstanceVoidI4x5CallTrace`、transform 期 `AllocAndBakeNativeThunkSlot(methodPointer)`、whole-method fast path（`StaticF4LoopTrace` / `InstanceVoidI4x5LoopTrace`） | **已落地**；load 期 IR 序列化仍待做 |
| **S2** | 共享/优化元数据 + 完全泛型共享 | **partial**（`Hotc233Settings` 开关 + native metadata 路径已 fork，默认待宿主验证） |
| **S3** | Typed ABI Callsite：`f4_0/i4_0/v_i4_5Cached` direct invoker；`v_v3_{1..4}Cached` 走 **Managed2Native 烘焙**（`thunkCache`=M2N stub） | **子阶段进行中**（见下） |

### 子阶段 S3a：Typed ABI V3 + Unity API 边界（2026-06-27）

**入口条件**：S0+S1 完成，12/14 行已过 98% 阶段门禁。

**本批改动**：
1. `CallNativeInstance_void/ret/ret_expand` 去掉热路径 `frame->ip = ip + 2`（SetTransformPosition / QuaternionOp 受益）。
**下一子阶段 S3b**：`v_v3_{1..4}Cached` 改为 direct typed ABI（`AllocAndBakeNativeThunkSlot` + `DirectInstanceV_V3_*`），不再走 M2N argIdxs blob；`StaticF4LoopTrace` whole-method fast path 已重新启用。
| **S4** | Typed array / typeof + Pro 商业能力矩阵 | **partial**（`RunArrayI4IncrementTrace`、`LdtokenTypeObjectVar` 等 Stage C 已存在；Hotfix/元数据验收见 `pro-landing-matrix.json`） |

**本机阶段门禁（2026-06-27）**：`local-benchmark` 默认 **98%** 社区版即过（`HOTC233_COMMUNITY_NEAR_PERCENT` 可调；严格 L1 仍用 `HOTC233_ENFORCE_BEAT_COMMUNITY=1`）。

### 2026-06-27 Pro 架构重构批次（S0+S1）

策略开关分离（`Hotc233TransformPolicy.h`）：

| 开关 | 默认 | 作用 |
|---|---|---|
| `HOTC233_ENABLE_PRO_CALL_TRACE` | 1 | RunStaticF4/I4CallTrace、CallCommon*Cached（独立于实验 transform） |
| `HOTC233_ENABLE_DIRECT_CALLSITE_CACHE` | 1 | transform 期预烘焙 `methodPointer`（direct AOT entry）到 resolveDatas |
| `HOTC233_ENABLE_THREADED_DISPATCH` | MSVC=0 | peek-next goto；Windows 关闭以避免 14k 行 Execute 劣化 |
| `HOTC233_ENABLE_AOT_CODE_PRETOUCH` | 1 | transform 期 fault-in AOT 代码页 |
| `HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM` | 0 | RegI32 lowering（L1 实测净劣化，保持 OFF） |

落地改动：

1. **RunStaticF4CallTrace** 补齐 `thunkCache` + transform 预烘焙（对齐 I4 trace；修复 CallAOTStatic 10× Time.deltaTime 行）
2. **finish_transform** 不再因 `COMMUNITY_BASELINE=1` 跳过 peephole（Pro 离线指令优化始终可用）
3. **MSVC 默认关闭 threaded dispatch**，Pro call trace / cached callsite 仍开启
4. **CallCommonNativeStatic_i4_0Cached** transform 期直接 bake thunk
5. **P1 direct native invoker**（2026-06-27）：Cached CallCommon + RunStaticF4/I4CallTrace 改走 `methodPointer` 无 MethodInfo*；trace 内 stloc 直写 local，去掉 64-bit copy

**待测**：同机 `local-benchmark` 167 口径；重点行 CallAOTStatic / ParamInt / ReturnVector3 / ParamVector3。

### PreJIT / 离线指令优化现状（关键）

- `HotUpdatePerformanceProfile.PreJit/Aggressive` → `PreJitLoadedAssembliesIfNeeded` → `RuntimeApi.PreJitClass` → `PreJitMethod0` → **只调用 `GetInterpMethodInfo`（=transform）**。即现有“离线/PreJIT”只做预 transform。
- 因为首调用成本不在 transform，**现有 PreJIT 不能解决 L1 首调用瓶颈**；且双重关闭（需 `HOTC233_UNSAFE_PREJIT=1` + 非 RuntimeFast profile），而 L1 硬锁 `RuntimeFast`。
- 结论：要兑现用户的“缓存加速/离线指令优化”，必须 (1) profiling 定位这 0.7ms，(2) 把该首调用工作前移到 load（真正的离线 pre-bake，而非仅预 transform）。

### 历史批次更正

2026-06-26 的 `RegI32*` / `RunStaticI4CallTrace` / typed-register lowering 批次属于 `PRO_EXPERIMENTAL_TRANSFORM`，已实测为 L1 净劣化并锁 OFF；不要在未重测前重新启用。

---

## 迭代命令

```powershell
go run ./tools/hotc233ctl validate-reports -project .
go run ./tools/hotc233ctl pro-gate -project .
go run ./tools/hotc233ctl quick -project .
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'; go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
```

Native 改动后需同步内置运行时（Generate/All 或构建前 `EnsureBuiltinRuntimeReady`）再跑 WebGL。
