# hotc233 性能报告

- 更新时间 (UTC): 2026-06-30T14:36:31.3675637Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **BinOpAdd 简单数值** = 110.0% (`hybridclr-binop-add`)
- 官方 base 最强项: **GameObject Create/Destroy** = 36214.3%
- 业务场景最弱项: **Tween 模拟更新** = 19.7%
- 业务场景最强项: **自定义类虚派发** = 222.0%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| BinOpAdd 简单数值 | 0.00 ms / 1250000 ops/s | 0.01 ms / 1136364 ops/s | 0.00 ms / 8352273 ops/s | 1 | **110.0%** | 1.00x | 6.68x | missing | - |
| BinOpComplex 复杂数值 | 0.00 ms / 1666667 ops/s | 0.01 ms / 1190476 ops/s | 0.00 ms / 8750000 ops/s | 1 | **140.0%** | 1.00x | 5.25x | missing | - |
| ArrayOp 数组写读 | 0.00 ms / 2500000 ops/s | 0.01 ms / 1098901 ops/s | 0.00 ms / 8076923 ops/s | 1 | **227.5%** | 1.00x | 3.23x | missing | - |
| typeof 指令 | 0.00 ms / 2500000 ops/s | 0.03 ms / 328947 ops/s | 0.00 ms / 3289474 ops/s | 1 | **760.0%** | 1.00x | 1.32x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 3333333 ops/s | 0.01 ms / 338983 ops/s | 0.00 ms / 508475 ops/s | 1 | **983.3%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 2000000 ops/s | 0.01 ms / 176991 ops/s | 0.00 ms / 1300885 ops/s | 1 | **1130.0%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamVector3 | 0.00 ms / 1666667 ops/s | 0.02 ms / 131579 ops/s | 0.00 ms / 967105 ops/s | 1 | **1266.7%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 3333333 ops/s | 0.01 ms / 194175 ops/s | 0.00 ms / 1427184 ops/s | 1 | **1716.7%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2500000 ops/s | 0.02 ms / 130719 ops/s | 0.01 ms / 196078 ops/s | 1 | **1912.5%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.00 ms / 2500000 ops/s | 0.02 ms / 125786 ops/s | 0.00 ms / 924528 ops/s | 1 | **1987.5%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 3333333 ops/s | 0.01 ms / 162602 ops/s | 0.00 ms / 243902 ops/s | 1 | **2050.0%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 2000000 ops/s | 0.03 ms / 69930 ops/s | 0.00 ms / 513986 ops/s | 1 | **2860.0%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 2000000 ops/s | 0.03 ms / 58651 ops/s | 0.02 ms / 64516 ops/s | 1 | **3410.0%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.00 ms / 1428571 ops/s | 0.25 ms / 3945 ops/s | 0.23 ms / 4339 ops/s | 1 | **36214.3%** | 1.00x | 1.00x | missing | - |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| Tween 模拟更新 | 0.06 ms / 173913 ops/s | 0.01 ms / 884956 ops/s | 0.00 ms / 2539823 ops/s | 10 | 19.7% | 5.09x | 14.60x | missing | - |
| List 池化 Rent/Return | 0.13 ms / 79554 ops/s | 0.03 ms / 343643 ops/s | 0.01 ms / 986254 ops/s | 10 | 23.2% | 4.32x | 12.40x | missing | - |
| async/await 热循环 | 0.00 ms / 5882353 ops/s | 0.00 ms / 25000000 ops/s | 0.00 ms / 71750000 ops/s | 10 | 23.5% | 4.25x | 12.20x | missing | - |
| 协程式 IEnumerator | 0.11 ms / 89767 ops/s | 0.03 ms / 330033 ops/s | 0.01 ms / 947195 ops/s | 10 | 27.2% | 3.68x | 10.55x | missing | - |
| Task.WhenAll | 0.00 ms / 5263158 ops/s | 0.00 ms / 14285714 ops/s | 0.00 ms / 41000000 ops/s | 10 | 36.8% | 2.71x | 7.79x | missing | - |
| Event 多播 | 0.05 ms / 190114 ops/s | 0.03 ms / 347222 ops/s | 0.01 ms / 996528 ops/s | 10 | 54.8% | 1.83x | 5.24x | missing | - |
| Callback 链 | 0.03 ms / 320513 ops/s | 0.02 ms / 404858 ops/s | 0.01 ms / 1161943 ops/s | 10 | 79.2% | 1.26x | 3.63x | missing | - |
| 配置表 Dictionary 查找 | 0.03 ms / 305810 ops/s | 0.03 ms / 384615 ops/s | 0.01 ms / 1103846 ops/s | 10 | 79.5% | 1.26x | 3.61x | missing | - |
| Struct 传值与字段更新 | 0.00 ms / 2380952 ops/s | 0.01 ms / 1960784 ops/s | 0.00 ms / 5627451 ops/s | 10 | **121.4%** | 1.00x | 2.36x | missing | - |
| 自定义类虚派发 | 0.01 ms / 1000000 ops/s | 0.02 ms / 450450 ops/s | 0.01 ms / 1292793 ops/s | 10 | **222.0%** | 1.00x | 1.29x | missing | - |

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
