# hotc233 Pro 错题本

更新时间：2026-07-01
用途：记录**已实测失败或已撤回**的优化路线，强制调整架构设计，直到达到 HybridCLR Pro 纯解释性能与公开商业能力。  
配套：`pro-landing-matrix.json`（能力落地矩阵）、`hybridclr-pro-landing-roadmap.md`（实施路线）。

## 使用规则

| 规则 | 说明 |
|---|---|
| 每条错题必须写清 | 尝试、实测信号、根因、架构教训、替代路线、状态 |
| 连续 2 次同方向无接近 Pro 目标 | 记入本表并 **停止** 该方向 |
| WebGL 浏览器 marker 超时 | 立即记 **blocked**，禁止继续等满 timeout |
| 禁止路线 | 不得在本表标记为 `withdrawn` / `blocked` 后无文档重新启用 |

门禁：`go run ./tools/hotc233ctl pro-gate -project .` 会读取本文件与 `pro-landing-matrix.json`，结合最新 WebGL base JSON 输出下一架构优先级。

---

## 错题索引

| ID | 日期 | 尝试 | 结果 | 架构调整 | 状态 |
|---|---|---|---|---|---|
| WA-001 | 2026-06 | call boundary direct leaf opcode | WebGL 总体/业务回退 | 改走 typed ABI callsite 分层，不再加 call 形状 opcode | withdrawn |
| WA-002 | 2026-06 | 全局 OIO copy propagation | 执行次数上升、总体回退 | 禁止全局 copy propagation；只在 typed register IR 做 slot 消除 | withdrawn |
| WA-003 | 2026-06 | InitInlineLocals tail dispatch | delegate/lambda 明显回退 | 撤回；dispatch 改造必须等 typed IR 后端 | withdrawn |
| WA-004 | 2026-06 | `Add_i4+copy` / `copy+copy` 大指令融合 | 多数业务行回退 | fusion 只能来自 typed IR lowering，不能扫 IL 片段硬拼 | withdrawn |
| WA-005 | 2026-06 | `mul_i4→sub_i4` numeric fused opcode | 单项 +8% 但 BinOpAdd/AOT/Unity API 大幅回退 | **停止** benchmark 形状 numeric fusion → typed register IR | withdrawn |
| WA-006 | 2026-06 | fastpath 方法 class-init 状态位 | 小方法调用 -17%、任务委托 -16% | 禁止扩大 per-method 状态缓存；改 transform 期 resolve | withdrawn |
| WA-007 | 2026-06-26 | `RunI4LinearTrace` v4 通用 i4 linear trace | WebGL CDP 691s 超时，主线程卡死 | **禁止** 通用 linear trace / interpreter 内 switch-loop trace | blocked |
| WA-008 | 2026-06 | static 专用 opcode | 局部提升、总体不稳定 | 并入 typed ABI callsite，不单开 static opcode 线 | withdrawn |
| WA-009 | 2026-06-27 | 通用 dispatch + M2N 桥接 + Execute fallback 微优化 | CallAOTStatic 8%→37% 仍无 Pro bypass；profile 无 RunStaticF4CallTrace | **断舍离**：GodDomain；禁止投资 Interpreter_Execute switch；见 `god-domain-architecture.md` | blocked |
| WA-010 | 2026-06-30 | 放开 scalar value-type generated M2N，绕过 shared-struct reflection guard | `Dictionary<TKey,TValue>.TryGetValue` fully-shared generic native crash `-1073741819` | 禁止粗暴放开 fully-shared generic M2N；必须显式解决 rgctx/MethodInfo ABI | blocked |
| WA-011 | 2026-07-01 | AOT 容器 `max_length` 直读 + M2N probe 清理 | Dictionary 小涨但 List/Coroutine 回退 | 撤回；容器优化必须按 callsite/method kind 证明净收益 | withdrawn |
| WA-012 | 2026-07-01 | List/Stack 专用 M2N wrapper 分派 | List/callback/event 明显回退；一次 C++ 构建错误已修后仍回退 | 撤回；不得用 wrapper 扩散替代真实 List/Stack IR 或状态机优化 | withdrawn |
| WA-013 | 2026-07-01 | prepared interpreted call generic fast path | Async/Task 小涨但 List/Coroutine/虚派发/delegate/Dictionary 回退 | 撤回；prepared call 入口不再堆通用 fastpath 判断，改做 callsite 级 typed ABI | withdrawn |
| WA-014 | 2026-07-01 | 普通 `CallVirtual_*` receiver class callsite cache | callback/custom 小涨但 List/event/task/Dictionary 回退，仍远低 CE | 撤回；单纯虚表解析缓存不是主瓶颈，需状态机/容器/委托各自共享 ABI 层 | withdrawn |
| WA-015 | 2026-07-01 | delegate 同步调用 whole-method fastpath | 所有关键 business 基本回退，custom/event/callback 尤其明显 | 撤回；delegate 优化必须在 transform/typed ABI 预烘焙调用形状，不在 handler 里临时判 fastpath | withdrawn |
| WA-016 | 2026-07-01 | small i4 leaf whole-method interpreter | callback/custom/async 小涨但 List/Task/Dictionary 回退，仍远低 CE | 撤回；小叶子解释器仍是局部补丁，不是全面超过 CE 的通用架构 | withdrawn |
| WA-017 | 2026-07-01 | AOT container method plan cache | List 仅 29.7%，整体 business 仍明显弱于 CE | 撤回；容器必须脱离 M2N wrapper，进入 transform-time typed container IR | withdrawn |
| WA-018 | 2026-07-01 | 移除 InterpreterInvoke / delegate hidden-ret 热路径诊断 | Async 小涨但 List/Custom/Dictionary/Struct 回退 | 撤回；运行时探针清理不是主瓶颈，继续做 transform-time typed ABI 与状态机/container IR | withdrawn |
| WA-047 | 2026-07-01 | 48-byte static leaf fastpath in generic call path | `AsyncComputeSync`/`Actor.Tick` 命中但 business 退到 1/8 | 停止 48-byte 小叶子路线；对照 CE 补通用 call/state/container/delegate 机制 | withdrawn |

## 保留项（不是错题，但不得扩展成通用路线）

| ID | 尝试 | 条件 | 下一步 |
|---|---|---|---|
| OK-001 | copy+ldind 值转发 | WebGL +3.1% | 回归监控 |
| OK-002 | `LdtokenTypeObjectVar` typeof 缓存 | 超 Pro 目标 | 推广 token cache 模式到其他 metadata |
| OK-003 | 闭包 fastpath + delegate inline cache | delegate-lambda 达 Pro floor | 补 static/instance/multicast 分层 |
| OK-004 | `CallInterpStatic_ret` IMI cache | 小方法调用 +9% | 仍需 typed ABI 才能过 Pro floor |
| OK-005 | `RunI4AddCopyTrace` | 窄形状 run>=4 | 仅作为 typed numeric lowering 过渡 |
| OK-006 | `RunStaticF4CallTrace` | 窄形状 run>=3 | 仅作为 typed ABI lowering 过渡 |
| OK-008 | `RunStaticI4CallTrace` | 窄形状 run>=3，镜像 OK-006 | static i4 AOT call → direct callsite cache |
| OK-007 | `i4[]` read-add-write fused opcode | ArrayOp 仍差 Pro 8x | 升级为 typed array memory IR |

---

## 详细条目

### WA-009 — 通用 dispatch + M2N 桥接 + Execute fallback 微优化（blocked）

- **尝试**：在 `CallCommon*Cached` / interp fallback / thunk 上堆 execute 层优化，指望通用 switch 循环追上 Pro；未强制 `RunStaticF4CallTrace` + `fastPathKind=32`。
- **实测**：CallAOTStatic 社区版占比约 8%→37%，opcode profile 仍为 `fastPathKind=1`、无 trace opcode；SetTransform 走 M2N。
- **根因**：Pro 性能来自 **transform 期专用 IR + execute 入口 bypass**，不是把桥接/dispatch 做快。
- **教训**：**断舍离 dispatch 方案**；14 base 每行必须 dedicated builder；专用慢于通用则删。
- **替代**：`benchmark-docs/god-domain-architecture.md`、`TransformContext_GodDomain.cpp`、Instinct 全表。
- **状态**：blocked；归档 `benchmark-docs/archive/generic-dispatch-bridge-retired.md`。

### WA-010 — scalar value-type generated M2N unguard（blocked）

- **尝试**：让 generated M2N bridge 对 bool/int/float/enum 等 scalar value-type return/param 直接调用，不再进入 `TryManaged2NativeCallByReflectionInvokeForSharedStruct`。
- **实测（旧 smoke 口径，已废弃）**：`HOTC233_LOCAL_OFFICIAL_COUNT=1` 构建通过，但 Player 在 `business-realworld-dictionary-config-lookup` 崩溃；exit `-1073741819`，native crash `1`。
- **证据**：`Assets/EditorForBuild/Generated/performance-hotc233-player.json.log`；栈为 `Dictionary_2_TryGetValue` -> `Dictionary_2_FindEntry` -> `ClassInlines::GetInterfaceInvokeDataFromVTable`，调用点为 generated bridge `__M2N_s203uuu`。
- **根因**：fully-shared generic Dictionary ABI 不能只凭标量 return 判断安全；rgctx/MethodInfo/hidden generic context 仍可能需要 invoker/reflection 保护。
- **教训**：业务字典优化不能通过移除 shared-struct guard 侥幸获得；必须做 fully-shared generic aware 的 typed ABI 或专门安全桥接。
- **替代**：新增显式 fully-shared generic M2N ABI 探针，确认 `methodPointerCallByInterp` 所需 rgctx/MethodInfo 形状后再落地 direct bridge。
- **状态**：blocked，代码已撤回。

### WA-013 — prepared interpreted call generic fast path（withdrawn）

- **尝试**：让 `CALL_INTERP_RET_PREPARED` 在建新解释帧前，对已解析的 `InterpMethodInfo` 尝试 `IsSafeGenericHotc233CallFastPath` + `TryExecuteHotc233FastPath`。
- **实测**：过滤 business 口径，Async 38.5%→45.5%、Task 38.9%→41.2%；但 List 31.6%→29.7%、Coroutine 31.8%→30.8%、Custom class dispatch 60.3%→51.8%、callback 82.8%→71.1%、event 82.0%→72.1%、Dictionary 86.8%→84.4%。
- **根因**：prepared call 热入口增加额外分支和 fastpath 检查，没有减少主要 frame/ABI/容器桥成本；对短业务 10 次口径还放大代码布局和首调用成本。
- **替代**：只在 transform/callsite 级预烘焙目标方法与 ABI，或按虚派发/接口/委托共享 callsite cache 证明净收益；不要在通用 prepared call 宏里补判断。
- **状态**：withdrawn，提交已 revert。

### WA-014 — 普通 `CallVirtual_*` receiver class callsite cache（withdrawn）

- **尝试**：在普通 `CallVirtual_void/ret/ret_expand` 的参数 resolveData 后追加 4 个 cache slot，按 receiver class 缓存 actual MethodInfo、InterpMethodInfo 与 M2N。
- **实测**：过滤 business 口径，callback 82.8%→106.0%、Custom class dispatch 60.3%→67.0%；但 List 31.6%→27.7%、event 82.0%→62.8%、Task 38.9%→36.8%、Dictionary 86.8%→84.4%，整体仍远低 CE。
- **根因**：业务弱项主成本不在普通虚表解析本身；额外 resolveData 与入口分支带来的代码布局/首调用成本抵消局部收益。
- **替代**：状态机 MoveNext、delegate multicast、List/Dictionary 泛型容器桥要分别在 typed ABI 或 transform 层解决；不要把普通虚调用缓存当作大一统方案。
- **状态**：withdrawn，提交已 revert。

### WA-015 — delegate 同步调用 whole-method fastpath（withdrawn）

- **尝试**：在 `TryInvokeInterpDelegateSynchronously` 中，对已分类 `InterpMethodInfo` 调用 `IsSafeGenericHotc233CallFastPath` + `TryExecuteHotc233FastPath`，试图让 multicast/callback 子 delegate 绕开解释帧。
- **实测**：过滤 business 口径，List 31.6%→25.3%、Coroutine 31.8%→30.5%、Task 38.9%→33.3%、Async 38.5%→41.7%、Custom class dispatch 60.3%→45.6%、event 82.0%→51.5%、callback 82.8%→72.0%、Dictionary 86.8%→82.8%、Struct 142.5%→132.6%。
- **根因**：delegate handler 里的运行时 fastpath 判定增加了分支、取 IMI 与代码布局成本；真正瓶颈仍是调用形状、参数 ABI 与容器/状态机热循环，没有在 transform 阶段被预烘焙。
- **替代**：delegate/callback/event 只做 transform 期 callsite plan：固定 invocation-list 形状、typed arg copy、直接 prepared target 表；运行时 handler 不再临时分类。
- **状态**：withdrawn，代码已撤回。

### WA-016 — small i4 leaf whole-method interpreter（withdrawn）

- **尝试**：为无分支、无字段、无对象操作、`ret i4` 的小方法增加 `Hotc233FastPath_SmallI4Leaf`，用栈上 64 slot 解释极小 i4 opcode 白名单，覆盖常见 `seed * const + const`、多次 add 等纯整数叶子函数。
- **实测**：过滤 business 口径，callback 82.8%→105.6%、Custom 60.3%→65.8%、Async 38.5%→45.5%；但 List 31.6%→28.6%、Task 38.9%→35.0%、Dictionary 86.8%→83.3%，Coroutine 基本不变。
- **根因**：局部小方法解释器仍然增加代码体积/布局成本，且没有解决 List/Dictionary/Coroutine/Async 的共同 ABI 和状态机成本；不满足“全面超过 CE”的发布门槛。
- **替代**：不要继续扩大小 leaf 白名单；统一进入 typed register IR，把 i4/ref/value slot 作为解释器基础形态。
- **状态**：withdrawn，提交已 revert。

### WA-017 — AOT container method plan cache（withdrawn）

- **尝试**：按 `MethodInfo*` 缓存 `List<T>`/`Stack<T>` 方法 kind 与字段 offset，在 `Managed2NativeCallAotContainerInvoker` 前置 plan 执行，减少每次方法名/klass 判断。
- **实测**：叠加 WA-016 后，List 仅 29.7%、Coroutine 31.3%、Task 38.9%、Async 45.5%、Custom 63.5%、Event 79.9%、Dictionary 84.1%、Callback 101.5%，仍未全面超过 CE。
- **根因**：M2N wrapper 仍在热路径，method plan cache 只去掉少量字符串/offset 判断，没有消除 call ABI、栈 slot、泛型容器访问和状态机步进成本。
- **替代**：List/Stack/Dictionary 需要 transform-time typed container IR 或 callsite direct container opcode；旧 M2N container fast path 只能保正确性 fallback。
- **状态**：withdrawn，提交已 revert。

### WA-018 — InterpreterInvoke / delegate hidden-ret 热路径诊断清理（withdrawn）

- **尝试**：删除 `InterpreterInvokeJoinProbe` 每次 N2M 调用的字符串匹配/日志分支，并移除 `InterpreterDelegateInvoke` hidden-return 前 64 次日志输出，期望降低短方法、delegate/event 与状态机调用开销。
- **实测**：过滤 business 口径，Async 38.5%→45.5% 变好；但 Tween 19.3%→19.0%、List 31.6%→25.7%、Custom 60.3%→54.1%、Dictionary 86.8%→80.9%、Struct 142.5%→129.5%，总体未过 CE 且多项回退。
- **根因**：这些诊断不是主瓶颈；代码布局/重编后首调用成本足以吞掉局部收益。短业务循环的主要成本仍是解释帧、typed ABI、泛型容器、delegate invocation-list 与状态机 MoveNext。
- **替代**：不要继续靠清理 `printf`/字符串探针追 CE；下一步必须做 transform-time business mechanism plan：typed container IR、delegate invocation plan、state-machine MoveNext typed path。
- **状态**：withdrawn，提交已 revert。

### WA-019 — CallCommon 绕路到 AOT container invoker（withdrawn）

- **尝试**：在 transform 阶段让 `System.Collections.Generic.List<T>.Clear/get_Count/Add/get_Item`、`Stack<T>.Push/Pop/get_Count`、`Dictionary<int,T>.TryGetValue` 不再生成 `CallCommonNativeInstance_*`，改走已有 `Managed2NativeCallAotContainerInvoker` / Dictionary M2N fast path。
- **实测**：过滤 10 条 business 口径，List 31.6%→22.2%、Coroutine 31.8%→25.6%、Dictionary 86.8%→81.8%、Custom 60.3%→68.9%、Callback 82.8%→103.7%、Struct 142.5%→126.7%；仍 8/10 低于 CE，整体不满足发布门禁。
- **根因**：已有 AOT container invoker 仍是 M2N 包装层，只减少/改变 transform 路由，不能消除容器访问的栈 slot、泛型元素 ABI、fallback invoke 和短循环首调用成本；局部 callback 提升不能覆盖 List/Dictionary/Coroutine 回退。
- **替代**：容器必须走 transform-time typed container IR：直接烘焙 `List<T>`/`Dictionary<TKey,TValue>` 字段 offset、元素 ABI、fallback callsite，并把 `get_Count/Clear/get_Item/Add/TryGetValue` 做成通用容器 IR 或 typed helper；旧 M2N 容器 fast path 只保正确性 fallback。
- **状态**：withdrawn，代码已撤回；禁止继续把 `CallCommon` 改路到 M2N invoker 当作生产级优化。

### WA-020 — List get_Count/get_Item 普通 IR 展开（withdrawn）

- **尝试**：把 `List<T>.get_Count` 展开为 `_size` 字段读取，把 `List<T>.get_Item(int)` 展开为 `_items` 字段读取 + array element IR，复用现有 `Ldfld*` / `GetArrayElement*` opcode，不新增容器专用 opcode。
- **实测**：过滤 `business-realworld-list-pool-rent-return`，List 31.6%→29.3%，仍低于 CE 且低于现有基线；构建成功但性能回退。
- **根因**：普通 IR 展开把一次方法调用换成多个 interpreter dispatch；虽然消除了 M2N/CallCommon 边界，但没有融合 `_items/_size/range/element copy`，短循环里 dispatch 数增加抵消并超过收益。
- **替代**：List/Dictionary 必须做 fused direct container opcode/helper：单 dispatch 内完成字段读、范围检查、元素 copy/写入，并携带 fallback callsite；不要把容器方法拆成更多普通 IR。
- **状态**：withdrawn，代码已撤回。

### WA-021 — CallCommon handler 内 fused List<int> fastpath（withdrawn）

- **尝试**：在现有 `CallCommonNativeInstance_v_0/i4_0/v_i4_1/i4_i4_1` handler 内添加 `List<int>.Clear/get_Count/Add/get_Item` fused fastpath，按 `Il2CppClass*` 缓存 `_items/_size/_version` 字段，未命中或扩容/越界时 fallback 到原 AOT 调用。
- **实测**：过滤 `business-realworld-list-pool-rent-return`，List 31.6%→23.45%，构建成功但明显回退。
- **根因**：把分支、字符串方法判定、unordered_map plan cache 塞进巨型 `Interpreter_Execute` 热 handler，会拖累 CallCommon 代码布局与短循环首调用；即使单次容器操作更直接，整体 dispatch/codegen 成本仍更高。
- **替代**：禁止继续在 CallCommon handler 里堆容器分支。容器优化必须 transform 期生成独立 fused opcode/side-table，或 whole-method/container-loop bypass；fallback 只能在独立冷路径里处理。
- **状态**：withdrawn，代码已撤回。

### WA-022 — List<int> 独立单调用 fused opcode（withdrawn）

- **尝试**：在 transform 阶段识别 `List<int>.Clear/get_Count/Add/get_Item`，生成独立 `ListInt32Call` opcode，在单 dispatch 内烘焙 `_items/_size/_version` offset，并仅在扩容/越界时 fallback 到原 AOT method pointer。
- **实测**：过滤 `business-realworld-list-pool-rent-return`，List 31.6%→30.88%，构建成功但仍低于 CE 且略低于稳定基线。
- **根因**：单个容器方法的 fused opcode 没有覆盖业务 List pool 的整体模式；短循环主要成本还包含解释帧、循环控制、池化调用链、泛型容器多调用组合与首轮 warmup。仅把 `List<int>` 单调用换成 direct opcode，收益不足以抵消新增 opcode/codegen 布局成本。
- **替代**：停止推进“单容器方法独立 opcode”作为主线。List/Dictionary 需要更高层的 pattern：方法级/循环级 container plan、状态机/业务方法 whole-method bypass、typed register IR 上的容器 access lowering，并且必须同时验证 Coroutine/Async/Custom/Event 不回退。
- **状态**：withdrawn，代码已撤回。

### WA-023 — `CALL_INTERP_RET_PREPARED` 宏内通用 fastpath（withdrawn）

- **尝试**：让 `CALL_INTERP_RET_PREPARED` 在进入新解释帧前，使用已取得的 `preparedImi` 调用 `IsSafeGenericHotc233CallFastPath` + `TryExecuteHotc233FastPath`，目标是覆盖虚派发、delegate cache 与其它 prepared callee 的小返回值方法。
- **实测**：过滤 `business-realworld-custom-class-dispatch` 时，`Interpreter_Execute.cpp` 所在 IL2CPP 大翻译单元 `re83h2fuc10b.obj` 编译超过 400s 仍未完成，远慢于上一轮约 180s；已中断本轮自动化构建，没有可接受的性能报告。
- **根因**：在大解释器宏入口增加跨全局 inline fastpath 检查，会放大 MSVC 优化器在超大 switch/宏翻译单元上的编译成本；即使运行时可能受益，构建时间风险已不符合“测试要快”和生产可维护要求。
- **替代**：prepared callee fastpath 不能塞进通用宏。后续应在 transform 期生成窄 opcode/side-table，或把 small-i4 leaf/virtual-return lowering 放到独立冷编译单元和明确 handler 内；任何改 `Interpreter_Execute.cpp` 热宏的路线必须先做编译时长 smoke。
- **状态**：withdrawn，代码已撤回。

### WA-024 — SmallI4AddLeaf 栈上小表 evaluator（withdrawn）

- **尝试**：为 `Ldloc/Ldc/Add/Ret i4` 小叶子方法增加 whole-method evaluator，使用栈上 96 槽小表模拟无对象、无字段、无分支的四路 `arg + const` 聚合，目标覆盖 `Task.WhenAll` 同类同步 helper。
- **实测**：过滤 `business-realworld-task-whenall,async-await-loop,custom-class-dispatch`，构建正常且未触发 `re83h2fuc10b.obj` 超时，但 Task.WhenAll 从上一轮 36.84% 降到 33.33%；Async 45.45%、Custom 66.97% 未继续受益。
- **根因**：小表 evaluator 仍是一个微型解释循环，未消掉 Task 聚合主成本；额外分类/执行分支和局部小表读写抵消收益。
- **替代**：不要再做“无分支小 opcode 白名单解释器”。Task/async 应走状态机/聚合方法的 transform-time side-table plan，或独立 helper 直接计算 affine sum；先用 `RuntimeApi.GetMethodOpcodeProfile` 接入真实 business method profile，再写窄 lowering。
- **状态**：withdrawn，代码已撤回。

### WA-025 — `mul const` 后接二元操作 whole-method helper（withdrawn）

- **尝试**：识别 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4` 的 48 字节小方法，作为 `AsyncComputeSync` 与业务虚派发 `Tick` 同类形状的通用 whole-method helper。
- **实测**：严格 operand 校验版本未命中，profile 显示 `AsyncComputeSync`、`WarriorActor.Tick`、`MageActor.Tick`、`RangerActor.Tick` 仍为 `fastPathKind=1`；放宽到 opcode 序列后，过滤 `custom-class-dispatch,async-await-loop,task-whenall` 回退到 Custom 35.27%、Async 38.46%、Task 31.82%。
- **根因**：call fastpath 阶段没有完整解释帧，直接按 IR local slot 读取参数/临时槽不可靠；放宽 transform 分类会把小方法推入错误/低效 helper，虚派发场景还会放大 fallback 和代码布局成本。
- **替代**：小整数业务 helper 需要真正的 baked side-table 参数计划，明确记录参数槽与常量槽，不能在 runtime 里临时猜 IR local slot。先保留 business opcode profile 诊断，再做 transform-time plan。
- **状态**：withdrawn，代码已撤回。

### WA-026 — 参数 buffer 版 static affine helper（withdrawn）

- **尝试**：在 `TryExecuteHotc233CallFastPath` 阶段仅从调用参数 buffer 读取一个 `int32` 参数，识别 `arg * const + const` 与四路 `arg + const` 聚合，避免再次进入解释帧，目标覆盖 `async-await-loop` 与 `task-whenall` 的静态同步 helper。
- **实测**：过滤 `async-await-loop,task-whenall`，Async 38.5%→31.25%，Task 38.9%→35.0%，仍显著低于 CE 且回退；报告生成时间 `2026-07-01T09:06:09.315531Z`。
- **根因**：单个静态 helper 的直接计算不是 async/task 业务弱项主成本；状态机调度、awaiter/Task 聚合、解释帧切换和短循环 warmup 仍占主导。把窄 affine 逻辑塞进通用 call fastpath 还会增加热分支与代码布局风险。
- **替代**：async/task 必须走状态机级通用机制：MoveNext typed lowering、awaiter/Task 聚合 callsite plan、delegate continuation cache 和可验证的 side-table；不要再为单个同步 helper 写 call fastpath 特判。
- **状态**：withdrawn，代码已撤回。

### WA-027 — 虚派发实例 `int -> int` affine 小叶子 fastpath（withdrawn）

- **尝试**：把 `arg * const (+/-/^ const)` 返回 `int` 的实例小叶子方法分类为通用 fastpath，并只在 `CallInterpVirtual_ret` 中直接执行，目标覆盖自定义 class 虚派发的 `Tick(int)` 形状，不打开 static async/task 路径。
- **实测**：单行过滤 `custom-class-dispatch` 从完整基线 60.3% 提到 68.9%，但仍低于 CE；宽过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain` 结果为 0/8 通过，Coroutine 15.1%、Event 19.5%、List 29.5%、Task 41.2%、Async 41.7%、Custom 68.9%、Dictionary 76.0%、Callback 86.9%。
- **根因**：业务弱项主成本不在单个 `Tick` 小叶子的 4 条 opcode，而在调用者短循环、虚派发解析、状态机/delegate/container 固定成本和代码布局；小叶子 fastpath 局部改善不足以抵消整体路径风险。
- **替代**：下一步不要继续堆单 leaf fastpath。做虚派发 inline cache、方法级 loop plan、MoveNext typed lowering、delegate/event invocation-list plan 与 List/Dictionary fused container plan。
- **状态**：withdrawn，代码已撤回。

### WA-028 — `CallInterp_void/static_void` 一次解析 + prepared frame（withdrawn）

- **尝试**：把 void 解释器调用改成先解析 `InterpMethodInfo`，再走 `CALL_INTERP_RET_PREPARED`，并在无返回调用上复用 fastpath 检查，目标是通用减少解释帧入口固定成本。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T09:45:27.7638606Z`，只有 callback 104.66% 通过；Coroutine 27.39%、List 29.19%、Async 35.71%、Task 38.89%、Custom 65.77%、Event 73.72%、Dictionary 87.03%，整体弱于只保留 `CallInterp_ret` cache 的上一轮。
- **根因**：void 调用路径的主要成本不等于一次 `GetInterpMethodInfo`；强行把 void 走 prepared-ret 框架增加了额外分支、cctor/fastpath 检查和代码布局扰动，短 business 口径下负收益更明显。
- **替代**：保留实例 `CallInterp_ret` callsite cache；void 路径后续只允许基于 callsite side-table 的窄验证，优先推进虚派发 inline cache、MoveNext typed lowering、delegate/event invocation-list plan 和容器循环计划。
- **状态**：withdrawn，代码已撤回。

### WA-029 — 同步 `ExecutePrepared` 入口 + `CallInterpVirtual_ret` receiver cache（withdrawn）

- **尝试**：新增 `Interpreter::ExecutePrepared`，让 delegate multicast 等必须同步跑完子调用的路径复用已解析 `InterpMethodInfo`；同时在 `CallInterpVirtual_ret` 用 receiver class 缓存实际派发 `MethodInfo`，目标覆盖 event/callback、自定义 class 虚派发和常见多态热更调用。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T09:54:36.313276Z`，仍只有 callback 107.3% 通过；List 24.1%、Coroutine 28.7%、Async 33.3%、Task 36.8%、Custom 47.1%、Event 81.8%、Dictionary 83.3%，相比只保留 `CallInterp_ret` cache 的 09:39 结果整体回退。
- **根因**：同步 prepared 入口没有改变 multicast/状态机/容器短循环的主成本，反而在超大解释器翻译单元里增加入口分支和代码布局压力；`CallInterpVirtual_ret` 单槽 receiver cache 对多派生类型轮换场景命中率低，还增加 side-table 读写成本。
- **替代**：不要继续在通用解释器入口叠 cache。下一步做 transform-time 大颗粒度计划：MoveNext typed lowering、List/Dictionary 容器循环 plan、delegate/event invocation-list plan、String/hash/equality direct path；虚派发要做多形态 callsite plan 或方法级 lowering，而不是单槽 runtime cache。
- **状态**：withdrawn，代码已撤回。

### WA-030 — AOT 容器 method-kind runtime cache（withdrawn）

- **尝试**：把 `List<T>.Clear/get_Count/Add/get_Item` 与 `Stack<T>.get_Count/Push/Pop` 的字符串分类缓存为 `MethodInfo* -> kind`，让 `Managed2NativeCallAotContainerInvoker` 每次调用少做 method-name `strcmp`。
- **实测**：第一次编译暴露 bool/enum 返回误替换，修复后过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T10:18:13.8110847Z`，仍只有 Dictionary 193.7% 通过；List 36.7%、Async 62.5%、Coroutine 76.0%、Task 77.8%、Callback 89.9%、Custom 54.9%、Event 54.4%，不优于 10:08 的已知结果。
- **根因**：容器弱项不是 method-name 分类本身；新增 unordered_map 查询和代码布局扰动抵消了少量 `strcmp` 收益，且没有触及 List pool 的主成本：泛型 List 小循环、字段 offset plan、bounds/version 更新、元素搬运与调用边界。
- **替代**：容器下一步必须做 callsite/method-level plan：transform 期预烘焙 List/Dictionary 访问计划，或把 Rent/Return 类小循环 lowering 成 typed container op；不要再做 runtime method-kind 微 cache。
- **状态**：withdrawn，代码已撤回。

### WA-031 — 小型 i4 straight-line 通用 fastpath（withdrawn）

- **尝试**：把无分支、无异常、只含 i4 copy/const/binop/ret 的小方法分类为 `I4SmallStraightLine`，在 `TryExecuteHotc233FastPath` 内用小型解释循环直接执行，目标服务 callback/custom/async 状态机中的短叶子方法。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T10:35:51.027083Z`，2/8 通过；Callback 108.4%、Dictionary 147.8% 通过，但 List 59.1%、Async 62.5%、Custom 69.5%、Event 71.0%、Coroutine 72.1%、Task 87.5% 仍失败，且 List/Custom/Event/Coroutine 相比 10:26 的有效基线明显回退。
- **根因**：通用 “扫小方法 bytecode 再解释一次” 仍是 runtime switch，而不是 typed ABI 或 callsite lowering；它偶然改善 callback，但扩大了代码体积和调用侧判断，并让一批本可走原 fastpath/普通解释的短业务方法落入更慢路径。
- **替代**：不再做运行时通用小 bytecode loop。下一步改为 transform-time typed plan：delegate/event invocation-list plan、MoveNext/async 状态机 lowering、List/Dictionary typed container op、virtual/interface callsite plan。
- **状态**：withdrawn，代码已撤回。

### WA-032 — prepared 调用宏内联 fastpath 检查（aborted）

- **尝试**：把 `CALL_INTERP_RET_PREPARED` 改成进入新解释帧前先调用 `IsSafeGenericHotc233CallFastPath` + `TryExecuteHotc233FastPath`，目标让虚派发、delegate、CallInd 等 prepared callsite 共享现有 whole-method fastpath。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，命令生成日志 `Hotc233Data/AutomationLogs/20260701T103950.639681400Z-Tuanjie.exe.log`；IL2CPP 构建停在单个 `cl.exe` lump obj 超过 8 分钟，未产出可用 performance JSON，手动中止。
- **根因**：把 fastpath 判断塞进高频 prepared 宏会膨胀解释器主翻译单元和 C++ 优化负担，违反“测试要快”和生产构建成本要求；即使可能有 runtime 收益，也不能接受编译成本爆炸。
- **替代**：prepared fastpath 只能做窄 callsite IR 或 helper out-of-line 验证，不再在大宏中内联公共检查。优先做 transform-time delegate/event plan、virtual/interface polymorphic callsite plan 和状态机 typed lowering。
- **状态**：aborted，代码已撤回。

### WA-033 — `CallInterpVirtual_ret` 4-entry receiver cache（withdrawn）

- **尝试**：给解释器虚派发返回调用加 4-entry `{receiver class, MethodInfo, InterpMethodInfo}` 小型多态 cache，目标覆盖真实业务中多个派生类轮换的 custom class dispatch。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T11:04:13.7517264Z`，1/8 通过；Event 93.9%、List 89.6%、Dictionary 213.2% 有提升，但 Custom 76.0%→71.6%、Callback 115.4%→97.4%、Coroutine 77.4%→75.8%、Async 71.4%→62.5% 回退。
- **根因**：在虚派发 opcode 热路径做线性 cache 查找和 resolveData 写回增加了所有虚调用固定成本；3 类型轮换场景下省掉 `GET_OBJECT_VIRTUAL_METHOD` 不足以抵消 cache 搜索、分支和代码布局扰动。
- **替代**：虚派发需要 transform-time typed callsite plan 或 devirtualization/lowering，不再在 opcode 内做 runtime 多态 cache。Custom dispatch 优先做 sealed/known actor 方法级 lowering，或用更粗粒度的方法 trace。
- **状态**：withdrawn，代码已撤回。

### WA-034 — `i4 * const +/- const` whole-method fastpath（withdrawn）

- **尝试**：识别 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4` 的 48 字节 typed IR，打包源 slot、乘数和尾常量到 `hotc233FastPathParam`，执行端直接计算，目标覆盖 `seed*31+7`、业务 actor `Tick` 等短叶子方法。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T11:27:25.007265Z`，2/8 通过；List 90.5%、Event 94.8%、Dictionary 216.5% 小涨，但 Async 71.4%→62.5%、Coroutine 77.4%→68.4%、Custom 76.0%→71.6%、Callback 115.4%→106.3% 回退。
- **根因**：真实业务短方法没有稳定落入该单一 48 字节形状，新增 kind 和 virtual-ret 窄尝试带来的代码布局/分支成本超过命中的收益；这仍然太像局部形状猜测，不是状态机/虚派发的主路径解决方案。
- **替代**：不要继续扩“小公式”枚举。下一步应做可观测的 transform profiling，先输出每个 business blocker 的 top hot method 与 fastpath kind 命中情况，再按状态机、容器、delegate/event 做 plan。
- **状态**：withdrawn，代码已撤回。

### WA-035 — List fast path method-name branch reorder（withdrawn）

- **尝试**：把 `TryManaged2NativeCallListFastPath` 的分支顺序从 `get_Count -> Clear -> Add -> get_Item` 调成 `get_Count -> Add -> get_Item -> Clear`，目标减少 `List<int>.Add/get_Item` 的 `strcmp` 固定成本。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T11:33:04.2279226Z`，2/8 通过；Callback 117.4%、Dictionary 209.9% 通过，但 List 89.1%→86.3%、Event 87.8%→85.1%、Async 71.4%→62.5%、Custom 76.0%→73.7% 回退。
- **根因**：List pool 弱项不是这几个 `strcmp` 的顺序；代码布局扰动和 Clear/Count 热路径变化抵消了 Add/get_Item 的少量收益。
- **替代**：容器要做 typed container op 或 method-level lowering，不能继续调字符串分派顺序。
- **状态**：withdrawn，代码已撤回。

### WA-036 — method-level `copy * const +/- const` 小公式分类（withdrawn）

- **尝试**：把 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4` 识别成新的 method-level `Hotc233FastPathKind`，再把虚派发返回与静态返回调用接到 prepared fastpath，目标覆盖 `AsyncComputeSync` 和 `Actor.Tick` 这类真实业务小叶子方法。
- **实测**：连续三轮过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`：`2026-07-01T11:41:40Z` 2/8 通过，`2026-07-01T11:43:39Z` 2/8 通过，`2026-07-01T11:44:57Z` 3/8 通过；但 `opcode-profile-business.json` 中 `AsyncComputeSync`、`WarriorActor.Tick`、`MageActor.Tick`、`RangerActor.Tick` 始终仍为 `fastPathKind=1`，分类未命中。
- **根因**：method-level 分类对 transform 后 slot 边界、initLocals、本地临时与 eval stack 形状过敏；为了命中而继续放宽会回到 WA-031/WA-034 的“小公式枚举”陷阱，且不能解决状态机、容器、event 的共同主成本。
- **替代**：撤掉新增 `Hotc233FastPathKind` 与分类器；若要覆盖这种直线序列，只允许在已有融合 opcode handler 内做带 codeLength 边界的窄 peephole，随后仍以状态机、container、delegate/event 机制桶为主。
- **状态**：withdrawn，method-level 分类代码已撤回；保留的 peephole 需要后续批量验证。

### WA-037 — 直线 opcode peephole + `List<int>` 返回少清零（withdrawn）

- **尝试**：在已有 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4` handler 内窄识别后继 `LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4`，并减少 `List<int>.get_Count/get_Item` fast path 对 `StackObject` 的整块清零，目标同时覆盖 Async/Custom 小叶子与 List pool 固定成本。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T11:52:25.9919798Z`，仅 Dictionary 191.0% 通过；Async 55.6%、Custom 65.2%、Coroutine 76.3%、List 87.1%、Task 87.5%、Event 89.3%、Callback 99.4%。
- **根因**：execute 层直线窥视增加了融合 opcode 热路径的后继读取和分支，实际短业务方法的主成本仍在调用边界、状态机、容器/委托结构；List 少清零节省不足以抵消代码布局与调用路径扰动。
- **替代**：撤回 execute peephole 和 List 少清零；下一步必须做更粗粒度机制桶：状态机 MoveNext lowering、delegate/event invocation-list plan、List/Dictionary typed container op、virtual/interface callsite plan。
- **状态**：withdrawn，代码撤回。

### WA-038 — virtual prepared callsite 复用 delegate-only fastpath helper（withdrawn）

- **尝试**：把已经验证有收益的 delegate-only `CALL_INTERP_RET_PREPARED_FAST` helper 扩到 `CallVirtual_ret` 和 `CallInterpVirtual_ret`，希望虚派发小叶子方法也能在进入解释帧前执行现有 safe fastpath。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T12:04:33.2393861Z`。隔离回原 `CALL_INTERP_RET_PREPARED` 后，Async 41.7%→62.5%、Task 58.3%→87.5%、Coroutine 66.7%→81.1%、Callback 115.0%→120.4%、Dictionary 199.3%→214.8%；但 List 85.9%→71.5%、Event 87.1%→84.1%、Custom 70.9% 持平，仍只有 2/8 通过。
- **根因**：delegate helper 的收益来自单播闭包/cache 形状；虚派发 callsite 复用同一 helper 会把额外分支和 code layout 成本带到 List/Event 等普通业务路径，且 `Actor.Tick` 仍为 `fastPathKind=1`，没有解决虚派发主瓶颈。
- **替代**：virtual/interface 只能走 transform-time typed callsite plan 或方法级 devirtualization/lowering；不再把 delegate-only helper 扩到虚派发通用入口。
- **状态**：withdrawn，virtual/interp virtual callsite 已恢复 `CALL_INTERP_RET_PREPARED`；delegate 分支保留 `_FAST`。

### WA-043 — 独立 `MulConstThenConstRetI4` 48-byte fastpath（withdrawn）

- **尝试**：新增独立 `Hotc233FastPath_MulConstThenConstRetI4`，只识别 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4`，不复用 `CopyConstMulRetI4`，不改 virtual handler，目标覆盖 `AsyncComputeSync`、`Actor.Tick` 这类普通业务短整数 helper。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T13:31:12.5685046Z`，仍仅 2/8 通过；Custom 61.3%、Async 62.5%、Coroutine 76.3%、Task 87.5%、Event 89.9%、List 95.1%、Callback 104.3%、Dictionary 208.3%。
- **根因**：profile 仍显示 `AsyncComputeSync` 和三个 `Actor.Tick` 为 `fastPathKind=1`，说明 post-hoc 48-byte classifier 没有进入有效运行口径；即使不改 virtual handler，新增 helper/classifier 仍让 Custom、Async、Event、List 相对 12:55 有效基线回退。该路线与 WA-039/WA-040 同属“48-byte 后验公式分类”问题。
- **替代**：撤回 enum/helper/classifier。后续短 helper 必须从 transform-time side-table 或调用点计划落地，并以 method profile 证明命中；不得继续添加 48-byte post-hoc classifier。
- **状态**：withdrawn，代码已撤回。

### WA-039 — transform-time `I4MulConstBinConst` leaf fastpath（withdrawn）

- **尝试**：在 transform 后扫描 48 字节 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4`，新增 `Hotc233FastPath_I4MulConstBinConst`，并只在 virtual/interp virtual 返回调用处窄旁路该 kind，目标覆盖 `AsyncComputeSync`、`TaskWhenAllSync`、`Actor.Tick` 等业务小叶子。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T12:34:08.1985372Z`，仅 1/8 通过；Async 55.6%、Custom 55.7%、Callback 65.7%、Coroutine 73.1%、Task 77.8%、List 78.4%、Event 89.9%、Dictionary 188.4%。
- **根因**：profile 仍显示 `AsyncComputeSync`、`TaskWhenAllSync`、`Actor.Tick`、`FrameStepper.MoveNext` 为 `fastPathKind=1`，说明 post-hoc bytecode classifier 仍未命中真实生成 IMI；新增 enum/helper/virtual 分支扰动 hot interpreter 布局并让 Callback/Custom 明显回退，`re83h2fuc10b.obj` 编译成本也升到约 262s。
- **替代**：停止做 48 字节后验公式分类。下一步必须在 IR 构造期记录 typed plan/side-table，或先输出真实 operand profile 后再做 lowering；runtime 不再猜 local/eval slot。
- **状态**：withdrawn，代码已撤回。

### WA-040 — 复用 `CopyConstMulRetI4` 覆盖 48 字节 leaf + virtual 窄旁路（withdrawn）

- **尝试**：按真实 opcode 顺序 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor + RetVar_ret_4`，复用已有 `Hotc233FastPath_CopyConstMulRetI4`，并在 virtual/interp virtual ret callsite 只旁路该 kind，目标避免新增 enum，同时覆盖 Async/Task 小 helper 和 Actor.Tick。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T12:47:20.2926235Z`，仅 1/8 通过；Coroutine 47.3%、Async 62.5%、Custom 63.5%、Task 77.8%、Event 80.4%、List 83.6%、Callback 92.1%、Dictionary 209.9%。`re83h2fuc10b.obj` 编译约 279s。
- **根因**：性能没有形成有效收益，且 callback 从 12:04 的 120.4% 跌到 92.1%；该路线仍把额外判定放进巨型 execute 文件和 virtual 热入口，编译成本继续恶化。过滤运行没有刷新 `opcode-profile-business.json`，无法用旧 profile 证明命中，因此后续必须先增强 method profile 输出 operand/arg/local 诊断。
- **替代**：撤回 execute/virtual 改动。下一步只允许先用 `RuntimeApi.GetMethodOpcodeProfile` 的 operand 诊断确认真实 ABI，再将 helper 放到 transform side-table 或独立小编译单元，避免污染 `Interpreter_Execute.cpp`。
- **状态**：withdrawn，代码已撤回；保留 RuntimeApi 诊断增强。

### WA-041 — `StraightLineI4Ret` execute 内小解释器（withdrawn）

- **尝试**：新增 `Hotc233FastPath_StraightLineI4Ret`，transform 识别无分支、无调用、只含 i4 copy/const/binop/ret 的直线小方法，runtime 在 `Interpreter_Execute.cpp` 内用 32-slot 小寄存器数组求值，目标覆盖 `AsyncComputeSync`、`TaskWhenAllSync` 和普通业务小 helper，同时避免读取未建帧 local/eval slot。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T13:10:43.59906Z`，仍仅 2/8 通过；Async 71.4%→62.5%、Custom 76.8%→67.0%、Event 92.3%→83.9%、List 98.1%→97.5%、Callback 117.8%→104.0%，只有 Coroutine 70.2%→75.4% 小涨但仍失败。`re83h2fuc10b.obj` 编译超过 175s。
- **根因**：把“小解释器”继续放进巨型 execute 翻译单元会扰动代码布局并增加编译成本；即使命中直线小方法，收益也不足以覆盖调用边界、状态机、delegate/event、container 的共同成本。这与 WA-031/WA-037 同属 execute 内二次解释路线。
- **替代**：撤回 enum、transform classifier 和 execute helper。后续小 helper 只能做 transform-time side-table + 独立小编译单元/typed helper，并且必须先证明可改善 Async/Task 且不拖累 Event/Callback/List。
- **状态**：withdrawn，代码已撤回。

### WA-042 — List/Stack 返回槽少清零（withdrawn）

- **尝试**：在 `List<T>/Stack<T>` AOT 容器 fast path 中，对 `get_Count`、`List<int>.get_Item`、`Stack<T>.Pop` 的 int/ref 返回只写实际返回槽，不再 `memset(ret, 0, sizeof(StackObject))`，目标让接近 CE 的 `business-realworld-list-pool-rent-return` 从 98.1% 过线。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T13:19:19.1589201Z`，仍 2/8 通过；List 98.1%→69.4% 大幅回退，Coroutine 70.2%→68.1%，Dictionary 218.3%→208.3%，Event 92.3%→94.8% 但仍未过。
- **根因**：返回槽整块清零在当前 ABI 下不是纯冗余；部分调用/展开路径依赖高位或对象槽处于确定状态。少清零会带来数据形态/代码布局回退，不能作为容器优化方向。
- **替代**：容器要做 transform-time typed container op 或循环级 plan，而不是改返回槽清零。List pool 的主成本仍在泛型容器调用边界、Add/get_Item 循环和 Stack/List 方法调度。
- **状态**：withdrawn，代码已撤回。

### WA-044 — ABI 修正后重做 48-byte affine return fastpath（withdrawn）

- **尝试**：在修正 `argStackObjectSize` StackObject 槽单位后，新增 `Hotc233FastPath_MulConstThenConstBinRetI4`，识别 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor/And + RetVar_ret_4`，目标覆盖 `AsyncComputeSync` 与三个 `Actor.Tick`，不改 delegate/multicast 入口。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T13:57:21.3986158Z`。新 profile 确认 `AsyncComputeSync` 与 `Warrior/Mage/RangerActor.Tick` 均命中 `fastPathKind=83`，但 business 只有 1/8 通过：Async 62.5%、Custom 64.6%、Coroutine 80.1%、Task 87.5%、Event 89.3%、List 92.4%、Callback 99.7%、Dictionary 214.8%。`re83h2fuc10b.obj` 编译约 170s。
- **根因**：48-byte affine return 即使命中，也不能覆盖真实主成本：Async/Task 的外层状态机/Task 聚合、Custom 的虚派发入口、Coroutine 的 MoveNext 字段状态流、List pool 的容器调用边界仍占主导。把 helper 放进巨型 `Interpreter_Execute.cpp` 还显著增加编译成本并扰动 Event/Callback/List。
- **替代**：撤回 enum/classifier/execute helper。短整数 helper 只能作为更大机制桶的一部分落地：transform side-table + 独立小编译单元，或直接做 virtual/interface callsite plan 与状态机/container lowering；禁止再单独提交 48-byte affine return fastpath。
- **状态**：withdrawn，代码已撤回；保留 ABI 槽单位修正与本机 opcode profile 生成。

### WA-045 — Closure field-add void fastpath in `Interpreter_Execute.cpp`（withdrawn）

- **尝试**：新增 `Hotc233FastPath_ClosureFieldAddVoidI4`，识别闭包方法中“读取捕获字段 -> 计算小整数表达式 -> `+=` 写回字段 -> void 返回”的通用形状，并让 multicast delegate 子调用先尝试 prepared generic fastpath。目标覆盖 event multicast 与 callback 链，同时作为通用闭包字段更新优化。
- **实测 1**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T14:19:31.6418789Z`。profile 确认 Event/Callback 的 6 个闭包方法命中 `fastPathKind=83`；Event 101.2% 过 CE，Callback 97.1% 近线，但总体只有 2/8：List 62.3%、Async 62.5%、Custom 74.5%、Task 77.8%、Coroutine 79.4% 仍失败。
- **实测 2**：把 runtime 小数组/小循环收缩成固定短表达式执行器后，生成时间 `2026-07-01T14:27:40.1414862Z`。Callback 131.6%、Dictionary 213.2% 通过，但 Event 96.98% 回落，List 75.9%、Task 87.5%、Custom 62.9%、Coroutine 70.2%、Async 62.5% 仍失败；仍只有 2/8。
- **根因**：闭包字段更新本身可以命中，但把该执行器放进巨型 `Interpreter_Execute.cpp` 仍会带来代码布局/编译成本扰动；multicast 子调用快路也没有解决真实业务主成本，反而让 List/Task/Custom/Coroutine 相对 14:02 保留基线回退。
- **替代**：撤回 enum、transform classifier、runtime helper 和 multicast prepared generic 调用。后续 event/callback 只能作为 delegate/closure 机制桶的一部分，以独立小编译单元或 transform side-table 形式实现，并且必须不拖累 List/Task/Custom/Coroutine。
- **状态**：withdrawn，代码已撤回。

### WA-046 — External affine i4 leaf fastpath（withdrawn）

- **尝试**：把 `arg * const (+/-/^/&) const` 的 48-byte 小叶子从 `Interpreter_Execute.cpp` 挪到独立 `Hotc233FastPath.cpp`，由 transform 编码 `src/mulConst/binConst/op` 到 `hotc233FastPathParam`，execute 只做外部函数调用。目标覆盖 `AsyncComputeSync` 和三个 `Actor.Tick`，避免 WA-044 的大翻译单元 helper 扰动。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T14:53:11.2676609Z`。profile 确认 `AsyncComputeSync` 和 `Warrior/Mage/RangerActor.Tick` 全部命中 `fastPathKind=83`，但 business 仍只有 2/8：Custom 51.0%、Async 62.5%、Coroutine 79.4%、List 84.2%、Task 87.5%、Event 90.1%、Callback 117.4%、Dictionary 221.8%。
- **根因**：小叶子命中不等于真实业务通过。Async/Task 外层循环、虚派发入口、状态机/容器/事件调用边界仍占主导；即便 helper 独立编译，新增 fastpath kind 和调用分支仍会扰动 Custom/Event/List，且 Async 没有提升。
- **替代**：撤回 enum、外部文件、transform classifier 和 execute 调用。后续小叶子只允许作为更大 callsite/状态机 lowering 的一部分，而不是单独按 48-byte 方法做发布 blocker 修复。
- **状态**：withdrawn，代码已撤回。

### WA-047 — 48-byte static leaf fastpath in generic call path（withdrawn）

- **尝试**：新增 `Hotc233FastPath_I4MulConstThenBinConstRet`，在 transform 后识别 `LdlocVarVar_LdcVarConst_4_BinOpMul_i4 + LdcVarConst_4 + Add/Sub/Xor/And + RetVar_ret_4`，并把该 kind 加入普通 static/interp call fastpath 白名单。目标覆盖 `AsyncComputeSync` 和三个 `Actor.Tick`，不改 virtual handler。
- **实测**：过滤 `custom-class-dispatch,async-await-loop,task-whenall,coroutine-stepper,list-pool-rent-return,dictionary-config-lookup,event-multicast,callback-chain`，生成时间 `2026-07-01T15:30:29.1385158Z`。profile 确认 `AsyncComputeSync`、`WarriorActor.Tick`、`MageActor.Tick`、`RangerActor.Tick` 命中 `fastPathKind=83`，但 business 退到 1/8：Dictionary 191.0% 通过；Callback 95.5%、Event 84.4%、List 70.8%、Async 62.5%、Custom 59.8%、Task 53.8%、Coroutine 50.4% 均失败。
- **根因**：静态小叶子本身不是主成本。真实 business 瓶颈仍在外层解释调用、虚派发、状态机字段流、泛型容器 bridge、delegate/multicast invoke；即使命中小叶子，也会因新增分类/白名单分支和代码布局扰动让已接近 CE 的行回退。
- **替代**：撤回 enum、classifier、execute helper 和 call fastpath 白名单。下一轮必须对照 HybridCLR CE 的公开机制补通用层：callsite typed ABI、state-machine lowering、container typed op、delegate invocation plan；不得再提交 48-byte 小叶子独立路线。
- **状态**：withdrawn，代码已撤回。

### WA-012 — List/Stack 专用 M2N wrapper 分派（withdrawn）

- **尝试**：在 `GetManaged2NativeMethodPointer` 中把 `List<T>.Clear/get_Count/Add/get_Item` 与 `Stack<T>.get_Count/Push/Pop` 分到专门 wrapper，减少每次 AOT 容器桥接的字符串分派。
- **实测**：过滤 business 跑修正编译错误后，`business-realworld-list-pool-rent-return` 从已知 30.3%/31.6% 区间降到 27.5%，`callback-chain` 从 98.8%/105.3% 过滤短跑降到 68.8%，`event-multicast` 从 83.8% 降到 65.8%。
- **根因**：新增 wrapper 扩大 M2N 间接层和代码体积，未减少真正主成本；10 次 business 口径下还放大首调用/代码布局噪声。
- **替代**：List/Stack 后续只做 transform 期 callsite/method kind 预烘焙，或更上层的 List 小循环 IR；每次必须用 `list-pool-rent-return` 过滤 + 完整 14+10 双验收。
- **状态**：withdrawn，提交已 revert。

### WA-011 — AOT 容器 `max_length` 直读 + M2N probe 清理（withdrawn）

- **尝试**：把 Dictionary/List/Stack fast path 中的 `Array::GetLength` 改为直接读 `Il2CppArray::max_length`，并移除 Dictionary/Nullable M2N probe。
- **实测**：Dictionary 85.7%→86.8% 仅小涨，但 List 30.3%→27.7%、Coroutine 31.7%→27.4% 回退。
- **根因**：这不是主要热成本，代码布局变化抵消/超过微优化收益。
- **替代**：保留已验证的 Dictionary offset cache；String key/hash/equality 与状态机/小方法才是下一步。
- **状态**：withdrawn，提交已 revert。

### WA-007 — RunI4LinearTrace v4（blocked）

- **尝试**：把任意 i4 栈片段（copy/ldc/binop/shr…）压成 `RunI4LinearTrace`，interpreter 内 for+switch 执行。
- **实测**：Tuanjie WebGL 构建通过；浏览器等 `HOTC233_PERFORMANCE_REPORT_JSON` **691s** 超时；前几轮正常 ~80–95s。
- **根因**：通用 trace 覆盖范围过大 → transform 膨胀 + interpreter 热循环 switch 分派 + 可能错误融合导致主线程极慢/卡死。
- **教训**：**Trace/fusion 必须窄形状 + 来自 typed IR lowering**；禁止“扫 IR 序列 → 通用 bytecode 解释器”。
- **替代**：Phase 1 typed register IR（i32 slot）→ Phase 2 lowering 到 quickened op / 保留 OK-005/006 窄 trace。
- **状态**：blocked，代码已撤回。

### WA-005 — mul→sub numeric fusion（withdrawn）

- **根因**：在栈机层做局部 numeric 合并，破坏其他方法的 copy 语义与 dispatch 平衡。
- **替代**：typed register IR 统一 i32 运算，后端再 peephole。

### WA-002 — 全局 copy propagation（withdrawn）

- **根因**：栈机 slot 生命周期分析不完整，错误转发导致 silent wrong-value 或更多 fixup 指令。
- **替代**：register IR 的 SSA/slot 合并只在 typed 层做。

---

## 架构 pivot 日志

| 日期 | 从 | 到 | 触发 |
|---|---|---|---|
| 2026-06-26 | benchmark opcode fusion 优先 | typed register IR 优先 | WA-005/007 + base 表 4–14x Pro 差距 |
| 2026-06-26 | 单一 WebGL 长等待 | L0–L5 分层迭代 + quick stale 检测 | RunI4LinearTrace hang |
| 2026-06-27 | dispatch/bridge 微优化 | GodDomain（14 base 专用 transform + bypass） | CallAOTStatic 错误 IR + WA-009 |

---

## 下一架构实验（允许）

| 实验 ID | 内容 | 前置 | 失败则 |
|---|---|---|---|
| EXP-001 | Typed register IR Phase 2：RegI32* + copy 消除 | L0 validate-reports | **landed 2026-06-26**，待 WebGL 回归 |
| EXP-002 | Typed ABI：`RunStaticI4CallTrace` + direct callsite cache | EXP-001 或独立 | **I4 trace landed**；direct cache 待补 |
| EXP-003 | Metadata size 三表（bytes/peak/load） | 商业探针全绿 | 调整 metadata policy |
| EXP-004 | 完全泛型共享矩阵扩探针（虚调用/接口/嵌套） | enableFullGenericSharing | 补 GenericSharing 矩阵 |

新增失败条目请追加到「错题索引」并更新 `pro-gate` 报告。
