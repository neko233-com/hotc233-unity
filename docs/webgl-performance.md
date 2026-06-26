# WebGL 直接 dll 性能验证

更新时间：2026-06-26

## 当前验收口径

| 项目 | 口径 |
|------|------|
| 平台 | WebGL / minigame，本地浏览器 IL2CPP |
| 载入 | 直接读取产出的 `.dll.bytes` 与 RuntimeMetadata |
| 排除 | Unity 打包耗时、AssetLib233、YooAsset、HTTP 下载链路 |
| 性能目标 | hotc233 下限 = HybridCLR Pro 纯解释上限，即 76.9% native IL2CPP |
| 内存目标 | 热循环 0 GC，`game-memory-stability.retainedDelta <= 65536` bytes |

## 命令

```powershell
go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
go run ./tools/hotc233ctl headless -project . -loader-profile RuntimeFast
go run ./tools/hotc233ctl validate-reports -project .
```

`webgl` 会生成：

| 文件 | 用途 |
|------|------|
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.md` | 人读主报告 |
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.html` | GitHub Pages / docs 展示 |
| `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.json` | 机器读取与上一次优化对比 |
| `Assets/EditorForBuild/Generated/webgl-hotc233-player-report.json` | hotc233 原始性能与 GC/heap 数据 |
| `Assets/EditorForBuild/Generated/webgl-native-il2cpp-performance.json` | 原生 WebGL IL2CPP 对照 |

## 表格规则

主表最左列固定是 hotc233。本次报告会读取上一份 WebGL JSON，按 `operation` 输出：

| 列 | 含义 |
|----|------|
| `hotc233` | 本轮 hotc233 WebGL 直接 dll 耗时 |
| `上一次 hotc233` | 上一次同项耗时 |
| `本次提升` | `(上一次 - 本次) / 上一次` |
| `HybridCLR 社区版上限` | 官方倍率反推参考，非同机实测 |
| `HybridCLR Pro 上限` | hotc233 当前达标线 |

同一类优化如果连续 2 次 WebGL 实测没有接近 Pro 上限，或主要 WebGL 指标倒退，就停止该路线，转向更高杠杆的解释器直执行、AOT stub、SSA 或内存布局优化。
