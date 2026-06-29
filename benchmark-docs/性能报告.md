# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T16:21:57.470772Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2353.5% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 1289916.0%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.10 ms / 103842160 ops/s | 0.08 ms / 4412153 ops/s | 0.31 ms / 32429326 ops/s | 10000 | **2353.5%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1063829787 ops/s | 0.22 ms / 7519136 ops/s | 0.18 ms / 55265646 ops/s | 10000 | **14148.3%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1449275362 ops/s | 0.18 ms / 9542857 ops/s | 0.14 ms / 70140000 ops/s | 10000 | **15187.0%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2127659574 ops/s | 0.02 ms / 13858921 ops/s | 0.48 ms / 20788382 ops/s | 10000 | **15352.3%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1298701299 ops/s | 0.21 ms / 8005753 ops/s | 0.17 ms / 58842282 ops/s | 10000 | **16222.1%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 2083333333 ops/s | 0.03 ms / 11802120 ops/s | 0.12 ms / 86745583 ops/s | 10000 | **17652.2%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2173913043 ops/s | 0.04 ms / 8743455 ops/s | 0.16 ms / 64264398 ops/s | 10000 | **24863.3%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2127659574 ops/s | 0.04 ms / 7822014 ops/s | 0.85 ms / 11733021 ops/s | 10000 | **27200.9%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2173913043 ops/s | 0.05 ms / 7276688 ops/s | 0.92 ms / 10915033 ops/s | 10000 | **29875.0%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.00 ms / 2083333333 ops/s | 0.06 ms / 5318471 ops/s | 0.26 ms / 39090764 ops/s | 10000 | **39171.7%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 2173913043 ops/s | 0.08 ms / 4366013 ops/s | 0.31 ms / 32090196 ops/s | 10000 | **49791.7%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 2173913043 ops/s | 0.18 ms / 1822149 ops/s | 4.99 ms / 2004364 ops/s | 10000 | **119304.9%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 20000000000 ops/s | 0.34 ms / 4856063 ops/s | 0.21 ms / 48560628 ops/s | 10000 | **411856.3%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.01 ms / 1960784314 ops/s | 0.55 ms / 152009 ops/s | 59.81 ms / 167210 ops/s | 10000 | **1289916.0%** | 1.00x | 1.00x | missing | - |

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
