# hotc233 文档索引

更新时间：2026-06-26

| 文档 | 用途 |
|---|---|
| **[performance-peak-plan.md](performance-peak-plan.md)** | 北极星目标、架构四阶段、**当前快照与下一步**、错题摘要 |
| **[flywheel-automation.md](flywheel-automation.md)** | 命令、digest 增量、**报告产出清单**、耗时统计 |
| **[platforms.md](platforms.md)** | 平台矩阵与 minigame 健康检查 |
| **[pro-landing-matrix.json](pro-landing-matrix.json)** | Pro 商业能力落地矩阵（机器可读） |

## 性能报告在哪里

宿主 demo 每次 WebGL 探针后自动生成（`go run ./tools/hotc233ctl webgl` 或 `flywheel`）：

| 文件 | 内容 |
|---|---|
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.md` | **全量表**：hotc233 ms / ops/s / ×社区版 \| 社区版 \| 专业版目标 \| IL2CPP |
| `Assets/EditorForBuild/Generated/performance-webgl-base-il2cpp.md` | 仅 HybridCLR 官方 base 基准（硬验收） |
| `Assets/EditorForBuild/Generated/hybridclr-webgl-player-report.json` | HybridCLR 社区版实测（来自 `hybridclr-benchmark-demo`） |
| `Assets/EditorForBuild/Generated/report-links.md` | 全部报告路径索引 |
| `Hotc233Data/flywheel-cache.json` | 飞轮耗时与上次 Unity 重建原因 |

**社区版对照**必须先有社区版报告：

```powershell
go run ./tools/hotc233ctl hybridclr-webgl -project . -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo -loader-profile RuntimeFast
# digest 命中后仅浏览器 ~20s：
go run ./tools/hotc233ctl hybridclr-webgl -project . -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo -skip-unity
```

## 日常命令

```powershell
go run ./tools/hotc233ctl validate-reports -project .
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'
go run ./tools/hotc233ctl flywheel -project . -loader-profile RuntimeFast
```

Native 解释器改动后：`flywheel -force-rebuild`。仅重跑浏览器：`webgl -skip-unity`。

## 已归档（内容已并入上文）

以下文件仅保留跳转，勿再单独维护：

- `webgl-performance.md` → 并入 `performance-peak-plan.md` + `flywheel-automation.md`
- `hybridclr-gap-analysis.md` → 并入 `performance-peak-plan.md` 快照节
- `hybridclr-pro-landing-roadmap.md` → 门禁摘要 + 详表见 `performance-peak-plan.md` Stage D
- `pro-wrong-answer-notebook.md` → 门禁摘要 + 详表见 `performance-peak-plan.md` 错题表
- `runtime-rewrite-plan.md` → 并入 Stage A/B/C
- `ecosystem.md` → 见包根 `README.md`
