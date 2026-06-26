# WebGL / Tuanjie 巅峰性能验证

更新时间：2026-06-26  
总纲：[`performance-peak-plan.md`](performance-peak-plan.md)

---

## 验收口径

| 项目 | 要求 |
|---|---|
| 引擎 | **Tuanjie**（Unity 2022+ 兼容线） |
| 平台 | WebGL / 微信小游戏 minigame(WebGL2) |
| Loader | **`RuntimeFast` 强制** |
| 载荷 | `StreamingAssets/Hotc233Probe/Payload` 原始 `.dll.bytes` + RuntimeMetadata |
| 排除 | 打包耗时、AssetBundle、YooAsset、HTTP、AssetLib233 下载 |
| 竞品目标 | **HybridCLR Pro 纯解释上限**（~76.9% native IL2CPP） |
| 社区版 | HybridCLR 8.11.0 同机表仅校准环境 |

---

## 分层迭代（禁止跳层）

```text
L0  validate-reports + go test     秒级   opcode/Pro 文档/RuntimeFast
L1  pro-gate + quick                秒级   能力矩阵 + base JSON 缺口
L3  webgl                           ~80–95s 唯一 IL2CPP 浏览器实测
L4  hybridclr-webgl                 更长   同机社区版对照
L5  perf / full verification        最长   base 改善后再跑业务行
```

**架构改动后必须 L0→L1→L3**，不得用旧 JSON 或更长 timeout 代替判断。

---

## 命令

```powershell
go test ./tools/hotc233ctl/...
go run ./tools/hotc233ctl validate-reports -project .
go run ./tools/hotc233ctl pro-gate -project .
go run ./tools/hotc233ctl quick -project .
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'; go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
go run ./tools/hotc233ctl hybridclr-webgl -project . -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo -loader-profile RuntimeFast -timeout 60m
```

Native 解释器改动后：先 **Generate/All** 或确保 `EnsureBuiltinRuntimeReady()` 同步 `Hotc233Data/LocalIl2CppData-*`，再打 WebGL。

---

## 产出文件

| 文件 | 用途 |
|---|---|
| `performance-webgl-local-il2cpp.json` | hotc233 WebGL 主表 + 上轮对比 |
| `performance-webgl-hotc-vs-hybridclr-base.json` | vs 社区版 + Pro 架构目标 |
| `webgl-hotc233-player-report.json` | 原始 GC/heap/opcode |
| `hotc233ctl-webgl-result.json` | 成功/失败/耗时 |
| `pro-landing-gate.json` | 推荐下一 Stage |
| `quick-base-performance-gate.json` | base-only 快速门禁 |
| `Hotc233Data/AutomationLogs/*` | CDP/浏览器日志（不进 Assets） |

---

## 表格规则

主表最左列 = **hotc233 operation**。必含：本轮 ms、相对上轮、native IL2CPP%、Pro 目标%、社区版参考。

连续 **2 次** WebGL 无接近 Pro 或倒退 → 停止该路线，记入 [`pro-wrong-answer-notebook.md`](pro-wrong-answer-notebook.md)。

---

## 有效快照与禁止引用

| 时间 | 状态 |
|---|---|
| **2026-06-26T12:40:59Z** | 最后有效基准（RegI32 + 撤回 LinearTrace 前） |
| 2026-06-26 RunI4LinearTrace v4 | **作废** — 691s CDP 超时 |

---

## Trace 规则

| 允许 | 禁止 |
|---|---|
| `RunI4AddCopyTrace`（run>=4 窄形状） | `RunI4LinearTrace` 及通用 i4 linear trace |
| `RunStaticF4CallTrace` / `RunStaticI4CallTrace`（run>=3） | interpreter 内 for+switch 通用 trace |
| `RegI32*` 独立 opcode | 全局 copy propagation |

WebGL marker 超时 = **立即 blocked**，不继续等满 timeout。

---

## 自动化踩坑

| 问题 | 决策 |
|---|---|
| live log 写入 `Assets/EditorForBuild/Generated/logs/` | Unity 导入循环 → 日志改 `Hotc233Data/AutomationLogs/` |
| `RunI4LinearTrace` 691s hang | 撤回；改 typed register IR |
