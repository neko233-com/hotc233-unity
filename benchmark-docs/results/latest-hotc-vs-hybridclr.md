# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T16:30:22.9969666Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2540.6% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 1447764.8%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 107411386 ops/s | 0.08 ms / 4227848 ops/s | 0.32 ms / 31074684 ops/s | 10000 | **2540.6%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1041666667 ops/s | 0.20 ms / 8559713 ops/s | 0.16 ms / 62913890 ops/s | 10000 | **12169.4%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2173913043 ops/s | 0.02 ms / 14212766 ops/s | 0.47 ms / 21319149 ops/s | 10000 | **15295.5%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1492537313 ops/s | 0.18 ms / 9521095 ops/s | 0.14 ms / 69980046 ops/s | 10000 | **15676.1%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1219512195 ops/s | 0.23 ms / 7353589 ops/s | 0.19 ms / 54048877 ops/s | 10000 | **16583.9%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 2127659574 ops/s | 0.03 ms / 11096346 ops/s | 0.12 ms / 81558140 ops/s | 10000 | **19174.4%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2040816327 ops/s | 0.04 ms / 8370927 ops/s | 0.16 ms / 61526316 ops/s | 10000 | **24379.8%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2127659574 ops/s | 0.04 ms / 7472036 ops/s | 0.89 ms / 11208054 ops/s | 10000 | **28475.0%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2127659574 ops/s | 0.05 ms / 6626984 ops/s | 1.01 ms / 9940476 ops/s | 10000 | **32106.0%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.00 ms / 2083333333 ops/s | 0.07 ms / 4977645 ops/s | 0.27 ms / 36585693 ops/s | 10000 | **41853.8%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 2040816327 ops/s | 0.07 ms / 4525745 ops/s | 0.30 ms / 33264228 ops/s | 10000 | **45093.5%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 2083333333 ops/s | 0.19 ms / 1793770 ops/s | 5.07 ms / 1973147 ops/s | 10000 | **116142.7%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 16666666667 ops/s | 0.36 ms / 4611986 ops/s | 0.22 ms / 46119856 ops/s | 10000 | **361377.2%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.00 ms / 2040816327 ops/s | 0.60 ms / 140963 ops/s | 64.49 ms / 155060 ops/s | 10000 | **1447764.8%** | 1.00x | 1.00x | missing | - |

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
