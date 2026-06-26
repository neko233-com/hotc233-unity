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
| 三个浏览器 URL 串行 CDP | 默认 `-parallel-captures` 并行 |
| 无「零 Unity」路径 | `-skip-unity` digest 全命中时只跑浏览器 |

PC 完整功能验证仍用 `build` / `all`（含 AB 链路），不与性能飞轮混用。

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
