# Local hotc233 vs HybridCLR 官方基准实测

- 生成时间 (UTC): 2026-06-27T17:29:02.3008819Z
- 口径: 本机 StandaloneWindows64 IL2CPP Player；不构建 WebGL、不启动本地 server、不启动 headless browser。
- 范围: 只看 HybridCLR 官方 14 条 base benchmark；业务行不参与此门禁。
- hotc233 工程: `D:/Code/neko233-Projects/unity-hotc233-demo`
- HybridCLR 工程: `D:/Code/Tuanjie-Projects/hybridclr-benchmark-demo`
- 本机阶段门禁: 默认 `hotc / HybridCLR >= 98%`（`HOTC233_COMMUNITY_NEAR_PERCENT` 可改；`HOTC233_ENFORCE_BEAT_COMMUNITY=1` 时仍为 100%）。
- 结论: 通过本机 L1。

| hotc233 | HybridCLR 本机社区版 | Pro 目标估算 | 项目 | 次数 | hotc / HybridCLR | 追平 HybridCLR 还需 | 追 Pro 还需 |
|---:|---:|---:|---|---:|---:|---:|---:|
| 0.04 ms / 8520408 ops/s | 0.04 ms / 8226601 ops/s | 0.01 ms / 60465517 ops/s | CallAOTInstance ReturnVector3 (`hybridclr-call-aot-instance-return-vector3`) | 334 | 103.6% | 1.00x | 7.10x |
| 0.05 ms / 6185185 ops/s | 0.06 ms / 5719178 ops/s | 0.04 ms / 8578767 ops/s | CallAOTInstance ParamInt (`hybridclr-call-aot-instance-param-int`) | 334 | 108.1% | 1.00x | 1.39x |
| 0.17 ms / 10108959 ops/s | 0.19 ms / 8949625 ops/s | 0.03 ms / 65779743 ops/s | BinOpAdd 简单数值 (`hybridclr-binop-add`) | 1670 | 113.0% | 1.00x | 6.51x |
| 0.06 ms / 6061706 ops/s | 0.07 ms / 4651811 ops/s | 0.01 ms / 34190808 ops/s | VectorOp2 Vector3 加法 (`hybridclr-vector-op2`) | 334 | 130.3% | 1.00x | 5.64x |
| 0.02 ms / 13632653 ops/s | 0.03 ms / 10151976 ops/s | 0.00 ms / 74617021 ops/s | VectorOp1 sqrMagnitude (`hybridclr-vector-op1`) | 334 | 134.3% | 1.00x | 5.47x |
| 0.02 ms / 17216495 ops/s | 0.03 ms / 12796935 ops/s | 0.02 ms / 19195402 ops/s | CallAOTInstance ReturnInt (`hybridclr-call-aot-instance-return-int`) | 334 | 134.5% | 1.00x | 1.11x |
| 0.18 ms / 9262341 ops/s | 0.25 ms / 6777597 ops/s | 0.03 ms / 49815341 ops/s | BinOpComplex 复杂数值 (`hybridclr-binop-complex`) | 1670 | 136.7% | 1.00x | 5.38x |
| 0.08 ms / 4206549 ops/s | 0.18 ms / 1902050 ops/s | 0.16 ms / 2092255 ops/s | SetTransformPosition (`hybridclr-set-transform-position`) | 334 | 221.2% | 1.00x | 1.00x |
| 0.19 ms / 449438 ops/s | 0.44 ms / 189317 ops/s | 0.40 ms / 208249 ops/s | GameObject Create/Destroy (`hybridclr-gameobject-create-destroy`) | 84 | 237.4% | 1.00x | 1.00x |
| 0.12 ms / 13621533 ops/s | 0.34 ms / 4895925 ops/s | 0.03 ms / 48959249 ops/s | typeof 指令 (`hybridclr-typeof`) | 1670 | 278.2% | 1.00x | 3.59x |
| 0.01 ms / 23356643 ops/s | 0.08 ms / 3947991 ops/s | 0.01 ms / 29017730 ops/s | CallAOTInstance ParamVector3 (`hybridclr-call-aot-instance-param-vector3`) | 334 | 591.6% | 1.00x | 1.24x |
| 0.00 ms / 556666667 ops/s | 0.06 ms / 5493421 ops/s | 0.04 ms / 8240132 ops/s | CallAOTStaticMethod (`hybridclr-call-aot-static-method`) | 334 | 10133.3% | 1.00x | 1.00x |
| 0.00 ms / 1113333333 ops/s | 0.08 ms / 4206549 ops/s | 0.01 ms / 30918136 ops/s | QuaternionOp (`hybridclr-quaternion-op`) | 334 | 26466.7% | 1.00x | 1.00x |
| 0.00 ms / 8350000000 ops/s | 0.17 ms / 9840896 ops/s | 0.02 ms / 72330583 ops/s | ArrayOp 数组写读 (`hybridclr-array-op`) | 1670 | 84850.0% | 1.00x | 1.00x |

## 源数据

- hotc233 local report: `D:/Code/neko233-Projects/unity-hotc233-demo/Assets/EditorForBuild/Generated/performance-hotc233-player.json`
- HybridCLR local report: `D:/Code/neko233-Projects/unity-hotc233-demo/Assets/EditorForBuild/Generated/hybridclr-local-player-report.json`
