# HybridCLR Pro 机制复刻路线（hotc233 落地对照）

更新时间：2026-06-27

本文档整理 **HybridCLR Pro 已公开/可观测机制** 与 **hotc233 从 0 复刻路径**。目标：每条机制 **100% 可验收、无隐藏魔法**；用于评估 AI/人力复刻耗时。

**hotc233 与 Pro 的路线差异（必读）**：

- **P1 主路径 = GodDomain 专用架构**（见 `god-domain-architecture.md`）：对 14 条 official base 做 **形状识别 → 最小 IR → whole-method bypass**，而不是先优化通用 14k 行 `switch` 分派。
- **全局 RegI32 lowering、threaded dispatch、Execute switch 微优化** 已归档（WA-009），**不得**作为追社区版/Pro 的 P1 手段。
- **DHE 差分混合执行** 不是 hotc233 纯解释器对标口径（AGENTS：禁止用首包 AOT 预置热更代码替代解释器成本）。

下列六章按 Pro 能力域划分；每节含：**Pro 原理 → hotc233 等价物 → 落地状态 → 验收方式 → 无魔法检查清单**。

---

## 一、解释器核心：栈式 → 寄存器式 / 单指令极限

| Pro 机制 | hotc233 等价策略 | 状态 | 验收 |
|---|---|---|---|
| 栈式 IL → 三地址寄存器 IR | **GodDomain trace opcode**（如 `RunStaticF4CallTrace`、`RunInstanceGetTransformSetV3CallTrace`）+ 窄域 `RunRegI32*Trace`（仅 binop 链，**非**全局 lowering） | 部分 landed | opcode profile：热方法内出现 trace / Instinct，而非长链 `Ldloc`+`BinOp` |
| 顶栈值缓存 / 物理寄存器绑定 | **Whole-method `TryExecuteHotc233FastPath`**（不进主循环）；冷路径暂不绑寄存器 | landed（fast path） | `hotc233FastPathKind != 0` 且 profile 无通用 dispatch 热点 |
| Threaded code / 计算 Goto | 冷路径保留 `switch`；**禁止 P1 投资** | 刻意不做 | WA-009：弱项仍慢时不得先改 dispatch |
| 超级指令（序列融合） | Transform 期 **peephole + trace fold**（≥3 连发同签名 call） | landed（CallAOTStatic 等） | 14 base 对应行 profile 有 fused opcode |
| 全局 RegI32 transform pass | **已废弃**（superseded by GodDomain） | retired | `PRO_EXPERIMENTAL_TRANSFORM=0` |

**无魔法清单**：

- [ ] 每条 base 弱项必须先有 **专用 builder 名**（`TryBuildGodDomain*` 或 trace fold 规则），再改 execute。
- [ ] 不得用「加快 switch 某一 case」替代专用 IR。
- [ ] 性能表必须在 **opcode profiler OFF** 下验收（见 `reporting-requirements.md`）。

**Pro 复刻优先级（数值类）**：BinOp/Vector 已赢社区 → 维持；Quaternion/Array 补 **typed struct trace**。

---

## 二、跨域调用：全路径 Bypass

| Pro 机制 | hotc233 实现 | 状态 | 验收 |
|---|---|---|---|
| 热更→AOT 直调 | `CallCommonNative*Cached` + `AllocAndBakeNativeThunkSlot` + `StaticF4CallTarget::Resolve` | landed | CallAOTStatic GodDomain + ParamVector3 已 >100% 社区 |
| 字段 ldfld/stfld 直偏移 | Instinct + 部分 fused ldfld；Unity 边界仍弱 | 部分 | SetTransform 仍 M2N 时 profile 见 `CallNativeInstance_*` |
| 泛型 AOT 实例直指针 | FGS + `InitAndGetInterpreterDirectlyCallMethodPointer` | P0 landed | 功能矩阵 + WebGL FGS 探针 |
| 0–8 参汇编桩 | `DirectInstanceV_V3_*` / static f4 direct | 部分（V3 setter） | getter/ref 仍待 Cached ABI |
| AOT→热更 轻量 Wrapper | 解释器入口 + `TryExecuteHotc233FastPath` 短路 | 部分 | 小方法 fastPathKind 覆盖 |
| callvirt 去虚拟化 | Transform 常量类型传播（待扩） | todo | Interface 探针 + base 无 callvirt 热点 |
| 委托 AOT 原生化 | `HybridCLR.RuntimeApi` facade → hotc233 实现 | 部分 | 委托探针 pass |

**当前最大缺口**：`set-transform-position`（get_transform 仍走 `CallNativeInstance_ret` / 无 trace fold）。

**落地顺序**：

1. `RunInstanceGetTransformSetV3CallTrace` peephole 必 fire + `get/set` 双 thunk cache（24B IR）。
2. ref 返回 getter → `CallCommonNativeInstance_*_0Cached`（Transform + Execute 成对）。
3. `Quaternion` 16B struct → `Q4` typed return trace（新 GodDomain builder）。

---

## 三、元数据与泛型

| Pro 机制 | hotc233 实现 | 状态 | 验收 |
|---|---|---|---|
| 统一 IL2CPP 元数据（Hook 注入） | 内置 `Data~/Libil2cpp` + `EnsureBuiltinRuntimeReady` | landed | 无外部 HybridCLR install |
| 全泛型共享 FGS | `Hotc233Settings.enableFullGenericSharing` | P0 landed | 平台矩阵 + 泛型探针 |
| 泛型 LRU | `MethodBodyCache` / inflate 缓存 | 部分 | 长跑内存稳定 |
| 元数据裁剪 | `AOTAssemblyMetadataStripper` | P0 landed | `metadata-optimization-report.json` ≥10% |
| 延迟加载 / 预原生化 | Transform 期 `resolveDatas` + token 缓存 | 部分 | typeof 已 297% 社区 |

**纯解释器对标**：元数据优化 **不替代** Execute 性能；P0 与 P1 分离门禁（`validate-reports` 默认只卡 P0）。

---

## 四、混合执行（DHE）

| Pro 机制 | hotc233 立场 | 状态 |
|---|---|---|
| 方法级 IL 哈希 → 跳 AOT | **非目标**（纯解释器对标） | n/a |
| 类/程序集级差分 | **非目标** | n/a |

若未来产品需要 DHE，单独立项；**不得**用于 `benchmark-docs` 14 条 base 验收。

---

## 五、编译期离线优化（Transform）

| Pro 机制 | hotc233 实现 | 状态 |
|---|---|---|
| 常量折叠 / 传播 | Instinct + fastPath `ConstI4` 等 | 部分 |
| 死代码 / 冗余消除 | peephole（`ApplyCommunityPeepholeFusion`） | landed |
| 装箱消除 | 随 Instinct 覆盖 | 部分 |
| GodDomain IL 扫描 | `TransformContext_GodDomain.cpp` 等 | landed（StaticF4） |

**原则**：离线做完的工作 = runtime 只跑 **一条 trace 或一次 bypass**；验收看 **指令条数与 opcode 种类**，不是 CPU 频率。

---

## 六、专项场景

| 场景 | Pro 做法 | hotc233 | 状态 |
|---|---|---|---|
| 异常处理查表 | EH clause → IR 映射表 | 沿用 HybridCLR 形态 | 已有 |
| 静态字段直址 | 首次缓存绝对地址 | 部分 Instinct | todo |
| async / ThreadStatic | 复用 IL2CPP | 功能探针 | 部分 |

---

## 14 base × 机制矩阵（当前方向）

| base slug | 主导机制章 | GodDomain 入口 | 社区 L1 | 追 Pro 重点 |
|---|---|---|---|---|
| set-transform-position | §二 | `RunInstanceGetTransformSetV3CallTrace` | **未** | typed get/set + trace |
| quaternion-op | §二 | 待增 `Q4` trace | **未** | struct return ABI |
| call-aot-instance-param-int | §二 | `RunInstanceVoidI4x5CallTrace` | **未** | 循环 trace + thunk |
| array-op | §一/§五 | array increment trace | **未** | typed array IR |
| call-aot-instance-return-vector3 | §二 | CallCommon v3 | 接近 | Pro 估算仍远 |
| call-aot-static-method | §二 | `TryBuildGodDomainStaticF4LoopMethod` | **是** | 正确性/计时复核 |
| typeof | §三/§五 | `TypeOfConstAccumI4` | **是** | Dominance 1000% |
| 其余数值/Vector/BinOp/GO | §一 Instinct | Instinct + CallCommon | **是** | Dominance 200%+ |

---

## AI 从 0 复刻耗时评估方法

1. **切机制**：按本章六域 + 上表一行 base 为最小 PR。
2. **先验收形状**：`HOTC233_LOCAL_OFFICIAL_COUNT=1` + 无 crash。
3. **再验收性能**：profile OFF 的 `local-benchmark` 全表 14 行。
4. **记录耗时**：机制 ID、PR 链接、前后 `hotc/社区`、是否触发 WA-009 止损。

**禁止项**：未绿 L1 不得启动 threaded dispatch / 全局 RegI32；不得用 profiler ON 的数据报「已赢 Pro」。

---

## 相关文档

- `benchmark-docs/god-domain-architecture.md` — P1 专用架构
- `benchmark-docs/reporting-requirements.md` — 全量表 + profiler 声明
- `benchmark-docs/comparison-policy.md` — L1/L2/Dominance 门禁
- `docs/pro-landing-matrix.json` — Pro 目标倍率与商业能力
- `docs/pro-wrong-answer-notebook.md` — 失败路线止损
