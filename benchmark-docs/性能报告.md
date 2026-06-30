# hotc233 性能报告

- 更新时间 (UTC): 2026-06-30T13:18:12.4583677Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- xLua 对照: 已启用但未产出数据（检查 `Assets/XLua` 与 `tools/setup-xlua.ps1`）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2407.7% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **SetTransformPosition** = 1031736.5%
- 业务场景最弱项: **配置表 Dictionary 查找** = 7.2%
- 业务场景最强项: **自定义类虚派发** = 143.2%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.09 ms / 109409190 ops/s | 0.07 ms / 4544218 ops/s | 0.30 ms / 33400000 ops/s | 10000 | **2407.7%** | 1.00x | 1.00x | missing | - |
| VectorOp1 sqrMagnitude | 0.03 ms / 322580645 ops/s | 0.05 ms / 7276688 ops/s | 0.19 ms / 53483660 ops/s | 10000 | **4433.1%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnInt | 0.00 ms / 2083333333 ops/s | 0.02 ms / 14585153 ops/s | 0.46 ms / 21877729 ops/s | 10000 | **14283.9%** | 1.00x | 1.00x | missing | - |
| BinOpAdd 简单数值 | 0.01 ms / 1492537313 ops/s | 0.20 ms / 8464268 ops/s | 0.16 ms / 62212367 ops/s | 10000 | **17633.4%** | 1.00x | 1.00x | missing | - |
| ArrayOp 数组写读 | 0.01 ms / 1075268817 ops/s | 0.32 ms / 5176689 ops/s | 0.26 ms / 38048667 ops/s | 10000 | **20771.4%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ReturnVector3 | 0.00 ms / 2127659574 ops/s | 0.04 ms / 8434343 ops/s | 0.16 ms / 61992424 ops/s | 10000 | **25226.1%** | 1.00x | 1.00x | missing | - |
| BinOpComplex 复杂数值 | 0.00 ms / 2127659574 ops/s | 0.26 ms / 6536204 ops/s | 0.21 ms / 48041096 ops/s | 10000 | **32551.9%** | 1.00x | 1.00x | missing | - |
| CallAOTStaticMethod | 0.00 ms / 2083333333 ops/s | 0.06 ms / 5651438 ops/s | 1.18 ms / 8477157 ops/s | 10000 | **36863.8%** | 1.00x | 1.00x | missing | - |
| CallAOTInstance ParamInt | 0.00 ms / 2083333333 ops/s | 0.06 ms / 5226917 ops/s | 1.28 ms / 7840376 ops/s | 10000 | **39857.8%** | 1.00x | 1.00x | missing | - |
| VectorOp2 Vector3 加法 | 0.01 ms / 1408450704 ops/s | 0.10 ms / 3453981 ops/s | 0.39 ms / 25386763 ops/s | 10000 | **40777.6%** | 1.00x | 1.00x | missing | - |
| QuaternionOp | 0.01 ms / 1754385965 ops/s | 0.10 ms / 3223938 ops/s | 0.42 ms / 23695946 ops/s | 10000 | **54417.5%** | 1.00x | 1.00x | missing | - |
| typeof 指令 | 0.00 ms / 25000000000 ops/s | 0.48 ms / 3456126 ops/s | 0.29 ms / 34561258 ops/s | 10000 | **723353.3%** | 1.00x | 1.00x | missing | - |
| GameObject Create/Destroy | 0.01 ms / 2000000000 ops/s | 0.36 ms / 230453 ops/s | 39.45 ms / 253498 ops/s | 10000 | **867857.1%** | 1.00x | 1.00x | missing | - |
| SetTransformPosition | 0.00 ms / 20000000000 ops/s | 0.17 ms / 1938479 ops/s | 4.69 ms / 2132327 ops/s | 10000 | **1031736.5%** | 1.00x | 1.00x | missing | - |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| 配置表 Dictionary 查找 | 0.33 ms / 30572 ops/s | 0.02 ms / 425532 ops/s | 0.01 ms / 1221277 ops/s | 10 | 7.2% | 13.92x | 39.95x | missing | - |
| List 池化 Rent/Return | 0.36 ms / 27693 ops/s | 0.03 ms / 353357 ops/s | 0.01 ms / 1014134 ops/s | 10 | 7.8% | 12.76x | 36.62x | missing | - |
| Tween 模拟更新 | 0.06 ms / 160256 ops/s | 0.01 ms / 934579 ops/s | 0.00 ms / 2682243 ops/s | 10 | 17.1% | 5.83x | 16.74x | missing | - |
| 协程式 IEnumerator | 0.13 ms / 78309 ops/s | 0.03 ms / 325733 ops/s | 0.01 ms / 934853 ops/s | 10 | 24.0% | 4.16x | 11.94x | missing | - |
| Callback 链 | 0.07 ms / 133511 ops/s | 0.02 ms / 483092 ops/s | 0.01 ms / 1386473 ops/s | 10 | 27.6% | 3.62x | 10.38x | missing | - |
| async/await 热循环 | 0.00 ms / 5882353 ops/s | 0.00 ms / 20000000 ops/s | 0.00 ms / 57400000 ops/s | 10 | 29.4% | 3.40x | 9.76x | missing | - |
| Event 多播 | 0.10 ms / 103734 ops/s | 0.03 ms / 352113 ops/s | 0.01 ms / 1010563 ops/s | 10 | 29.5% | 3.39x | 9.74x | missing | - |
| Task.WhenAll | 0.00 ms / 5000000 ops/s | 0.00 ms / 16666667 ops/s | 0.00 ms / 47833333 ops/s | 10 | 30.0% | 3.33x | 9.57x | missing | - |
| Struct 传值与字段更新 | 0.00 ms / 2272727 ops/s | 0.00 ms / 2083333 ops/s | 0.00 ms / 5979167 ops/s | 10 | **109.1%** | 1.00x | 2.63x | missing | - |
| 自定义类虚派发 | 0.01 ms / 847458 ops/s | 0.02 ms / 591716 ops/s | 0.01 ms / 1698225 ops/s | 10 | **143.2%** | 1.00x | 2.00x | missing | - |

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
