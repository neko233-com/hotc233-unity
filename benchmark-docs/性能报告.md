# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T16:05:07.8508795Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2521.8% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 980458.2%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 109529025 ops/s | 0.08 ms / 4343303 ops/s | 0.31 ms / 31923277 ops/s | 10000 | **2521.8%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1041666667 ops/s | 0.23 ms / 7360071 ops/s | 0.18 ms / 54096518 ops/s | 10000 | **14152.9%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.01 ms / 1923076923 ops/s | 0.02 ms / 13577236 ops/s | 0.49 ms / 20365854 ops/s | 10000 | **14164.0%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1204819277 ops/s | 0.22 ms / 7608200 ops/s | 0.18 ms / 55920273 ops/s | 10000 | **15835.8%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.01 ms / 1265822785 ops/s | 0.04 ms / 7625571 ops/s | 0.87 ms / 11438356 ops/s | 10000 | **16599.7%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1612903226 ops/s | 0.17 ms / 9548313 ops/s | 0.14 ms / 70180103 ops/s | 10000 | **16892.0%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.01 ms / 2000000000 ops/s | 0.03 ms / 10670927 ops/s | 0.13 ms / 78431310 ops/s | 10000 | **18742.5%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.01 ms / 1960784314 ops/s | 0.04 ms / 8652850 ops/s | 0.16 ms / 63598446 ops/s | 10000 | **22660.6%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 1234567901 ops/s | 0.07 ms / 5107034 ops/s | 0.27 ms / 37536697 ops/s | 10000 | **24173.9%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.01 ms / 1265822785 ops/s | 0.07 ms / 4531886 ops/s | 0.30 ms / 33309362 ops/s | 10000 | **27931.5%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.01 ms / 1960784314 ops/s | 0.05 ms / 6640159 ops/s | 1.00 ms / 9960239 ops/s | 10000 | **29529.2%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.01 ms / 1923076923 ops/s | 0.18 ms / 1805405 ops/s | 5.04 ms / 1985946 ops/s | 10000 | **106517.7%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 20000000000 ops/s | 0.37 ms / 4539277 ops/s | 0.22 ms / 45392770 ops/s | 10000 | **440598.8%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.01 ms / 1886792453 ops/s | 0.44 ms / 192440 ops/s | 47.24 ms / 211684 ops/s | 10000 | **980458.2%** | 1.00x | 1.00x | missing | - |

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
