# hotc233 性能报告

- 更新时间 (UTC): 2026-07-01T03:26:28.0827291Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- Floor: `typeof=1000%`；HybridCLR 商业版公开算术项对应行 `500%`；其它官方 base 默认 `300%`；business 默认观察。
- 业务场景: **已包含** `business-realworld-*`（`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 可仅跑官方 14 条）
- 分层 floor 门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2273.9% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 689183.3%
- 业务场景最弱项: **Tween 模拟更新** = 18.6%
- 业务场景最强项: **Struct 传值与字段更新** = 126.7%

## 生产机制生效性

- 状态: **通过**，P0 商业能力、metadata、低 GC/内存稳定哨兵均有当前证据。
- 商业能力探针: 通过
- Metadata 优化: 通过，节省 51.5%，2601472 -> 1262080 bytes，load 10.96 ms，peak heap delta 0 bytes。
- Loader 机制: standard-interpreter=通过，offline-instruction=通过，assembly-load=通过，metadata-saving=51.5%，metadata-load=10.96 ms。
- 内存/GC 哨兵: 通过，low-gc=通过，memory-stability=通过，retainedDelta=0 bytes。
- Tween 策略: observe-only，不做特殊 Tween 优化；优先覆盖通用解释器、容器、字符串、状态机与 async 路径。

| 关注项 | 当前数据 | 来源 | 状态 |
|---|---:|---|---|
| List pool | 29.9% vs HybridCLR CE | local-benchmark business | observe-only |
| Dictionary lookup | 82.6% vs HybridCLR CE | local-benchmark business | observe-only |
| Coroutine | 29.6% vs HybridCLR CE | local-benchmark business | observe-only |
| Async-await | 38.5% vs HybridCLR CE | local-benchmark business | observe-only |
| Task.WhenAll | 38.9% vs HybridCLR CE | local-benchmark business | observe-only |
| String | 0.2182 ms / 2291476 ops/s | P0 performance-report.json | snapshot |
| Collections | 1.6757 ms / 298383 ops/s | P0 performance-report.json | snapshot |
| Config lookup | 0.0647 ms / 7727975 ops/s | P0 performance-report.json | snapshot |

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | floor | floor status |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|------:|--------------|
| CallAOTInstance ParamVector3 | 0.01 ms / 108695652 ops/s | 0.21 ms / 4780115 ops/s | 0.03 ms / 35133843 ops/s | 1000 | **2273.9%** | 1.00x | 1.00x | 300% | passed |
| BinOpAdd 简单数值 | 0.00 ms / 909090909 ops/s | 0.12 ms / 8474576 ops/s | 0.02 ms / 62288136 ops/s | 1000 | **10727.3%** | 1.00x | 1.00x | 500% | passed |
| VectorOp1 sqrMagnitude | 0.00 ms / 1666666667 ops/s | 0.08 ms / 12970169 ops/s | 0.01 ms / 95330739 ops/s | 1000 | **12850.0%** | 1.00x | 1.00x | 300% | passed |
| ArrayOp 数组写读 | 0.00 ms / 1111111111 ops/s | 0.14 ms / 6920415 ops/s | 0.02 ms / 50865052 ops/s | 1000 | **16055.6%** | 1.00x | 1.00x | 300% | passed |
| BinOpComplex 复杂数值 | 0.00 ms / 1666666667 ops/s | 0.15 ms / 6657790 ops/s | 0.02 ms / 48934754 ops/s | 1000 | **25033.3%** | 1.00x | 1.00x | 500% | passed |
| CallAOTInstance ReturnInt | 0.00 ms / 3333333333 ops/s | 0.08 ms / 13210040 ops/s | 0.05 ms / 19815059 ops/s | 1000 | **25233.3%** | 1.00x | 1.00x | 300% | passed |
| CallAOTInstance ReturnVector3 | 0.00 ms / 3333333333 ops/s | 0.13 ms / 7905138 ops/s | 0.02 ms / 58102767 ops/s | 1000 | **42166.7%** | 1.00x | 1.00x | 300% | passed |
| CallAOTInstance ParamInt | 0.00 ms / 3333333333 ops/s | 0.14 ms / 7251632 ops/s | 0.09 ms / 10877447 ops/s | 1000 | **45966.7%** | 1.00x | 1.00x | 300% | passed |
| VectorOp2 Vector3 加法 | 0.00 ms / 2500000000 ops/s | 0.37 ms / 2704164 ops/s | 0.05 ms / 19875608 ops/s | 1000 | **92450.0%** | 1.00x | 1.00x | 300% | passed |
| typeof 指令 | 0.00 ms / 2500000000 ops/s | 0.38 ms / 2664535 ops/s | 0.04 ms / 26645350 ops/s | 1000 | **93825.0%** | 1.00x | 1.00x | 1000% | passed |
| CallAOTStaticMethod | 0.00 ms / 2500000000 ops/s | 0.38 ms / 2602811 ops/s | 0.26 ms / 3904217 ops/s | 1000 | **96050.0%** | 1.00x | 1.00x | 300% | passed |
| QuaternionOp | 0.00 ms / 2000000000 ops/s | 0.75 ms / 1337793 ops/s | 0.10 ms / 9832776 ops/s | 1000 | **149500.0%** | 1.00x | 1.00x | 300% | passed |
| SetTransformPosition | 0.00 ms / 2000000000 ops/s | 1.08 ms / 922424 ops/s | 0.99 ms / 1014667 ops/s | 1000 | **216820.0%** | 1.00x | 1.00x | 300% | passed |
| GameObject Create/Destroy | 0.00 ms / 1666666667 ops/s | 4.14 ms / 241832 ops/s | 3.76 ms / 266015 ops/s | 1000 | **689183.3%** | 1.00x | 1.00x | 300% | passed |

## 实际业务热更场景

覆盖: 自定义 class 虚派发、struct 传值、callback 链、async/await、Task.WhenAll、IEnumerator 协程式步进、DOTween 式 lerp、event 多播、配置表查找、List 池化。

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | floor | floor status |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|------:|--------------|
| Tween 模拟更新 | 0.06 ms / 164204 ops/s | 0.01 ms / 884956 ops/s | 0.00 ms / 2539823 ops/s | 10 | 18.6% | 5.39x | 15.47x | observe | observe-only |
| Callback 链 | 0.16 ms / 62383 ops/s | 0.03 ms / 296736 ops/s | 0.01 ms / 851632 ops/s | 10 | 21.0% | 4.76x | 13.65x | observe | observe-only |
| 协程式 IEnumerator | 0.13 ms / 79872 ops/s | 0.04 ms / 270270 ops/s | 0.01 ms / 775676 ops/s | 10 | 29.6% | 3.38x | 9.71x | observe | observe-only |
| List 池化 Rent/Return | 0.12 ms / 84890 ops/s | 0.04 ms / 284091 ops/s | 0.01 ms / 815341 ops/s | 10 | 29.9% | 3.35x | 9.60x | observe | observe-only |
| async/await 热循环 | 0.00 ms / 7692308 ops/s | 0.00 ms / 20000000 ops/s | 0.00 ms / 57400000 ops/s | 10 | 38.5% | 2.60x | 7.46x | observe | observe-only |
| Task.WhenAll | 0.00 ms / 5555556 ops/s | 0.00 ms / 14285714 ops/s | 0.00 ms / 41000000 ops/s | 10 | 38.9% | 2.57x | 7.38x | observe | observe-only |
| 自定义类虚派发 | 0.01 ms / 746269 ops/s | 0.01 ms / 1369863 ops/s | 0.00 ms / 3931507 ops/s | 10 | 54.5% | 1.84x | 5.27x | observe | observe-only |
| Event 多播 | 0.05 ms / 196464 ops/s | 0.04 ms / 239234 ops/s | 0.01 ms / 686603 ops/s | 10 | 82.1% | 1.22x | 3.49x | observe | observe-only |
| 配置表 Dictionary 查找 | 0.03 ms / 300300 ops/s | 0.03 ms / 363636 ops/s | 0.01 ms / 1043636 ops/s | 10 | 82.6% | 1.21x | 3.48x | observe | observe-only |
| Struct 传值与字段更新 | 0.00 ms / 2222222 ops/s | 0.01 ms / 1754386 ops/s | 0.00 ms / 5035088 ops/s | 10 | **126.7%** | 1.00x | 2.27x | observe | observe-only |

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
