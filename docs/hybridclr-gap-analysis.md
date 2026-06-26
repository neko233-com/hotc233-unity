# hotc233 与 HybridCLR Pro 纯解释差距说明

更新时间：2026-06-26  
最低支持：Unity / Tuanjie 2022+  
当前性能口径：`RuntimeFast`、WebGL 浏览器本地 IL2CPP、直接加载产出的 `.dll.bytes` / RuntimeMetadata，原生 WebGL IL2CPP = 100%。不统计 AssetLib233、YooAsset、HTTP 下载和打包链路。

## 结论

| 问题 | 当前结论 |
|------|----------|
| Pro 纯解释能力是否全部生产等价落地 | 没有。hotc233 已有兼容入口、RuntimeFast、加载策略、热修复入口、报告链路和部分解释器快路径；当前正在补商业能力硬门禁。 |
| 性能是否达到 HybridCLR 专业版上限 | 没有。当前目标改为 hotc233 下限 = HybridCLR Pro 纯解释上限；WebGL 热更业务热路径仍显著低于这个目标。 |
| 是否可以说整体超过专业版 | 不能。当前报告必须以 Pro 上限为达标线，而不是专业版下限。 |
| 社区版是否是目标 | 不是。独立 HybridCLR 8.11.0 WebGL 同机表只作 benchmark 环境校准；竞品目标只有 HybridCLR Pro 纯解释。 |

## 同机 HybridCLR 8.11.0 参考实测

2026-06-26 已建立独立项目 `D:/Code/Tuanjie-Projects/hybridclr-benchmark-demo`，固定 HybridCLR 8.11.0 embedded package 和 `HybridCLRData/LocalIl2CppData-WindowsEditor` 安装缓存。当前报告来自两个独立 Tuanjie 项目、同一套 HybridCLR 官方 performance benchmark 代码形状、WebGL 浏览器 headless 捕获。

| hotc233 | HybridCLR 本地实测 | 结论 |
|---|---:|---|
| 官方基准最低相对项 | 53.0% | `hybridclr-binop-complex`，需要 1.89x 才到本机社区版 |
| 官方基准最高相对项 | 358.1% | `hybridclr-typeof`，已超过本机社区版和当前 Pro 目标 |
| static AOT call | 82.5% | 仍需 typed ABI callsite cache |
| typeof | 358.1% | `ldtoken + Type.GetTypeFromHandle` transform 缓存已生效，进入回归监控 |
| 数值复杂运算 | 53.0% | 需要 typed register IR；单条 opcode fusion 已证明不是主路线 |
| 数组写读 | 85.2% | 有 `i4[]` 局部 fusion，但仍需 typed array memory IR |

完整表：

- `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.md`
- `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.html`
- `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.json`

## 对标口径

HybridCLR Pro 纯解释相关能力包括完全泛型共享、元数据优化、标准解释优化、Hotfix、Assembly.Load 优化、解释器栈诊断、代码加固和访问控制等。hotc233 不通过把大量热更逻辑预置进首包 AOT 来换性能。

| 口径 | 以原生 IL2CPP = 100% |
|------|----------------------|
| HybridCLR 专业版纯解释目标下限 | 7.8%，仅保留为参考 |
| hotc233 当前达标线：HybridCLR 专业版纯解释上限 | 76.9% |
| HybridCLR 8.11.0 社区版本机表 | 只作 sanity check，不作为竞品目标 |

参考：

- https://www.hybridclr.cn/docs/business
- https://www.hybridclr.cn/docs/business/basicoptimization
- https://www.hybridclr.cn/docs/business/fullgenericsharing
- https://www.hybridclr.cn/docs/business/metadataoptimization

## 当前性能分档

数据来自 demo 工程生成的 `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.json`，生成时间为 2026-06-26。每次优化后，该报告会读取上一份 WebGL JSON 并输出 `previousHotc233ElapsedMs` 与 `hotc233ImprovementPercentFromPrevious`。

| 分档 | 当前结论 |
|------|----------|
| 本机 HybridCLR 8.11.0 表 | 只用于校准 benchmark 环境；不能作为成功目标 |
| 达到 hotc233 目标线（Pro 上限） | 当前 WebGL 游戏业务热循环尚未达到 |
| 连续优化策略 | 同一类微优化连续 2 次 WebGL 实测未接近 Pro 上限或出现倒退，就停止该路线 |

## GC 与内存门禁

`validate-reports` 当前以 WebGL hotc233、WebGL 原生 IL2CPP raw snapshot 为硬门禁；PC snapshot 如果存在只作为附带展示。当前 2026-06-26 实测：

| 平台/后端 | 低 GC 帧循环 | 内存稳定哨兵 |
|-----------|--------------|--------------|
| WebGL hotc233 RuntimeFast | `gc0=0,gc1=0,gc2=0` | `gc0=0,gc1=0,gc2=0,retainedDelta=0` |
| WebGL 原生 IL2CPP | `gc0=0,gc1=0,gc2=0` | `gc0=0,gc1=0,gc2=0,retainedDelta=0` |

这说明当前新增的热循环内存泄漏哨兵没有发现常驻托管堆增长；但 `game-memory-stability` 性能仍低于 hotc233 目标线，不能视为性能达标。

## 优先优化项

| 优先级 | 项目 | 目标 |
|--------|------|------|
| P0 | 商业能力硬门禁 | `CommercialCapabilityProbe`、`commercialCapabilityProbe` JSON、`feature-report.md` 必须证明非解释器商业能力全绿。 |
| P0 | WebGL/headless 内存 | `game-low-gc-frame` 与 `game-memory-stability` 已进入 PC/WebGL 原始 JSON 门禁，并强制 `retainedDelta <= 65536` bytes；本轮不要求微信小游戏真机验证。 |
| P1 | typed register IR | 优先解决 BinOp、Vector、Quaternion 的栈机 copy/push/pop 成本；不要再优先堆 benchmark 形状 opcode。 |
| P1 | typed ABI callsite | static/instance AOT call、Vector3 参数/返回都必须减少通用 argIdx/stub 分派。 |
| P1 | typed array memory IR | 数组元素大小、地址、边界策略需要 transform 阶段缓存；局部 fused opcode 只是第一步。 |
| P2 | 元数据优化实测矩阵 | 统计补充元数据大小、热更程序集大小、加载峰值和延迟初始化内存。 |

## 发布口径

推荐对外口径：

> hotc233 RuntimeFast 已落地 WebGL 直接 `.dll.bytes` 性能报告、上一轮优化对比、独立 HybridCLR 8.11.0 WebGL 同机参考表、Pro 架构目标线以及 GC/retained heap 门禁；但整体尚未达到 Pro 目标，商业能力硬门禁、typed register IR、typed ABI、完全泛型共享等价、元数据优化和生产级加固仍在补齐。

不推荐口径：

- “专业版能力已全部落地”
- “整体性能已经超过 HybridCLR 专业版”
- “整体本地实测达到 HybridCLR Pro”
