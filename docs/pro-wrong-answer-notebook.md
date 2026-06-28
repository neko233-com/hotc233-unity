# hotc233 Pro 错题本

更新时间：2026-06-26  
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
