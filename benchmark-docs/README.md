# hotc233 性能对标

更新时间：2026-06-27

**先商业、后性能。** 日常 CI 默认只卡商业能力；性能对标在本节，架构全绿后再硬卡 L1。

## 落地顺序

1. **P0 商业**：`go run ./hotc233ctl validate-reports -project ..`（默认，不卡性能表）
2. **P1 架构**：callsite → typed ABI → register/array IR；`local-benchmark` 看方向
3. **P2 细节**：仅 P1 桶 L1 全绿后才做 peephole/单 opcode

**200% 规则**：任一条 base `hotc233PercentOfHybridClr < 50%` → 架构方向错误，换桶，禁止细节优化。

**Dominance 规则（Pro 量级）**：相对社区版默认目标 **200%**；typeof **1000%**；CallAOTStatic **500%**。见 `benchmark-docs/arch-self-review.md` 与 `docs/pro-landing-matrix.json` → `communityDominanceTargetsPercent`。

**Pro 机制对照（实现原理 + 落地状态）**：[`benchmark-docs/pro-mechanism-landing.md`](pro-mechanism-landing.md)

**每次对标后必报全量表 + profiler 声明**：[`benchmark-docs/reporting-requirements.md`](reporting-requirements.md)

**GodDomain（唯一 P1 主路径，2026-06-27 起）**：[`benchmark-docs/god-domain-architecture.md`](god-domain-architecture.md)

## 新版压测循环

1. **先基线**：刷新同机 HybridCLR 社区版 14 条 base；没有社区版实测，不评价 hotc。
2. **先正确性**：hotc 架构改动先跑 `HOTC233_LOCAL_OFFICIAL_COUNT=1`，只看是否完成、崩溃、超时。
3. **再正式表**：最小验收过后跑默认 `local-benchmark` 167 口径；只用这份表判断性能方向。
4. **先专用后 opcode**：弱项只按 **专用 transform → whole-method bypass → typed ABI**；**禁止**通用 dispatch / M2N 桥接 / Execute switch 微优化（见 `god-domain-architecture.md`）。
5. **两次止损**：同一路线连续两次无收益或回退，写入 `pro-wrong-answer-notebook.md` 并换假设；禁止把废弃路线当“可选优化”重新默认开启。

## P0 商业全绿（进入 P1 前必跑）

```powershell
# 1. 生成 + AB 验证（写入 metadata-optimization-report + verification-report）
go run ./hotc233ctl build -project ..

# 2. WebGL 平台矩阵（写入 commercialP0Passed / fullGenericSharing）
go run ./hotc233ctl matrix -project .. -targets WebGL

# 3. P0 门禁（默认不卡性能）
go run ./hotc233ctl validate-reports -project ..
```

P0 全绿后才开始 P1 架构性能（`local-benchmark` + `HOTC233_ENFORCE_PERFORMANCE=1`）。

## 日常命令

在宿主 demo 的 `tools/hotc233ctl` 目录执行：

```powershell
# 全自动架构自审（商业 P0 + 14 条 base + Dominance 队列）
go run . arch-self-review -project .. -loader-profile RuntimeFast `
  -hybridclr-project ..\..\Benchmarks\hybridclr-benchmark-demo

# P0 商业门禁（默认）
go run . validate-reports -project ..

# P1 性能门禁（架构迭代显式开启）
$env:HOTC233_ENFORCE_PERFORMANCE='1'
go run ./hotc233ctl validate-reports -project ..
Remove-Item Env:\HOTC233_ENFORCE_PERFORMANCE -ErrorAction SilentlyContinue
```

性能对标命令：

```powershell
# 最小正确性闸
$env:HOTC233_LOCAL_OFFICIAL_COUNT='1'
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project ..\..\Benchmarks\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -force-rebuild
Remove-Item Env:\HOTC233_LOCAL_OFFICIAL_COUNT -ErrorAction SilentlyContinue

# M2N 弱项快速迭代（只跑 3 条 + 跳过 HybridCLR 重建 + 跳过 AB 全验证）
$env:HOTC233_LOCAL_OFFICIAL_COUNT='1'
$env:HOTC233_LOCAL_BENCHMARK_FILTER='quaternion-op,call-aot-static,set-transform-position'
$env:HOTC233_LOCAL_PERF_FAST='1'
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project ..\..\Benchmarks\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -skip-hybridclr -force-rebuild
Remove-Item Env:\HOTC233_LOCAL_OFFICIAL_COUNT,Env:\HOTC233_LOCAL_BENCHMARK_FILTER,Env:\HOTC233_LOCAL_PERF_FAST -ErrorAction SilentlyContinue

# 正式同机对标（默认含 10 条 business-realworld-* + 自动生成 性能报告.md）
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project ..\..\Benchmarks\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -force-rebuild

# 仅官方 14 条 base（不含业务场景）
$env:HOTC233_INCLUDE_BUSINESS_BENCHMARK='0'
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project ..\..\Benchmarks\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -force-rebuild
Remove-Item Env:\HOTC233_INCLUDE_BUSINESS_BENCHMARK -ErrorAction SilentlyContinue

# xLua 未安装时先执行
powershell -ExecutionPolicy Bypass -File ..\tools\setup-xlua.ps1

# Unity 真实热更快测（独立于官方 14 base；10~100 次推演 1s 吞吐）
go run ./hotc233ctl unity-realworld-benchmark -project .. -loader-profile RuntimeFast
# 详见 benchmark-docs/real-world-unity-hotupdate-suite.md
```

WebGL/小游戏只在需要平台结论时顺序跑：

```powershell
go run ./hotc233ctl benchmark -project .. `
  -hybridclr-project ..\..\Benchmarks\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -force-rebuild
```

## 判定口径

- **L1 硬门禁**：只认 HybridCLR 官方 14 条 `hybridclr-*` base；任一 base 行 `hotc233PercentOfHybridClr <= 100%`，方向不合格。
- **业务拉伸（可选）**：10 条 `business-realworld-*`（custom class / struct / async / Task / callback / coroutine / tween 模拟等）；`HOTC233_ENFORCE_BUSINESS_FLOOR=500` 启用 500% 拉伸门禁。
- **业务代码对社区版**：必须同名、同迭代、同平台、同 Player 对比；禁止增大迭代数制造稳定读数；缺 HybridCLR 社区版同名 business 行时不得宣称商业版业务性能落地。
- Pro 目标是纯解释器性能，不用 DHE 或首包 AOT 预置热更代码替代。
- 禁止 Cursor multitask、并行 WebGL 探针、旧 `flywheel` 结论。

## 报告位置

- **人类可读唯一入口（自动生成）**：[`benchmark-docs/性能报告.md`](性能报告.md) — 官方 14 行 + 业务场景双表 + 自动摘要 + L1 结论
- 临时：`Assets/EditorForBuild/Generated/performance-local-hotc-vs-hybridclr-base.*`
- 机器可读归档：`benchmark-docs/results/latest-hotc-vs-hybridclr.json`
- 归档 Markdown 副本：`benchmark-docs/results/latest-hotc-vs-hybridclr.md`

`local-benchmark` 成功后 `hotc233ctl` 会自动同步 `性能报告.md` 与 `results/latest-*`。

## 文档规则

- 文档只写口径、命令、当前结论、下一步假设。
- 不粘贴整份旧表；旧数据放 `results/`。
- 失败路线只写“失败原因 + 后续动作”。
- **每次跑完 benchmark 必须按 [`reporting-requirements.md`](reporting-requirements.md) 更新 [`性能报告.md`](性能报告.md) 全量 14 行表**，并声明 opcode profiler 开/关。
