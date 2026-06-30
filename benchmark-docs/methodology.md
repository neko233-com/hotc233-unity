# 性能测试方法

更新时间：2026-07-01

## 固定工程

| 角色 | 工程 | 说明 |
|---|---|---|
| hotc233 | `unity-hotc233-benchmark` | 宿主测试工程，引用 `hotc233-unity` 子仓库 |
| HybridCLR CE | `unity-hybridclr-ce-benchmark` | 社区版独立测试工程，只产出 CE 对照数据 |

两个工程必须使用同一套 benchmark 形状。hotc233 工程不安装官方 HybridCLR 包。

## 运行策略

1. CE 结果只需产出一次，写入 hotc233 工程的 `Assets/EditorForBuild/Generated/hybridclr-local-player-report.json`。
2. 后续 hotc233 迭代默认复用 CE 结果，避免每轮重复构建 CE。
3. 需要刷新 CE 时设置 `HOTC233_REFRESH_HYBRIDCLR_CE=1`。
4. 每次 hotc233 性能判断只看一组完整对照：14 条 base + 10 条 business。

## 固定次数

| 分组 | 数量 | 每项循环 |
|---|---:|---:|
| HybridCLR 官方 base | 14 | 1000 |
| 实际业务 business | 10 | 10 |

解释器无 JIT 加成，增加循环次数不会改变真实性能形态，只会拖慢验证。因此次数不允许被业务侧或环境变量修改。

## 命令

```powershell
go run ./tools/hotc233ctl local-benchmark -project . -loader-profile RuntimeFast `
  -hybridclr-project ..\unity-hybridclr-ce-benchmark -force-rebuild
```

只刷新 CE：

```powershell
$env:HOTC233_REFRESH_HYBRIDCLR_CE='1'
go run ./tools/hotc233ctl local-benchmark -project . -loader-profile RuntimeFast `
  -hybridclr-project ..\unity-hybridclr-ce-benchmark -force-rebuild
Remove-Item Env:\HOTC233_REFRESH_HYBRIDCLR_CE -ErrorAction SilentlyContinue
```

## 判定

- 生产 floor 分层：`typeof=1000%`，HybridCLR 商业版公开算术项对应行 `500%`，其它官方 base 默认 `300%`。
- 任一 base 行低于自身分层 floor 即未达到当前生产性能目标。
- JSON 每行必须输出 `floorPercent`、`floorScope`、`floorSource`、`floorStatus`；Markdown 表格必须展示 floor 与 status。
- `HOTC233_COMMUNITY_NEAR_PERCENT` 只允许作为诊断覆盖，报告必须显式标注 override，不代表默认生产 floor。
- business 行只作为实际业务风险观察；当启用 business floor 时必须同名、同次数、同平台比较。
- WebGL/小游戏专项不替代本机日常 floor。
