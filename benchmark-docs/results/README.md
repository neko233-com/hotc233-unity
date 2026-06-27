# 归档实测结果

本目录存放 **可引用** 的性能对标快照。`Assets/EditorForBuild/Generated/` 中的文件是当次运行临时产物，可能失败或 stale。

## 最新文件（由 `hotc233ctl benchmark` 维护）

| 文件 | 内容 |
|---|---|
| `latest-hotc-vs-hybridclr.json` / `.md` | 同机 hotc233 vs HybridCLR 社区版 vs Pro 目标 |
| `latest-hybridclr-community-webgl.json` | HybridCLR 8.11.0 独立工程 WebGL 实测 |
| `latest-hotc233-webgl-base.json` | hotc233 仅 14 条官方 base |
| `runs/<UTC>/` | 单次完整历史快照 + `run-meta.json` |

## HybridCLR 社区版原始工程

同机数据来自：

`D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo`

该工程 README 与 `hybridclr-webgl` 命令共用同一 WebGL 构建与探针链路。

若 `latest-*.json` 缺失或时间戳早于解释器改动，必须重新运行：

```powershell
go run ./tools/hotc233ctl benchmark -project .. -loader-profile RuntimeFast -skip-unity -port 6511
```

（若 `6510` 被占用，换 `-port`；**禁止 multitask / 禁止 `-parallel-captures`**。）

**当前状态（2026-06-27）**：`latest-*` 已从 2026-06-26T09:43Z 最后一次成功对标种子导入，并标记 `runs/20260626T094317Z-stale-seed/`。Execute 对齐后的 fresh 探针今晚因浏览器探针挂起未完成——**不得用此表评判 Pro 路线**；需在本机顺序重跑 `benchmark` 覆盖。
