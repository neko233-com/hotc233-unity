# hotc233 性能对标

更新时间：2026-07-01

## 目标

hotc233-unity 的生产目标是：**通用高性能热更解释器，商业可用，对标 HybridCLR 商业版相对 CE 的性能量级**。

当前日常验收只保留一组完整对照：

- hotc233: `unity-hotc233-benchmark`
- HybridCLR CE: `unity-hybridclr-ce-benchmark`
- 平台: StandaloneWindows64 IL2CPP Player
- Loader: `RuntimeFast`
- base: HybridCLR 官方 14 条 benchmark，每条固定 1000 次
- business: 10 条实际业务热更场景，每条固定 10 次

## Floor

HybridCLR 商业版相对 CE 的公开表格显示不同操作的收益不同；其中公开算术项约 5x，`typeof` 目标按 Pro 量级 10x。hotc233 本机生产 floor 采用分层规则：

```text
typeof: 1000%
HybridCLR 商业版公开算术项对应行: 500%
其它官方 base: 尽量全面 300%+
```

该 floor 只用来判断 hotc233 是否进入生产级性能区间；Pro 目标列仍作为更高阶参考。不要把 500% 错扩展到所有 benchmark。

## 唯一日常命令

在 `unity-hotc233-benchmark` 执行：

```powershell
go run ./tools/hotc233ctl local-benchmark -project . -loader-profile RuntimeFast `
  -hybridclr-project ..\unity-hybridclr-ce-benchmark -force-rebuild
```

`local-benchmark` 默认复用 `Assets/EditorForBuild/Generated/hybridclr-local-player-report.json`。HybridCLR CE 只需产出一次对照结果；只有缓存不存在，或显式设置：

```powershell
$env:HOTC233_REFRESH_HYBRIDCLR_CE='1'
```

才会重跑 CE。

## 结果

- 临时对比报告：`Assets/EditorForBuild/Generated/performance-local-hotc-vs-hybridclr-base.{json,md,html}`
- 人类可读归档：`benchmark-docs/性能报告.md`
- 机器可读归档：`benchmark-docs/results/latest-hotc-vs-hybridclr.json`

报告必须包含完整 14 条 base 和 10 条 business。缺行、改次数、缺 CE 同名行都视为 blocker。

机器可读报告每行必须包含 `floorPercent`、`floorScope`、`floorSource`、`floorStatus`。如果设置 `HOTC233_COMMUNITY_NEAR_PERCENT`，报告必须标注 `floorOverrideEnabled=true`，该结果只作为诊断覆盖口径。

## 开发顺序

1. 先保证商业能力：热更加载、热重载、加固、访问控制、Assembly 缓存、崩溃栈诊断。
2. 再做性能架构：专用 transform、whole-method bypass、typed ABI、register/array IR。
3. 最后做细节：peephole 和单 opcode 优化只在分层 floor 的主要路径清楚后推进。

禁止用增加循环次数、删减失败行、替换 benchmark 形状、预置大量热更逻辑进首包 AOT 的方式制造性能结论。
