# hotc233 性能报告

- 更新时间 (UTC): 2026-06-30T11:38:05.4546039Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 业务拉伸门禁 (>= 500%): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2605.8% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 1213799.8%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 109890110 ops/s | 0.08 ms / 4217172 ops/s | 0.32 ms / 30996212 ops/s | 10000 | **2605.8%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.01 ms / 1333333333 ops/s | 0.03 ms / 10774194 ops/s | 0.13 ms / 79190323 ops/s | 10000 | **12375.2%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1063829787 ops/s | 0.20 ms / 8210423 ops/s | 0.17 ms / 60346608 ops/s | 10000 | **12957.1%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2127659574 ops/s | 0.02 ms / 14273504 ops/s | 0.47 ms / 21410256 ops/s | 10000 | **14906.4%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1538461538 ops/s | 0.18 ms / 9334824 ops/s | 0.15 ms / 68610956 ops/s | 10000 | **16480.9%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 1052631579 ops/s | 0.07 ms / 4992526 ops/s | 0.27 ms / 36695067 ops/s | 10000 | **21084.1%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2040816327 ops/s | 0.04 ms / 8564103 ops/s | 0.16 ms / 62946154 ops/s | 10000 | **23829.9%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.01 ms / 1123595506 ops/s | 0.07 ms / 4544218 ops/s | 0.30 ms / 33400000 ops/s | 10000 | **24725.8%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2083333333 ops/s | 0.04 ms / 7822014 ops/s | 0.85 ms / 11733021 ops/s | 10000 | **26634.2%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 2000000000 ops/s | 0.26 ms / 6418140 ops/s | 0.21 ms / 47173328 ops/s | 10000 | **31161.7%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2127659574 ops/s | 0.05 ms / 6680000 ops/s | 1.00 ms / 10020000 ops/s | 10000 | **31851.2%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 25000000000 ops/s | 0.36 ms / 4613260 ops/s | 0.22 ms / 46132597 ops/s | 10000 | **541916.2%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 20000000000 ops/s | 0.17 ms / 1935110 ops/s | 4.70 ms / 2128621 ops/s | 10000 | **1033532.9%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.00 ms / 2040816327 ops/s | 0.50 ms / 168135 ops/s | 54.07 ms / 184948 ops/s | 10000 | **1213799.8%** | 1.00x | 1.00x | missing | - |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| async/await 热循环 | 0.00 ms / 6250000 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| Task.WhenAll | 0.00 ms / 5263158 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| Callback 链 | 0.00 ms / 3571429 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| Struct 传值与字段更新 | 0.00 ms / 2222222 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| Event 多播 | 0.01 ms / 1388889 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| 协程式 IEnumerator | 0.12 ms / 85837 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| Tween 模拟更新 | 0.24 ms / 41563 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| 自定义类虚派发 | 0.27 ms / 36523 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| List 池化 Rent/Return | 0.30 ms / 33212 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |
| 配置表 Dictionary 查找 | 1.24 ms / 8035 ops/s | n/a | n/a | 10 | n/a | n/a | n/a | n/a | n/a |

## 验收命令

```powershell
cd D:\Code\neko233-Projects\unity-hotc233-demo\tools
go run ./hotc233ctl generate -project .. -loader-profile RuntimeFast
go run ./hotc233ctl local-benchmark -project .. -loader-profile RuntimeFast -force-rebuild `
  -hybridclr-project ..\Benchmarks\hybridclr-benchmark-demo
$env:HOTC233_ENFORCE_PERFORMANCE='1'
$env:HOTC233_ENFORCE_BEAT_COMMUNITY='1'
go run ./hotc233ctl validate-reports -project ..
```
