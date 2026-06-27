# 同机环境记录

更新时间：2026-06-27（请在每次正式对标后更新）

## 参考机器

| 项 | 值 |
|---|---|
| 主机 | Windows 10/11 x64（本机开发机） |
| Tuanjie | 2022.3.62t10 |
| HybridCLR 社区版包 | 8.11.0（`hybridclr-benchmark-demo`） |
| hotc233 loader | RuntimeFast |
| 浏览器 | Chrome/Edge headless（`HOTC233_BROWSER_PATH` 未设则用系统默认） |

## 工程路径

| 工程 | 路径 |
|---|---|
| hotc233 demo | `D:\Code\neko233-Projects\unity-hotc233-demo` |
| HybridCLR benchmark | `D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo` |

## 记录规范

每次 `hotc233ctl benchmark` 归档时，在 `results/runs/<timestamp>/run-meta.json` 中写入：

- `generatedAtUtc`
- `unityVersion`（来自报告 JSON）
- `browserPath`（若可检测）
- `hybridClrBenchmarkProjectRoot`
- `hotc233ProjectRoot`
- `skipUnity` / `forceRebuild` 标志

便于复现「同一台机器」对比。
