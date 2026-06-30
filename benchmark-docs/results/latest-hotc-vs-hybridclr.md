# hotc233 性能报告

- 更新时间 (UTC): 2026-06-30T12:46:15.2565033Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2501.0% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 902472.5%
- 业务场景最弱项: **配置表 Dictionary 查找** = 4.2%
- 业务场景最强项: **Struct 传值与字段更新** = 112.8%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 108342362 ops/s | 0.08 ms / 4332036 ops/s | 0.31 ms / 31840467 ops/s | 10000 | **2501.0%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.01 ms / 1265822785 ops/s | 0.03 ms / 10986842 ops/s | 0.12 ms / 80753289 ops/s | 10000 | **11521.3%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1075268817 ops/s | 0.23 ms / 7248264 ops/s | 0.19 ms / 53274740 ops/s | 10000 | **14834.8%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2127659574 ops/s | 0.02 ms / 13577236 ops/s | 0.49 ms / 20365854 ops/s | 10000 | **15670.8%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1538461538 ops/s | 0.17 ms / 9548313 ops/s | 0.14 ms / 70180103 ops/s | 10000 | **16112.4%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 990099010 ops/s | 0.06 ms / 5276461 ops/s | 0.26 ms / 38781991 ops/s | 10000 | **18764.5%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1960784314 ops/s | 0.21 ms / 7840376 ops/s | 0.17 ms / 57626761 ops/s | 10000 | **25008.8%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2083333333 ops/s | 0.04 ms / 8287841 ops/s | 0.16 ms / 60915633 ops/s | 10000 | **25137.2%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.01 ms / 1190476190 ops/s | 0.07 ms / 4569083 ops/s | 0.30 ms / 33582763 ops/s | 10000 | **26055.0%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2040816327 ops/s | 0.04 ms / 7625571 ops/s | 0.87 ms / 11438356 ops/s | 10000 | **26762.8%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2083333333 ops/s | 0.05 ms / 7276688 ops/s | 0.92 ms / 10915033 ops/s | 10000 | **28630.2%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 25000000000 ops/s | 0.41 ms / 4105211 ops/s | 0.24 ms / 41052114 ops/s | 10000 | **608982.0%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 16666666667 ops/s | 0.18 ms / 1875351 ops/s | 4.85 ms / 2062886 ops/s | 10000 | **888722.6%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.01 ms / 1923076923 ops/s | 0.39 ms / 213090 ops/s | 42.66 ms / 234399 ops/s | 10000 | **902472.5%** | 1.00x | 1.00x | missing | - |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| 配置表 Dictionary 查找 | 0.60 ms / 16592 ops/s | 0.03 ms / 395257 ops/s | 0.01 ms / 1134387 ops/s | 10 | 4.2% | 23.82x | 68.37x | missing | - |
| 自定义类虚派发 | 0.16 ms / 64350 ops/s | 0.02 ms / 512821 ops/s | 0.01 ms / 1471795 ops/s | 10 | 12.5% | 7.97x | 22.87x | missing | - |
| Tween 模拟更新 | 0.06 ms / 155521 ops/s | 0.01 ms / 909091 ops/s | 0.00 ms / 2609091 ops/s | 10 | 17.1% | 5.85x | 16.78x | missing | - |
| async/await 热循环 | 0.00 ms / 4761905 ops/s | 0.00 ms / 20000000 ops/s | 0.00 ms / 57400000 ops/s | 10 | 23.8% | 4.20x | 12.05x | missing | - |
| List 池化 Rent/Return | 0.12 ms / 85324 ops/s | 0.03 ms / 330033 ops/s | 0.01 ms / 947195 ops/s | 10 | 25.9% | 3.87x | 11.10x | missing | - |
| Callback 链 | 0.08 ms / 122100 ops/s | 0.02 ms / 454545 ops/s | 0.01 ms / 1304545 ops/s | 10 | 26.9% | 3.72x | 10.68x | missing | - |
| 协程式 IEnumerator | 0.13 ms / 77160 ops/s | 0.04 ms / 268097 ops/s | 0.01 ms / 769437 ops/s | 10 | 28.8% | 3.47x | 9.97x | missing | - |
| Event 多播 | 0.10 ms / 102669 ops/s | 0.03 ms / 331126 ops/s | 0.01 ms / 950331 ops/s | 10 | 31.0% | 3.23x | 9.26x | missing | - |
| Task.WhenAll | 0.00 ms / 4761905 ops/s | 0.00 ms / 14285714 ops/s | 0.00 ms / 41000000 ops/s | 10 | 33.3% | 3.00x | 8.61x | missing | - |
| Struct 传值与字段更新 | 0.00 ms / 2127660 ops/s | 0.01 ms / 1886792 ops/s | 0.00 ms / 5415094 ops/s | 10 | **112.8%** | 1.00x | 2.55x | missing | - |

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
