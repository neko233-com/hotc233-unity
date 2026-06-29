# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T16:57:21.6723537Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2592.8% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 1417933.1%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 110741971 ops/s | 0.08 ms / 4271100 ops/s | 0.32 ms / 31392583 ops/s | 10000 | **2592.8%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1098901099 ops/s | 0.19 ms / 8621580 ops/s | 0.16 ms / 63368611 ops/s | 10000 | **12745.9%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2173913043 ops/s | 0.02 ms / 14521739 ops/s | 0.46 ms / 21782609 ops/s | 10000 | **14970.1%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1492537313 ops/s | 0.18 ms / 9313999 ops/s | 0.15 ms / 68457892 ops/s | 10000 | **16024.7%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1351351351 ops/s | 0.22 ms / 7519136 ops/s | 0.18 ms / 55265646 ops/s | 10000 | **17972.2%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 2127659574 ops/s | 0.03 ms / 11597222 ops/s | 0.12 ms / 85239583 ops/s | 10000 | **18346.3%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2083333333 ops/s | 0.04 ms / 8520408 ops/s | 0.16 ms / 62625000 ops/s | 10000 | **24451.1%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 1351351351 ops/s | 0.07 ms / 5030120 ops/s | 0.27 ms / 36971386 ops/s | 10000 | **26865.2%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2127659574 ops/s | 0.04 ms / 7840376 ops/s | 0.85 ms / 11760563 ops/s | 10000 | **27137.2%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2173913043 ops/s | 0.05 ms / 6680000 ops/s | 1.00 ms / 10020000 ops/s | 10000 | **32543.6%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 2173913043 ops/s | 0.07 ms / 4613260 ops/s | 0.29 ms / 33907459 ops/s | 10000 | **47123.1%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 2127659574 ops/s | 0.17 ms / 1928406 ops/s | 4.71 ms / 2121247 ops/s | 10000 | **110332.5%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 20000000000 ops/s | 0.37 ms / 4554131 ops/s | 0.22 ms / 45541314 ops/s | 10000 | **439161.7%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.00 ms / 2127659574 ops/s | 0.56 ms / 150054 ops/s | 60.58 ms / 165059 ops/s | 10000 | **1417933.1%** | 1.00x | 1.00x | missing | - |

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
