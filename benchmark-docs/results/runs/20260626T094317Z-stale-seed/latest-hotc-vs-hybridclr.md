# WebGL hotc233 vs HybridCLR 官方基准实测

- 生成时间 (UTC): 2026-06-26T09:43:17.7987167Z
- 口径: 两个独立 Tuanjie 项目、同一套 HybridCLR 官方 performance benchmark 代码形状、WebGL 浏览器 headless 捕获。
- hotc233 工程: `D:/Code/neko233-Projects/unity-hotc233-demo`
- HybridCLR 工程: `D:/Code/Tuanjie-Projects/hybridclr-benchmark-demo`
- 说明: 本地 HybridCLR 8.11.0 WebGL 只作为社区版/基础解释器参照；hotc233 验收目标必须继续追 Pro 标准解释优化目标。
- Pro 目标: 数值/数组类按官方标准解释优化高档倍率估算，`typeof` 按 10x 社区版估算；如果 native WebGL 分母可信，则目标不超过官方 Pro upper tier。

| hotc233 | HybridCLR 本地社区版 | Pro 目标 | 项目 | 次数 | hotc / 社区版 | hotc / Pro 目标 | 追 Pro 还需 | Pro 目标来源 |
|---:|---:|---:|---|---:|---:|---:|---:|---|
| 275.30 ms / 3632401 ops/s | 200.80 ms / 4980082 ops/s | 27.32 ms / 36603604 ops/s | BinOpAdd 简单数值 (`hybridclr-binop-add`) | 1000000 | 72.9% | 9.9% | 10.08x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 547.80 ms / 1825484 ops/s | 290.40 ms / 3443526 ops/s | 39.51 ms / 25309917 ops/s | BinOpComplex 复杂数值 (`hybridclr-binop-complex`) | 1000000 | 53.0% | 7.2% | 13.86x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 302.00 ms / 3311258 ops/s | 257.20 ms / 3888025 ops/s | 34.99 ms / 28576983 ops/s | ArrayOp 数组写读 (`hybridclr-array-op`) | 1000000 | 85.2% | 11.6% | 8.63x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 108.20 ms / 924215 ops/s | 89.30 ms / 1119821 ops/s | 12.15 ms / 8230683 ops/s | VectorOp1 sqrMagnitude (`hybridclr-vector-op1`) | 100000 | 82.5% | 11.2% | 8.91x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 135.30 ms / 739098 ops/s | 108.40 ms / 922510 ops/s | 14.75 ms / 6780449 ops/s | VectorOp2 Vector3 加法 (`hybridclr-vector-op2`) | 100000 | 80.1% | 10.9% | 9.17x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 84.80 ms / 1179247 ops/s | 74.90 ms / 1335112 ops/s | 19.50 ms / 5128205 ops/s | QuaternionOp (`hybridclr-quaternion-op`) | 100000 | 88.3% | 23.0% | 4.35x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter; capped by Pro upper tier/native IL2CPP |
| 113.40 ms / 881834 ops/s | 93.50 ms / 1069519 ops/s | 62.33 ms / 1604278 ops/s | CallAOTStaticMethod (`hybridclr-call-aot-static-method`) | 100000 | 82.5% | 55.0% | 1.82x | hotc233 Pro architecture target: cached direct AOT callsite |
| 144.40 ms / 692521 ops/s | 94.80 ms / 1054852 ops/s | 63.20 ms / 1582278 ops/s | CallAOTInstance ParamInt (`hybridclr-call-aot-instance-param-int`) | 100000 | 65.7% | 43.8% | 2.28x | hotc233 Pro architecture target: cached direct AOT callsite |
| 160.90 ms / 621504 ops/s | 114.80 ms / 871080 ops/s | 15.62 ms / 6402439 ops/s | CallAOTInstance ParamVector3 (`hybridclr-call-aot-instance-param-vector3`) | 100000 | 71.3% | 9.7% | 10.30x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 105.50 ms / 947867 ops/s | 92.10 ms / 1085778 ops/s | 61.40 ms / 1628666 ops/s | CallAOTInstance ReturnInt (`hybridclr-call-aot-instance-return-int`) | 100000 | 87.3% | 58.2% | 1.72x | hotc233 Pro architecture target: cached direct AOT callsite |
| 128.80 ms / 776398 ops/s | 95.20 ms / 1050420 ops/s | 12.95 ms / 7720588 ops/s | CallAOTInstance ReturnVector3 (`hybridclr-call-aot-instance-return-vector3`) | 100000 | 73.9% | 10.1% | 9.94x | HybridCLR Pro standard instruction optimization high tier: 7.35x community/basic interpreter |
| 30.00 ms / 333333 ops/s | 30.70 ms / 325734 ops/s | 32.63 ms / 306466 ops/s | GameObject Create/Destroy (`hybridclr-gameobject-create-destroy`) | 10000 | 102.3% | 108.8% | 0.92x | hotc233 Pro architecture target: keep Unity API boundary above community; capped by Pro upper tier/native IL2CPP |
| 237.10 ms / 421763 ops/s | 202.60 ms / 493584 ops/s | 184.18 ms / 542942 ops/s | SetTransformPosition (`hybridclr-set-transform-position`) | 100000 | 85.4% | 77.7% | 1.29x | hotc233 Pro architecture target: keep Unity API boundary above community |
| 813.50 ms / 1229256 ops/s | 2913.40 ms / 343242 ops/s | 1981.07 ms / 504778 ops/s | typeof 指令 (`hybridclr-typeof`) | 1000000 | 358.1% | 243.5% | 0.41x | HybridCLR Pro typeof target: >=10x community/basic interpreter; capped by Pro upper tier/native IL2CPP |

## 源数据

- hotc233 base report: `D:/Code/neko233-Projects/unity-hotc233-demo/Assets/EditorForBuild/Generated/performance-webgl-base-il2cpp.json`
- HybridCLR WebGL report: `D:/Code/neko233-Projects/unity-hotc233-demo/Assets/EditorForBuild/Generated/hybridclr-webgl-player-report.json`
