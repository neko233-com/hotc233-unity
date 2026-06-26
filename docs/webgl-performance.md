# WebGL 直接 dll 性能验证

更新时间：2026-06-26

## 当前验收口径

| 项目 | 口径 |
|------|------|
| 平台 | WebGL / minigame，本地浏览器 IL2CPP |
| 载入 | 直接读取产出的 `.dll.bytes` 与 RuntimeMetadata |
| 排除 | Unity 打包耗时、AssetLib233、YooAsset、HTTP 下载链路 |
| 性能目标 | 只以 HybridCLR Pro 纯解释天花板为竞品目标；本机 HybridCLR 8.11.0 仅作参考测量点 |
| 内存目标 | 热循环 0 GC，`game-memory-stability.retainedDelta <= 65536` bytes |

## 架构优先迭代循环

性能差距来自解释器架构，不是 Tuanjie 编译本身。必须按层推进，禁止用“更长 WebGL 等待”代替架构判断。

```text
L0 validate-reports + go test     → 秒级，opcode/报告表
L1 headless                       → 秒级，编译+只读 JSON 完整性
L2 quick                          → 秒级，只读 base JSON，架构桶排序
L3 webgl                          → ~80–95s 正常；唯一浏览器 IL2CPP 实测
L4 hybridclr-webgl                → 同机 HybridCLR base 对照
L5 perf / full verification       → base 改善后再跑业务与 Unity 特性
```

| 阶段 | 命令 |
|------|------|
| 改 transform/解释器后 | `go run ./tools/hotc233ctl validate-reports -project .` |
| 小步架构改动后 | `go run ./tools/hotc233ctl quick -project .` |
| 需要 IL2CPP 浏览器证据 | `go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast` |
| base 对照 | `go run ./tools/hotc233ctl hybridclr-webgl -project . -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo -loader-profile RuntimeFast -timeout 60m` |

`quick` 会检查架构源文件是否晚于所读 JSON，以及最近一次 `hotc233ctl-webgl-result.json` 是否失败；任一命中都不得把旧报告当有效结论。

trace/fusion 规则：只允许已实测有效的窄形状 lowering（`RunI4AddCopyTrace`、`RunStaticF4CallTrace`）。禁止通用 linear trace（`RunI4LinearTrace` 已撤回）。WebGL 浏览器 marker 超时 = 立即记失败并回滚，不继续等满 timeout。

## 命令

```powershell
go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
go run ./tools/hotc233ctl hybridclr-webgl -project . -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo -loader-profile RuntimeFast -timeout 60m
go run ./tools/hotc233ctl quick -project .
go run ./tools/hotc233ctl headless -project . -loader-profile RuntimeFast
go run ./tools/hotc233ctl validate-reports -project .
```

`quick` 只读最新 base JSON，不重新打 Unity/WebGL，用于每次架构小改后的快速判定。`webgl` 和 `hybridclr-webgl` 会生成：

| 文件 | 用途 |
|------|------|
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.md` | 人读主报告 |
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.html` | GitHub Pages / docs 展示 |
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.json` | 机器读取与上一次优化对比 |
| `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.md` | hotc233 与本机 HybridCLR 8.11.0 参考表、Pro 架构目标对比 |
| `Assets/EditorForBuild/Generated/quick-base-performance-gate.md` | base-only 快速门禁，按架构缺口排序 |
| `Assets/EditorForBuild/Generated/webgl-hotc233-player-report.json` | hotc233 原始性能与 GC/heap 数据 |
| `Assets/EditorForBuild/Generated/webgl-native-il2cpp-performance.json` | 原生 WebGL IL2CPP 对照 |
| `Assets/EditorForBuild/Generated/hotc233ctl-webgl-result.json` | 最近一次 webgl 命令成功/失败与耗时 |
| `Hotc233Data/AutomationLogs/*-webgl-browser-*.log` | 浏览器 CDP/marker 抓取日志（不进 Assets，避免 Unity 导入循环） |

## 表格规则

主表最左列固定是 hotc233。本次报告会读取上一份 WebGL JSON，按 `operation` 输出：

| 列 | 含义 |
|----|------|
| `hotc233` | 本轮 hotc233 WebGL 直接 dll 耗时 |
| `上一次 hotc233` | 上一次同项耗时 |
| `本次提升` | `(上一次 - 本次) / 上一次` |
| `HybridCLR 社区版` | 独立 Tuanjie 项目本机 WebGL 实测 |
| `HybridCLR Pro 目标` | 按 Pro 标准解释优化/typeof/callsite 已知收益估算的架构目标 |

同一类优化如果连续 2 次 WebGL 实测没有接近 Pro 架构目标，或主要 WebGL 指标倒退，就停止该路线，转向更高杠杆的 typed register IR、direct AOT callsite、metadata token cache 或低 GC 内存布局优化。

## 当前有效快照

最新**有效** WebGL RuntimeFast 报告：`2026-06-26T12:40:59Z`（`RunI4LinearTrace` v4 失败轮次作废，不得引用其 JSON）。

| hotc233 operation | 本轮 | 相对上轮 | hotc / IL2CPP | Pro floor |
|---|---:|---:|---:|---|
| 闭包委托 | 22.10 ms / 4,524,866 ops/s | -0.9% | 8.6% | 达到 |
| 小方法调用 | 72.80 ms / 13,736,264 ops/s | +9.3% | 4.8% | 差 1.6x |
| 泛型实例化 | 28.20 ms / 3,546,099 ops/s | +1.8% | 6.7% | 差 1.1x |
| SetTransformPosition | 248.30 ms / 402,739 ops/s | -1.8% | 6.6% | 差 1.2x |
| CallAOTStaticMethod | 124.70 ms / 801,925 ops/s | +3.8% | 1.9% | 差 3.9x |
| BinOpAdd 简单数值 | 308.30 ms / 3,243,595 ops/s | +15.9% | 0.7% | 差 11.2x |
| 任务委托派发 | 148.00 ms / 67,568 ops/s | +25.4% | 0.8% | 差 9.4x |

保留优化：闭包方法 fastpath、单播解释 delegate inline cache、`CallInterpStatic_ret` callee IMI cache、`RunI4AddCopyTrace`、`RunStaticF4CallTrace`。已回滚优化：fastpath 方法 class-init 状态位、static 专用 opcode、全局 copy propagation、单条 numeric fusion、通用 `RunI4LinearTrace`。

## 自动化失败记录

| 时间 | 尝试 | 结果 | 决策 |
|---|---|---|---|
| 2026-06-26 | 将 Tuanjie/browser live log 写入 `Assets/EditorForBuild/Generated/logs/` | Unity 导入正在增长的 `.log`，触发 SourceAssetDB modification time 循环，WebGL AssetBundle build failed | 撤回；live log 固定移到 `Hotc233Data/AutomationLogs/`，`Assets/EditorForBuild/Generated/*.json` 只做运行前置空和最终结果文件 |
| 2026-06-26 13:40–13:51 UTC | `RunI4LinearTrace` v4 通用 i4 linear trace | Tuanjie WebGL 构建通过；浏览器 CDP 等 `HOTC233_PERFORMANCE_REPORT_JSON` 691s 超时（`hotc233ctl-webgl-result.json` status=failed）；前几轮正常约 80–95s | 撤回 `RunI4LinearTrace`；只保留 `RunI4AddCopyTrace` 与 `RunStaticF4CallTrace`；下一轮以 L0→L2→L3 重跑验证 |
