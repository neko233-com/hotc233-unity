# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T04:34:33.7126853Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2052.2% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 4519320.0%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.10 ms / 105263158 ops/s | 1.95 ms / 5129257 ops/s | 0.27 ms / 37700041 ops/s | 10000 | **2052.2%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1923076923 ops/s | 0.91 ms / 11007155 ops/s | 0.12 ms / 80902587 ops/s | 10000 | **17471.2%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.00 ms / 5000000000 ops/s | 1.04 ms / 9652510 ops/s | 0.14 ms / 70945946 ops/s | 10000 | **51800.0%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 16666666667 ops/s | 0.58 ms / 17167382 ops/s | 0.08 ms / 126180258 ops/s | 10000 | **97083.3%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 20000000000 ops/s | 0.54 ms / 18525380 ops/s | 0.36 ms / 27788070 ops/s | 10000 | **107960.0%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 12500000000 ops/s | 0.97 ms / 10325245 ops/s | 0.65 ms / 15487868 ops/s | 10000 | **121062.5%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 16666666667 ops/s | 0.77 ms / 13020833 ops/s | 0.51 ms / 19531250 ops/s | 10000 | **128000.0%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 25000000000 ops/s | 0.85 ms / 11799410 ops/s | 0.12 ms / 86725664 ops/s | 10000 | **211875.0%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.00 ms / 20000000000 ops/s | 1.59 ms / 6307159 ops/s | 0.22 ms / 46357616 ops/s | 10000 | **317100.0%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 20000000000 ops/s | 2.02 ms / 4943154 ops/s | 0.20 ms / 49431537 ops/s | 10000 | **404600.0%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.00 ms / 16666666667 ops/s | 2.59 ms / 3862495 ops/s | 0.35 ms / 28389340 ops/s | 10000 | **431500.0%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 25000000000 ops/s | 1.91 ms / 5241640 ops/s | 0.26 ms / 38526051 ops/s | 10000 | **476950.0%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 20000000000 ops/s | 4.80 ms / 2082075 ops/s | 4.37 ms / 2290283 ops/s | 10000 | **960580.0%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.00 ms / 20000000000 ops/s | 22.60 ms / 442544 ops/s | 20.54 ms / 486799 ops/s | 10000 | **4519320.0%** | 1.00x | 1.00x | missing | - |

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
