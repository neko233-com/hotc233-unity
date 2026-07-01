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
