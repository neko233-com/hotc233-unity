# 性能测试方法（对齐 HybridCLR 社区版）

更新时间：2026-06-27

## 固定对标工程

| 角色 | 路径 | 说明 |
|---|---|---|
| hotc233 宿主 | `unity-hotc233-demo` | 内置 hotc233 运行时，不安装 `com.code-philosophy.hybridclr` |
| HybridCLR 社区版 | `Hotc233Data/local-machine.json` → `hybridClrBenchmarkProjectRoot` | 本机实测；默认 `D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo` |

两个工程使用 **同一套** `OfficialBenchmarkProbe` 代码形状（14 条 HybridCLR 官方 performance benchmark）。

## 解释器对标口径（本机轻量）

- **只跑 14 条官方 base**（`RunOfficialBaseOnly` / `OfficialBenchmarkProbe`），不跑 business 探针。
- **无 warmup**：纯解释器无 JIT，预热只浪费时间且污染对比。
- **迭代档位**（hotc233 与 hybridclr-benchmark-demo 一致）：
  - `officialTestCount = 167`（相对旧 ~60s 档位同比例缩到 ~10s）
  - large = ×10 → 1 670
  - medium = ×2 → 334
  - GameObject = 84
- **默认平台**：StandaloneWindows64 IL2CPP Player，同机顺序运行 hotc233 与 HybridCLR 社区版。
- **不启动**：WebGL build、本地 server、headless browser。

## 快速验证周期

1. hotc233：`CI_RunHotc233LocalPerformanceOnly` 构建一个 Windows IL2CPP 热更 Player，写 `performance-hotc233-player.json`。
2. HybridCLR：`CI_RunLocalBenchmark` 构建一个 Windows IL2CPP Player，写 `hybridclr-local-player-report.json`。
3. Go 侧只聚合 JSON，写 `performance-local-hotc-vs-hybridclr-base.{json,md,html}`。
4. native `Data~/Libil2cpp/.../hotc233/` 变更后重跑 `local-benchmark`；需要 WebGL/小游戏平台结论时才跑 WebGL 专项。

## 优化重心（native / il2cpp_hotc233）

```text
Assets/neko233/hotc233-unity/Data~/Libil2cpp/2022-tuanjie/hotc233/
  interpreter/Interpreter_Execute.cpp
  transform/TransformContext.cpp
  transform/Hotc233TransformPolicy.h
```

## 测试环境（必须与 HybridCLR 一致）

- **默认平台**：StandaloneWindows64 + IL2CPP（两边独立工程，本机 Player）
- **WebGL 专项**：WebGL + IL2CPP + headless Chrome，只在需要小游戏/WebGL 平台结论时运行
- **Loader**：`RuntimeFast`（强制）
- **HybridCLR 数据**：本机 `hybridclr-benchmark-demo` 实测，**不用**官方文档冒充社区版实测
- **Pro 列**：仅参考官方文档相对社区版的倍率区间，不作为本机实测
- **禁止**：Mono 表冒充 IL2CPP；并行探针；demo 内模拟 HybridCLR 数值

## 命令

```powershell
go run ./tools/hotc233ctl local-benchmark -project . -loader-profile RuntimeFast
```

路径自动读 `Hotc233Data/local-machine.json`；也可 `-hybridclr-project ...`。

需要 WebGL/小游戏平台专项结论时：

```powershell
go run ./tools/hotc233ctl benchmark -project . -force-rebuild -loader-profile RuntimeFast
```

## 结论位置

**日常临时报告**：`Assets/EditorForBuild/Generated/performance-local-hotc-vs-hybridclr-base.{md,json,html}`

**WebGL 专项归档**：`benchmark-docs/results/latest-hotc-vs-hybridclr.{md,json}`

`Assets/EditorForBuild/Generated/` 仅为临时产物，不得作为验收依据。
