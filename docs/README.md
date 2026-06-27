# hotc233 文档索引

更新时间：2026-06-27

## 性能对标（唯一入口）

**所有性能测试文档、同机实测、验收口径 → [`../benchmark-docs/`](../benchmark-docs/)**

```powershell
go run ./tools/hotc233ctl benchmark -project . -loader-profile RuntimeFast `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo
```

## 非性能文档

| 文档 | 用途 |
|---|---|
| [platforms.md](platforms.md) | 平台矩阵与 minigame 健康检查 |
| [pro-landing-matrix.json](pro-landing-matrix.json) | Pro 商业能力落地矩阵（机器可读） |
| [pro-wrong-answer-notebook.md](pro-wrong-answer-notebook.md) | 优化错题本（Pro-gate 引用） |

## 已废弃（勿维护）

| 旧文件 | 替代 |
|---|---|
| `flywheel-automation.md` | `benchmark-docs/methodology.md` |
| `webgl-performance.md` | `benchmark-docs/methodology.md` |
| `performance-peak-plan.md` | `benchmark-docs/comparison-policy.md` |
| `hybridclr-gap-analysis.md` | `benchmark-docs/gap-analysis.md` |
| `hybridclr-pro-landing-roadmap.md` | `comparison-policy.md` + `pro-landing-matrix.json` |
| `runtime-rewrite-plan.md` | `gap-analysis.md` |

功能验证仍用宿主 demo `build` / `all`；**性能验收只用 `benchmark`**。
