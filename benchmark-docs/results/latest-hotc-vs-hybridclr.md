# hotc233 性能报告

- 更新时间 (UTC): 2026-06-30T13:55:01.7540122Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2389.4% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **SetTransformPosition** = 1073652.7%
- 业务场景最弱项: **List 池化 Rent/Return** = 15.3%
- 业务场景最强项: **自定义类虚派发** = 136.8%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 107411386 ops/s | 0.07 ms / 4495289 ops/s | 0.30 ms / 33040377 ops/s | 10000 | **2389.4%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1020408163 ops/s | 0.17 ms / 9964200 ops/s | 0.14 ms / 73236874 ops/s | 10000 | **10240.7%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2083333333 ops/s | 0.02 ms / 14649123 ops/s | 0.46 ms / 21973684 ops/s | 10000 | **14221.6%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1492537313 ops/s | 0.17 ms / 9548313 ops/s | 0.14 ms / 70180103 ops/s | 10000 | **15631.4%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.01 ms / 1886792453 ops/s | 0.03 ms / 9794721 ops/s | 0.14 ms / 71991202 ops/s | 10000 | **19263.4%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1886792453 ops/s | 0.19 ms / 8657335 ops/s | 0.16 ms / 63631415 ops/s | 10000 | **21794.1%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.01 ms / 1960784314 ops/s | 0.04 ms / 8391960 ops/s | 0.16 ms / 61680905 ops/s | 10000 | **23365.0%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2040816327 ops/s | 0.04 ms / 7573696 ops/s | 0.88 ms / 11360544 ops/s | 10000 | **26946.1%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 1333333333 ops/s | 0.07 ms / 4918999 ops/s | 0.28 ms / 36154639 ops/s | 10000 | **27105.8%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2083333333 ops/s | 0.05 ms / 7016807 ops/s | 0.95 ms / 10525210 ops/s | 10000 | **29690.6%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.01 ms / 1666666667 ops/s | 0.08 ms / 4304124 ops/s | 0.32 ms / 31635309 ops/s | 10000 | **38722.6%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 25000000000 ops/s | 0.40 ms / 4133663 ops/s | 0.24 ms / 41336634 ops/s | 10000 | **604790.4%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.01 ms / 1886792453 ops/s | 0.43 ms / 196767 ops/s | 46.20 ms / 216444 ops/s | 10000 | **958894.9%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 20000000000 ops/s | 0.18 ms / 1862800 ops/s | 4.88 ms / 2049080 ops/s | 10000 | **1073652.7%** | 1.00x | 1.00x | missing | - |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| List 池化 Rent/Return | 0.19 ms / 51493 ops/s | 0.03 ms / 336700 ops/s | 0.01 ms / 966330 ops/s | 10 | 15.3% | 6.54x | 18.77x | missing | - |
| Tween 模拟更新 | 0.06 ms / 173310 ops/s | 0.01 ms / 917431 ops/s | 0.00 ms / 2633028 ops/s | 10 | 18.9% | 5.29x | 15.19x | missing | - |
| async/await 热循环 | 0.00 ms / 5000000 ops/s | 0.00 ms / 25000000 ops/s | 0.00 ms / 71750000 ops/s | 10 | 20.0% | 5.00x | 14.35x | missing | - |
| 协程式 IEnumerator | 0.12 ms / 81103 ops/s | 0.03 ms / 320513 ops/s | 0.01 ms / 919872 ops/s | 10 | 25.3% | 3.95x | 11.34x | missing | - |
| Task.WhenAll | 0.00 ms / 5263158 ops/s | 0.00 ms / 16666667 ops/s | 0.00 ms / 47833333 ops/s | 10 | 31.6% | 3.17x | 9.09x | missing | - |
| Event 多播 | 0.06 ms / 162338 ops/s | 0.03 ms / 343643 ops/s | 0.01 ms / 986254 ops/s | 10 | 47.2% | 2.12x | 6.08x | missing | - |
| 配置表 Dictionary 查找 | 0.05 ms / 202429 ops/s | 0.03 ms / 373134 ops/s | 0.01 ms / 1070896 ops/s | 10 | 54.3% | 1.84x | 5.29x | missing | - |
| Callback 链 | 0.04 ms / 226757 ops/s | 0.02 ms / 413223 ops/s | 0.01 ms / 1185950 ops/s | 10 | 54.9% | 1.82x | 5.23x | missing | - |
| Struct 传值与字段更新 | 0.01 ms / 2000000 ops/s | 0.00 ms / 2040816 ops/s | 0.00 ms / 5857143 ops/s | 10 | 98.0% | 1.02x | 2.93x | missing | - |
| 自定义类虚派发 | 0.01 ms / 800000 ops/s | 0.02 ms / 584795 ops/s | 0.01 ms / 1678363 ops/s | 10 | **136.8%** | 1.00x | 2.10x | missing | - |

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
