# hotc233 性能报告

- 更新时间 (UTC): 2026-06-28T13:14:20.8352629Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player · RuntimeFast · warmup=0
- Loader profile: `RuntimeFast`
- 业务场景: 未包含（仅官方 base）
- xLua 对照: **已包含**（同机 demo Player 内 Lua 脚本；最后一列 `hotc/xLua` 为相对参照）
- L1 社区版门禁 (官方 14 条): **通过**
- 机器可读归档: [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)

> 本文件由 `hotc233ctl local-benchmark` 自动生成；禁止手工只改部分行。

## 自动摘要

- 官方 base 最弱项: **CallAOTInstance ParamVector3** = 2410.3% (`hybridclr-call-aot-instance-param-vector3`)
- 官方 base 最强项: **GameObject Create/Destroy** = 224266.7%

## 官方 14 条 HybridCLR base

| 项目 | hotc233 | HybridCLR 社区版 | Pro 目标估算 | 次数 | hotc / HybridCLR | 追平社区还需 | 追 Pro 还需 | xLua | hotc / xLua |
|------|---------|------------------|--------------|-----:|-----------------:|-------------:|------------:|-----:|------------:|
| CallAOTInstance ParamVector3 | 0.00 ms / 85641026 ops/s | 0.09 ms / 3553191 ops/s | 0.01 ms / 26115957 ops/s | 334 | **2410.3%** | 1.00x | 1.00x | 2.41 ms / 692745 ops/s | **12362.6%** |
| QuaternionOp | 0.00 ms / 371111111 ops/s | 0.08 ms / 4360313 ops/s | 0.01 ms / 32048303 ops/s | 334 | **8511.1%** | 1.00x | 1.00x | 1.83 ms / 912170 ops/s | **40684.4%** |
| CallAOTInstance ReturnInt | 0.00 ms / 1113333333 ops/s | 0.03 ms / 12370370 ops/s | 0.02 ms / 18555556 ops/s | 334 | **9000.0%** | 1.00x | 1.00x | 0.96 ms / 1742851 ops/s | **63880.0%** |
| VectorOp1 sqrMagnitude | 0.00 ms / 1113333333 ops/s | 0.03 ms / 10151976 ops/s | 0.00 ms / 74617021 ops/s | 334 | **10966.7%** | 1.00x | 1.00x | 1.41 ms / 1184481 ops/s | **93993.3%** |
| CallAOTInstance ReturnVector3 | 0.00 ms / 1113333333 ops/s | 0.04 ms / 8186275 ops/s | 0.01 ms / 60169118 ops/s | 334 | **13600.0%** | 1.00x | 1.00x | 3.54 ms / 472379 ops/s | **235686.7%** |
| CallAOTStaticMethod | 0.00 ms / 1113333333 ops/s | 0.05 ms / 7076271 ops/s | 0.03 ms / 10614407 ops/s | 334 | **15733.3%** | 1.00x | 1.00x | 1.07 ms / 1555804 ops/s | **71560.0%** |
| ArrayOp 数组写读 | 0.00 ms / 1518181818 ops/s | 0.25 ms / 6750202 ops/s | 0.03 ms / 49613985 ops/s | 1670 | **22490.9%** | 1.00x | 1.00x | 1.57 ms / 10603848 ops/s | **14317.3%** |
| VectorOp2 Vector3 加法 | 0.00 ms / 1113333333 ops/s | 0.07 ms / 4737589 ops/s | 0.01 ms / 34821277 ops/s | 334 | **23500.0%** | 1.00x | 1.00x | 4.84 ms / 345148 ops/s | **322566.7%** |
| CallAOTInstance ParamInt | 0.00 ms / 1670000000 ops/s | 0.05 ms / 6290019 ops/s | 0.04 ms / 9435028 ops/s | 334 | **26550.0%** | 1.00x | 1.00x | 1.28 ms / 1301028 ops/s | **128360.0%** |
| BinOpAdd 简单数值 | 0.00 ms / 2783333333 ops/s | 0.20 ms / 8442872 ops/s | 0.03 ms / 62055106 ops/s | 1670 | **32966.7%** | 1.00x | 1.00x | 1.83 ms / 9116218 ops/s | **30531.7%** |
| SetTransformPosition | 0.00 ms / 1113333333 ops/s | 0.19 ms / 1769068 ops/s | 0.17 ms / 1945975 ops/s | 334 | **62933.3%** | 1.00x | 1.00x | 8.63 ms / 193403 ops/s | **575653.3%** |
| BinOpComplex 复杂数值 | 0.00 ms / 4175000000 ops/s | 0.27 ms / 6273479 ops/s | 0.04 ms / 46110068 ops/s | 1670 | **66550.0%** | 1.00x | 1.00x | 1.42 ms / 11737419 ops/s | **35570.0%** |
| typeof 指令 | 0.00 ms / 8350000000 ops/s | 0.36 ms / 4667412 ops/s | 0.04 ms / 46674120 ops/s | 1670 | **178900.0%** | 1.00x | 1.00x | 1971.68 ms / 8470 ops/s | **98583775.0%** |
| GameObject Create/Destroy | 0.00 ms / 280000000 ops/s | 0.67 ms / 124851 ops/s | 0.61 ms / 137337 ops/s | 84 | **224266.7%** | 1.00x | 1.00x | 1.29 ms / 129117 ops/s | **216857.5%** |

## 验收命令

```powershell
cd D:\Code\neko233-Projects\unity-hotc233-demo\tools
go run ./hotc233ctl generate -project .. -loader-profile RuntimeFast
go run ./hotc233ctl local-benchmark -project .. -loader-profile RuntimeFast -force-rebuild `
  -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo
$env:HOTC233_ENFORCE_BEAT_COMMUNITY='1'
go run ./hotc233ctl validate-reports -project ..
```
