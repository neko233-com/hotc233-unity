# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T16:39:39.0160821Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2634.2% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 978620.0%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 110253583 ops/s | 0.08 ms / 4185464 ops/s | 0.33 ms / 30763158 ops/s | 10000 | **2634.2%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1063829787 ops/s | 0.23 ms / 7402482 ops/s | 0.18 ms / 54408245 ops/s | 10000 | **14371.3%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2127659574 ops/s | 0.02 ms / 13577236 ops/s | 0.49 ms / 20365854 ops/s | 10000 | **15670.8%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1123595506 ops/s | 0.25 ms / 6567047 ops/s | 0.21 ms / 48267794 ops/s | 10000 | **17109.6%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1562500000 ops/s | 0.20 ms / 8198331 ops/s | 0.17 ms / 60257732 ops/s | 10000 | **19058.8%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 2173913043 ops/s | 0.03 ms / 11170569 ops/s | 0.12 ms / 82103679 ops/s | 10000 | **19461.1%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2127659574 ops/s | 0.04 ms / 8477157 ops/s | 0.16 ms / 62307107 ops/s | 10000 | **25098.7%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2083333333 ops/s | 0.04 ms / 7895981 ops/s | 0.84 ms / 11843972 ops/s | 10000 | **26384.7%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2083333333 ops/s | 0.05 ms / 6958333 ops/s | 0.96 ms / 10437500 ops/s | 10000 | **29940.1%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.00 ms / 2083333333 ops/s | 0.07 ms / 4948148 ops/s | 0.27 ms / 36368889 ops/s | 10000 | **42103.3%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 2083333333 ops/s | 0.07 ms / 4531886 ops/s | 0.30 ms / 33309362 ops/s | 10000 | **45970.6%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 2083333333 ops/s | 0.18 ms / 1879572 ops/s | 4.84 ms / 2067530 ops/s | 10000 | **110840.8%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 25000000000 ops/s | 0.38 ms / 4402847 ops/s | 0.23 ms / 44028474 ops/s | 10000 | **567814.4%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.00 ms / 2040816327 ops/s | 0.40 ms / 208540 ops/s | 43.59 ms / 229394 ops/s | 10000 | **978620.0%** | 1.00x | 1.00x | missing | - |

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
