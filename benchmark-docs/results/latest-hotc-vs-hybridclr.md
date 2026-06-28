# hotc233 性能报告

- 更新时间 (UTC): 2026-06-28T04:51:04.8801094Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: **已包含**（同机 demo Player 内 Lua 脚本；最后一列 `hotc/xLua` 为相对参照）
- L1 社区版门禁 (官方 14 条): **未通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamInt** = 79.4% (`hybridclr-call-aot-instance-param-int`)
- 官方 base 最强项: **GameObject Create/Destroy** = 177350.0%
- 低于 L1 阈值的 base 行: CallAOTInstance ParamInt, VectorOp1 sqrMagnitude

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamInt | 0.07 ms / 4751067 ops/s | 0.06 ms / 5985663 ops/s | 0.04 ms / 8978495 ops/s | 334 | 79.4% | 1.26x | 1.89x | 1.47 ms / 1136905 ops/s | **417.9%** |
| VectorOp1 sqrMagnitude | 0.04 ms / 8697917 ops/s | 0.03 ms / 10844156 ops/s | 0.00 ms / 79704545 ops/s | 334 | 80.2% | 1.25x | 9.16x | 2.25 ms / 743180 ops/s | **1170.4%** |
| VectorOp2 Vector3 加法 | 0.06 ms / 5259843 ops/s | 0.07 ms / 4751067 ops/s | 0.01 ms / 34920341 ops/s | 334 | **110.7%** | 1.00x | 6.64x | 5.64 ms / 296325 ops/s | **1775.0%** |
| CallAOTInstance ReturnVector3 | 0.04 ms / 8812665 ops/s | 0.04 ms / 7877358 ops/s | 0.01 ms / 57898585 ops/s | 334 | **111.9%** | 1.00x | 6.57x | 4.21 ms / 396637 ops/s | **2221.8%** |
| BinOpComplex 复杂数值 | 0.24 ms / 7052365 ops/s | 0.28 ms / 5957902 ops/s | 0.04 ms / 43790582 ops/s | 1670 | **118.4%** | 1.00x | 6.21x | 1.96 ms / 8507387 ops/s | 82.9% |
| BinOpAdd 简单数值 | 0.17 ms / 9881657 ops/s | 0.21 ms / 7937262 ops/s | 0.03 ms / 58338878 ops/s | 1670 | **124.5%** | 1.00x | 5.90x | 2.63 ms / 6354642 ops/s | **155.5%** |
| CallAOTInstance ReturnInt | 0.02 ms / 16700000 ops/s | 0.03 ms / 12234432 ops/s | 0.02 ms / 18351648 ops/s | 334 | **136.5%** | 1.00x | 1.10x | 0.96 ms / 1739221 ops/s | **960.2%** |
| SetTransformPosition | 0.09 ms / 3808438 ops/s | 0.18 ms / 1817193 ops/s | 0.17 ms / 1998912 ops/s | 334 | **209.6%** | 1.00x | 1.00x | 12.47 ms / 133939 ops/s | **2843.4%** |
| typeof 指令 | 0.13 ms / 13317384 ops/s | 0.39 ms / 4235354 ops/s | 0.04 ms / 42353538 ops/s | 1670 | **314.4%** | 1.00x | 3.18x | 2181.22 ms / 7656 ops/s | **173941.0%** |
| CallAOTInstance ParamVector3 | 0.02 ms / 15904762 ops/s | 0.09 ms / 3897316 ops/s | 0.01 ms / 28645274 ops/s | 334 | **408.1%** | 1.00x | 1.80x | 4.09 ms / 407924 ops/s | **3899.0%** |
| CallAOTStaticMethod | 0.00 ms / 668000000 ops/s | 0.05 ms / 6987448 ops/s | 0.03 ms / 10481172 ops/s | 334 | **9560.0%** | 1.00x | 1.00x | 1.09 ms / 1526369 ops/s | **43764.0%** |
| QuaternionOp | 0.00 ms / 1113333333 ops/s | 0.07 ms / 4519621 ops/s | 0.01 ms / 33219215 ops/s | 334 | **24633.3%** | 1.00x | 1.00x | 1.48 ms / 1127312 ops/s | **98760.0%** |
| ArrayOp 数组写读 | 0.00 ms / 5566666667 ops/s | 0.26 ms / 6505649 ops/s | 0.03 ms / 47816517 ops/s | 1670 | **85566.7%** | 1.00x | 1.00x | 3.18 ms / 5258021 ops/s | **105870.0%** |
| GameObject Create/Destroy | 0.00 ms / 420000000 ops/s | 0.35 ms / 236820 ops/s | 0.32 ms / 260502 ops/s | 84 | **177350.0%** | 1.00x | 1.00x | 0.64 ms / 261469 ops/s | **160631.1%** |

## 验收命令

```powershell
cd D:\Code\neko233-Projects\unity-hotc233-demo\tools
go run ./hotc233ctl generate -project .. -loader-profile RuntimeFast
go run ./hotc233ctl local-benchmark -project .. -loader-profile RuntimeFast -force-rebuild `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo
$env:HOTC233_ENFORCE_BEAT_COMMUNITY='1'
go run ./hotc233ctl validate-reports -project ..
```
