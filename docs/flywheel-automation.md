# hotc233 优化飞轮自动化（榨干本机性能）

更新时间：2026-06-26  
总纲：[`performance-peak-plan.md`](performance-peak-plan.md)

---

## 目标

把 **提出思路 → 改代码 → 编译 → 验证** 循环压到最短，且产出稳定：

| 层级 | 典型耗时 | 命令 |
|---|---:|---|
| L0 门禁 | **1–3s** | `validate-reports` + `pro-gate` + `quick`（无 Unity） |
| L1 热更编译 | **15–40s** | `compile`（仅 CompileDll + payload） |
| L2 WebGL 增量 | **45–55s** | `webgl` / `flywheel`（digest 命中：0 次或 1 次 Unity） |
| L3 WebGL 原生壳 | **60–90s** | native libil2cpp 变更 → 单次 `CI_FlywheelWebGLPrepare` |
| L4 全量 | **按需** | `-force-rebuild` |

---

## 主命令：`flywheel`

```powershell
# 日常迭代（推荐）：L0 并行门禁 + 增量 WebGL
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'
go run ./tools/hotc233ctl flywheel -project . -loader-profile RuntimeFast

# 只跑门禁，不打 Unity/WebGL
go run ./tools/hotc233ctl flywheel -project . -verify-only

# 只改了热更 C#（无 native 解释器改动）
go run ./tools/hotc233ctl compile -project .

# 改了 Data~/Libil2cpp 解释器 → 必须重建 WebGL 壳
go run ./tools/hotc233ctl flywheel -project . -force-rebuild

# 仅重跑浏览器探针（shell/payload digest 均未变）
go run ./tools/hotc233ctl webgl -project . -skip-unity

# 并行 CDP 探针（默认开启）
go run ./tools/hotc233ctl webgl -project . -parallel-captures=true
```

**WebGL 探针口径（不用 Playwright / CDP 轮询）**

1. Tuanjie 构建 WebGL IL2CPP Player（`BuildWebGLHotcPerformanceShell`）
2. headless Chrome/Edge 仅负责加载 WASM（`HOTC233_BROWSER_PATH` 可覆盖）
3. `Hotc233PlayerAbVerifier` + `Hotc233WebGLReport.jslib` 分块输出 JSON
4. 页面内 `__hotc233_probe__/bridge.js` 组装后 **HTTP POST** 到本地静态服
5. `hotc233ctl` 在 Go 侧等待 marker，**不在 benchmark 热路径上注入 Runtime.evaluate**

PC 性能仍走 `player` / `perf`（Standalone Windows IL2CPP Player，无浏览器）。

---

## 增量策略（digest 缓存）

| 变更范围 | 检测根 | Unity 动作 |
|---|---|---|
| `Assets/neko233/.../hotc233` 解释器/native | WebGL shell stamp | `BuildWebGLHotcPerformanceShell`（**无 AB 全量验证**） |
| `Assets/CodeHotUpdate` 热更代码 | `Hotc233Data/.hotc233-webgl-shell-stamp.json` (payload) | `PublishDirectWebGLPayload` |
| native 对照 Player 源 | native shell stamp | `BuildNativePerformancePlayer` |
| 无变更 | — | **跳过 Unity**，仅 sync payload + 浏览器探针 |

**单次 Tuanjie 启动**：`CI_FlywheelWebGLPrepare` 在一个 batchmode 会话内完成 hotc 壳 / payload / native 壳的组合任务，不再 hotc + native 各启一次编辑器。

---

## 与旧路径的差异

| 旧行为 | 新行为 |
|---|---|
| `CI_BuildPlayerProbe` 先跑完整 AB/YooAsset 验证 | WebGL 性能路径改用 `BuildWebGLHotcPerformanceShell` |
| hotc WebGL 与 native WebGL 两次 `-batchmode` | 合并为 `CI_FlywheelWebGLPrepare` |
| 三个浏览器 URL 串行 CDP | 默认 `-parallel-captures` 并行 **Unity jslib HTTP POST** |
| Node.js CDP `Runtime.evaluate` 轮询 | **已移除**；主线程跑 benchmark 时不再被 CDP 打断 |
| 无「零 Unity」路径 | `-skip-unity` digest 全命中时只跑 WebGL 探针 |

PC 完整功能验证仍用 `build` / `all`（含 AB 链路），不与性能飞轮混用。

---

## 产出与自动化统计

每次 `webgl` / `flywheel` 成功后会写入：

| 产出 | 统计字段 |
|---|---|
| `performance-webgl-local-il2cpp.json` | 每行 `hotc233ElapsedMs`、`hotc233OpsPerSecond`、`hotc233RatioToHybridClrCommunity` |
| 同上 `.md` | 列：**hotc233 (ms / ops/s / ×社区版)** \| **HybridCLR 社区版** \| **专业版目标** \| **WebGL IL2CPP** |
| `performance-webgl-base-il2cpp.json` | 仅 `benchmarkGroup=base` 官方基准 |
| `hybridclr-webgl-player-report.json` | 社区版实测（`hybridclr-webgl` 命令） |
| `hotc233ctl-*-result.json` | 命令 success、elapsedMs、failurePhase |
| `Hotc233Data/flywheel-cache.json` | 上次 L0/L3 耗时、digest 命中层 |
| `webgl-hotc233-opcode-profile.json` | 热方法 opcode 分布（诊断 trace 命中） |

**社区版列**：当 `hybridclr-webgl-player-report.json` 存在且 benchmark 名称匹配时，`hotc233ctl` 自动合并社区 ms/ops/s 并计算 ×社区版；否则显示 `-`（需先跑 `hybridclr-webgl`）。

**hybridclr-benchmark-demo 飞轮**：`hybridclr-webgl -skip-unity` 在 digest 命中时 **~20s** 仅浏览器；stamp 位于 `HybridCLRData/.hybridclr-benchmark-webgl-stamp.json`。

---

## 产出与稳定性

- 命令结果：`Assets/EditorForBuild/Generated/hotc233ctl-<command>-result.json`
- 飞轮缓存：`Hotc233Data/flywheel-cache.json`（耗时、上次 Unity 重建原因）
- WebGL shell stamp：`Build/WebGLPerformance/*/.hotc233-webgl-shell-stamp.json`
- 日志：`Hotc233Data/AutomationLogs/`（不进 Assets，避免 Unity 导入循环）

---

## 推荐日常循环

```powershell
# 1. 改 native / transform / 热更 C#
# 2. 秒级门禁
go run ./tools/hotc233ctl flywheel -project . -verify-only
# 3. 一条命令编译+WebGL 实测
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'
go run ./tools/hotc233ctl flywheel -project . -loader-profile RuntimeFast
# 4. 读报告
# Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.json
# Assets/EditorForBuild/Generated/pro-landing-gate.md
```

native 解释器改动后若 WebGL 壳未自动失效，使用 `-force-rebuild` 一次即可恢复确定性。
