# hotc233 性能对标

更新时间：2026-06-27

本目录只归档可复用的性能结论。宿主工程 `Assets/EditorForBuild/Generated/` 是临时输出，不直接当长期结论。

## 新版压测循环

1. **先基线**：刷新同机 HybridCLR 社区版 14 条 base；没有社区版实测，不评价 hotc。
2. **先正确性**：hotc 架构改动先跑 `HOTC233_LOCAL_OFFICIAL_COUNT=1`，只看是否完成、崩溃、超时。
3. **再正式表**：最小验收过后跑默认 `local-benchmark` 167 口径；只用这份表判断性能方向。
4. **先架构后 opcode**：弱项先归因到 typed IR、call bridge、metadata/cache、Unity API 边界；opcode 微优化放最后。
5. **两次止损**：同一路线连续两次无收益或回退，停止扩大，写一句结论后换架构假设。

## 日常命令

在宿主 demo 的 `tools` 目录执行：

```powershell
# 最小正确性闸
$env:HOTC233_LOCAL_OFFICIAL_COUNT='1'
go run ./hotc233ctl local-benchmark -project .. `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo `
  -loader-profile RuntimeFast
Remove-Item Env:\HOTC233_LOCAL_OFFICIAL_COUNT -ErrorAction SilentlyContinue

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
