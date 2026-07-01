# hotc233 性能报告

- 更新时间 (UTC): 2026-07-01T02:07:40.7235062Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- Floor: `typeof=1000%`；HybridCLR 商业版公开算术项对应行 `500%`；其它官方 base 默认 `300%`；business 默认观察。
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- 分层 floor 门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2202.1% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 689183.3%
- 业务场景最弱项: **协程式 IEnumerator** = 18.6%
- 业务场景最强项: **Struct 传值与字段更新** = 139.0%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | floor | floor status |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|------:|--------------|
| CallAOTInstance ParamVector3 | 0.01 ms / 105263158 ops/s | 0.21 ms / 4780115 ops/s | 0.03 ms / 35133843 ops/s | 1000 | **2202.1%** | 1.00x | 1.00x | 300% | passed |
| BinOpAdd 简单数值 | 0.00 ms / 833333333 ops/s | 0.12 ms / 8474576 ops/s | 0.02 ms / 62288136 ops/s | 1000 | **9833.3%** | 1.00x | 1.00x | 500% | passed |
| VectorOp1 sqrMagnitude | 0.00 ms / 1428571429 ops/s | 0.08 ms / 12970169 ops/s | 0.01 ms / 95330739 ops/s | 1000 | **11014.3%** | 1.00x | 1.00x | 300% | passed |
| ArrayOp 数组写读 | 0.00 ms / 1000000000 ops/s | 0.14 ms / 6920415 ops/s | 0.02 ms / 50865052 ops/s | 1000 | **14450.0%** | 1.00x | 1.00x | 300% | passed |
| CallAOTInstance ReturnInt | 0.00 ms / 3333333333 ops/s | 0.08 ms / 13210040 ops/s | 0.05 ms / 19815059 ops/s | 1000 | **25233.3%** | 1.00x | 1.00x | 300% | passed |
| BinOpComplex 复杂数值 | 0.00 ms / 2000000000 ops/s | 0.15 ms / 6657790 ops/s | 0.02 ms / 48934754 ops/s | 1000 | **30040.0%** | 1.00x | 1.00x | 500% | passed |
| CallAOTInstance ReturnVector3 | 0.00 ms / 3333333333 ops/s | 0.13 ms / 7905138 ops/s | 0.02 ms / 58102767 ops/s | 1000 | **42166.7%** | 1.00x | 1.00x | 300% | passed |
| CallAOTInstance ParamInt | 0.00 ms / 3333333333 ops/s | 0.14 ms / 7251632 ops/s | 0.09 ms / 10877447 ops/s | 1000 | **45966.7%** | 1.00x | 1.00x | 300% | passed |
| CallAOTStaticMethod | 0.00 ms / 1666666667 ops/s | 0.38 ms / 2602811 ops/s | 0.26 ms / 3904217 ops/s | 1000 | **64033.3%** | 1.00x | 1.00x | 300% | passed |
| VectorOp2 Vector3 加法 | 0.00 ms / 2500000000 ops/s | 0.37 ms / 2704164 ops/s | 0.05 ms / 19875608 ops/s | 1000 | **92450.0%** | 1.00x | 1.00x | 300% | passed |
| typeof 指令 | 0.00 ms / 3333333333 ops/s | 0.38 ms / 2664535 ops/s | 0.04 ms / 26645350 ops/s | 1000 | **125100.0%** | 1.00x | 1.00x | 1000% | passed |
| QuaternionOp | 0.00 ms / 2000000000 ops/s | 0.75 ms / 1337793 ops/s | 0.10 ms / 9832776 ops/s | 1000 | **149500.0%** | 1.00x | 1.00x | 300% | passed |
| SetTransformPosition | 0.00 ms / 2500000000 ops/s | 1.08 ms / 922424 ops/s | 0.99 ms / 1014667 ops/s | 1000 | **271025.0%** | 1.00x | 1.00x | 300% | passed |
| GameObject Create/Destroy | 0.00 ms / 1666666667 ops/s | 4.14 ms / 241832 ops/s | 3.76 ms / 266015 ops/s | 1000 | **689183.3%** | 1.00x | 1.00x | 300% | passed |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | floor | floor status |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|------:|--------------|
| 协程式 IEnumerator | 0.20 ms / 50403 ops/s | 0.04 ms / 270270 ops/s | 0.01 ms / 775676 ops/s | 10 | 18.6% | 5.36x | 15.39x | observe | observe-only |
| Tween 模拟更新 | 0.06 ms / 170358 ops/s | 0.01 ms / 884956 ops/s | 0.00 ms / 2539823 ops/s | 10 | 19.3% | 5.19x | 14.91x | observe | observe-only |
| List 池化 Rent/Return | 0.12 ms / 83056 ops/s | 0.04 ms / 284091 ops/s | 0.01 ms / 815341 ops/s | 10 | 29.2% | 3.42x | 9.82x | observe | observe-only |
| async/await 热循环 | 0.00 ms / 7692308 ops/s | 0.00 ms / 20000000 ops/s | 0.00 ms / 57400000 ops/s | 10 | 38.5% | 2.60x | 7.46x | observe | observe-only |
| Task.WhenAll | 0.00 ms / 5555556 ops/s | 0.00 ms / 14285714 ops/s | 0.00 ms / 41000000 ops/s | 10 | 38.9% | 2.57x | 7.38x | observe | observe-only |
| 自定义类虚派发 | 0.01 ms / 826446 ops/s | 0.01 ms / 1369863 ops/s | 0.00 ms / 3931507 ops/s | 10 | 60.3% | 1.66x | 4.76x | observe | observe-only |
| Callback 链 | 0.04 ms / 230415 ops/s | 0.03 ms / 296736 ops/s | 0.01 ms / 851632 ops/s | 10 | 77.6% | 1.29x | 3.70x | observe | observe-only |
| 配置表 Dictionary 查找 | 0.03 ms / 291545 ops/s | 0.03 ms / 363636 ops/s | 0.01 ms / 1043636 ops/s | 10 | 80.2% | 1.25x | 3.58x | observe | observe-only |
| Event 多播 | 0.05 ms / 195695 ops/s | 0.04 ms / 239234 ops/s | 0.01 ms / 686603 ops/s | 10 | 81.8% | 1.22x | 3.51x | observe | observe-only |
| Struct 传值与字段更新 | 0.00 ms / 2439024 ops/s | 0.01 ms / 1754386 ops/s | 0.00 ms / 5035088 ops/s | 10 | **139.0%** | 1.00x | 2.06x | observe | observe-only |

## 验收命令

```powershell
cd D:\Code\neko233-Projects\hotc233\unity-hotc233-benchmark\tools
go run ./hotc233ctl generate -project .. -loader-profile RuntimeFast
go run ./hotc233ctl local-benchmark -project .. -loader-profile RuntimeFast -force-rebuild `
  -hybridclr-project ..\unity-hybridclr-ce-benchmark
$env:HOTC233_ENFORCE_PERFORMANCE='1'
$env:HOTC233_ENFORCE_BEAT_COMMUNITY='1'
go run ./hotc233ctl validate-reports -project ..
```
