# hotc233 性能报告

- 更新时间 (UTC): 2026-06-29T16:48:26.4039587Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2489.2% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 1256666.7%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 105374078 ops/s | 0.08 ms / 4233207 ops/s | 0.32 ms / 31114068 ops/s | 10000 | **2489.2%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.01 ms / 1724137931 ops/s | 0.02 ms / 13688525 ops/s | 0.49 ms / 20532787 ops/s | 10000 | **12595.5%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1190476190 ops/s | 0.20 ms / 8296076 ops/s | 0.16 ms / 60976155 ops/s | 10000 | **14349.9%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1075268817 ops/s | 0.22 ms / 7428826 ops/s | 0.18 ms / 54601868 ops/s | 10000 | **14474.3%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.01 ms / 1010101010 ops/s | 0.25 ms / 6598183 ops/s | 0.21 ms / 48496642 ops/s | 10000 | **15308.8%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.00 ms / 2127659574 ops/s | 0.03 ms / 11245791 ops/s | 0.12 ms / 82656566 ops/s | 10000 | **18919.6%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2083333333 ops/s | 0.04 ms / 8882979 ops/s | 0.15 ms / 65289894 ops/s | 10000 | **23453.1%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2083333333 ops/s | 0.04 ms / 7539503 ops/s | 0.88 ms / 11309255 ops/s | 10000 | **27632.2%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2083333333 ops/s | 0.05 ms / 6929461 ops/s | 0.96 ms / 10394191 ops/s | 10000 | **30064.9%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 1886792453 ops/s | 0.07 ms / 5030120 ops/s | 0.27 ms / 36971386 ops/s | 10000 | **37509.9%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.00 ms / 2083333333 ops/s | 0.07 ms / 4550409 ops/s | 0.30 ms / 33445504 ops/s | 10000 | **45783.4%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 2127659574 ops/s | 0.19 ms / 1760675 ops/s | 5.16 ms / 1936742 ops/s | 10000 | **120843.4%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 12500000000 ops/s | 0.37 ms / 4472416 ops/s | 0.22 ms / 44724156 ops/s | 10000 | **279491.0%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.01 ms / 2000000000 ops/s | 0.53 ms / 159151 ops/s | 57.12 ms / 175066 ops/s | 10000 | **1256666.7%** | 1.00x | 1.00x | missing | - |

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
