# hotc233 性能对标

更新时间：2026-06-27

**先商业、后性能。** 日常 CI 默认只卡商业能力；性能对标在本节，架构全绿后再硬卡 L1。

## 落地顺序

1. **P0 商业**：`go run ./hotc233ctl validate-reports -project ..`（默认，不卡性能表）
2. **P1 架构**：callsite → typed ABI → register/array IR；`local-benchmark` 看方向
3. **P2 细节**：仅 P1 桶 L1 全绿后才做 peephole/单 opcode

**200% 规则**：任一条 base `hotc233PercentOfHybridClr < 50%` → 架构方向错误，换桶，禁止细节优化。

**Dominance 规则（Pro 量级）**：相对社区版默认目标 **200%**；typeof **1000%**；CallAOTStatic **500%**。见 `benchmark-docs/arch-self-review.md` 与 `docs/pro-landing-matrix.json` → `communityDominanceTargetsPercent`。

## 新版压测循环

1. **先基线**：刷新同机 HybridCLR 社区版 14 条 base；没有社区版实测，不评价 hotc。
2. **先正确性**：hotc 架构改动先跑 `HOTC233_LOCAL_OFFICIAL_COUNT=1`，只看是否完成、崩溃、超时。
3. **再正式表**：最小验收过后跑默认 `local-benchmark` 167 口径；只用这份表判断性能方向。
4. **先架构后 opcode**：弱项只按 P1 callsite → P2 typed ABI → P3 register/array；**禁止**实验 transform 或 profile 策略分支。
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
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo

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
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast
Remove-Item Env:\HOTC233_LOCAL_OFFICIAL_COUNT -ErrorAction SilentlyContinue

# M2N 弱项快速迭代（只跑 3 条 + 跳过 HybridCLR 重建 + 跳过 AB 全验证）
$env:HOTC233_LOCAL_OFFICIAL_COUNT='1'
$env:HOTC233_LOCAL_BENCHMARK_FILTER='quaternion-op,call-aot-static,set-transform-position'
$env:HOTC233_LOCAL_PERF_FAST='1'
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -skip-hybridclr -force-rebuild
Remove-Item Env:\HOTC233_LOCAL_OFFICIAL_COUNT,Env:\HOTC233_LOCAL_BENCHMARK_FILTER,Env:\HOTC233_LOCAL_PERF_FAST -ErrorAction SilentlyContinue

# 正式同机对标
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast
```

WebGL/小游戏只在需要平台结论时顺序跑：

```powershell
go run ./hotc233ctl benchmark -project .. `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast -skip-unity
```

## 判定口径

- 只认 HybridCLR 官方 14 条 base benchmark。
- 社区版是绝对门槛：任一 base 行 `hotc233PercentOfHybridClr <= 100%`，方向不合格。
- Pro 目标是纯解释器性能，不用 DHE 或首包 AOT 预置热更代码替代。
- 禁止 Cursor multitask、并行 WebGL 探针、旧 `flywheel` 结论。

## 报告位置

- 临时：`Assets/EditorForBuild/Generated/performance-local-hotc-vs-hybridclr-base.*`
- 社区版临时：`Assets/EditorForBuild/Generated/hybridclr-local-player-report.json`
- hotc 临时：`Assets/EditorForBuild/Generated/performance-hotc233-player.json`
- 权威归档：`benchmark-docs/results/latest-hotc-vs-hybridclr.*`

## 文档规则

- 文档只写口径、命令、当前结论、下一步假设。
- 不粘贴整份旧表；旧数据放 `results/`。
- 失败路线只写“失败原因 + 后续动作”。
