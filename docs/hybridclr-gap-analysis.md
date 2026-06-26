# hotc233 与 HybridCLR 社区版 / 专业版差距说明

更新时间：2026-06-26  
最低支持：Unity / Tuanjie 2022+  
当前性能口径：`RuntimeFast`、WebGL 浏览器本地 IL2CPP、直接加载产出的 `.dll.bytes` / RuntimeMetadata，原生 WebGL IL2CPP = 100%。不统计 AssetLib233、YooAsset、HTTP 下载和打包链路。

## 结论

| 问题 | 当前结论 |
|------|----------|
| 专业版能力是否全部生产等价落地 | 没有。hotc233 已有兼容入口、RuntimeFast、加载策略、热修复入口、报告链路和部分解释器快路径，但完全泛型共享等价、元数据内存优化、生产级加密、解释器栈诊断和 WebGL/minigame 真机内存表仍需补齐。 |
| 性能是否达到 HybridCLR 专业版上限 | 没有。当前目标改为 hotc233 下限 = HybridCLR Pro 纯解释上限；WebGL 热更业务热路径仍显著低于这个目标。 |
| 是否可以说整体超过专业版 | 不能。当前报告必须以 Pro 上限为达标线，而不是专业版下限。 |
| 是否可以说超过社区版 | 暂不能作为实测结论。本仓库没有安装官方社区版并跑同机同工程对照，只能按官方倍率做粗推。 |

## 同机 HybridCLR 8.11.0 实测

2026-06-26 已建立独立项目 `D:/Code/Tuanjie-Projects/hybridclr-benchmark-demo`，固定 HybridCLR 8.11.0 embedded package 和 `HybridCLRData/LocalIl2CppData-WindowsEditor` 安装缓存。当前报告来自两个独立 Tuanjie 项目、同一套 HybridCLR 官方 performance benchmark 代码形状、WebGL 浏览器 headless 捕获。

| hotc233 | HybridCLR 本地实测 | 结论 |
|---|---:|---|
| 官方基准最低相对项 | 50.6% | `hybridclr-array-op`，需要 1.98x |
| 官方基准最高相对项 | 111.7% | `hybridclr-gameobject-create-destroy`，已超过 |
| static AOT call | 79.8% | 需要 1.25x，单点优化空间有限，必须做 call site cache |
| typeof | 87.9% | 需要 1.14x，但相对原生仍只有 45.9% |
| 数值复杂运算 | 54.6% | 需要 1.83x，说明 typed register IR/quickened numeric op 是 P0 |
| 数组写读 | 50.6% | 需要 1.98x，说明数组地址/类型/边界快路径是 P0 |

完整表：

- `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.md`
- `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.html`
- `Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.json`

## 对标口径

HybridCLR 官方商业能力包括完全泛型共享、元数据优化、标准解释优化、Hotfix、Assembly.Load 优化、解释器栈诊断、代码加固和访问控制等。hotc233 当前明确排除 DHE，不通过把大量热更逻辑预置进首包 AOT 来换性能。

| 口径 | 以原生 IL2CPP = 100% |
|------|----------------------|
| HybridCLR 专业版纯解释目标下限 | 7.8%，仅保留为参考 |
| hotc233 当前达标线：HybridCLR 专业版纯解释上限 | 76.9% |
| HybridCLR 社区版粗略推算区间 | 约 1.1% - 26.8%，仅由官方倍率反推，非本地实测 |

参考：

- https://www.hybridclr.cn/docs/business
- https://www.hybridclr.cn/docs/business/basicoptimization
- https://www.hybridclr.cn/docs/business/fullgenericsharing
- https://www.hybridclr.cn/docs/business/metadataoptimization

## 当前性能分档

数据来自 demo 工程生成的 `Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.json`，生成时间为 2026-06-26。每次优化后，该报告会读取上一份 WebGL JSON 并输出 `previousHotc233ElapsedMs` 与 `hotc233ImprovementPercentFromPrevious`。

| 分档 | 当前结论 |
|------|----------|
| 达到 HybridCLR 社区版上限 | 只作为折算参考，不能宣传为同机实测 |
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
| P0 | static 热更方法调用 | 降低解释方法调解释方法的 frame 准备、实例检查和分发成本；`CallInterpStatic_void` 与 `CallInterpStatic_ret` 已作为通用 fast path 落地。 |
| P0 | delegate / lambda | 优化单播闭包 lambda：单播、解释方法、闭包 target、参数数相同的常见路径直接准备 this 并进入解释执行。 |
| P0 | 状态序列化 | 降低 boxing、字符串/缓冲区搬运、虚调用和对象访问成本。 |
| P0 | WebGL/minigame 内存 | `game-low-gc-frame` 与 `game-memory-stability` 已进入 PC/WebGL 原始 JSON 门禁，并强制 `retainedDelta <= 65536` bytes；下一步补微信小游戏真机峰值内存、常驻内存和加载时间表。 |
| P1 | 完全泛型共享验证矩阵 | 覆盖值类型泛型、引用类型泛型、泛型虚调用、接口、委托和容器。 |
| P1 | 元数据优化实测矩阵 | 统计补充元数据大小、热更程序集大小、加载峰值和延迟初始化内存。 |

## 发布口径

推荐对外口径：

> hotc233 RuntimeFast 已落地 WebGL 直接 `.dll.bytes` 性能报告、上一轮优化对比、HybridCLR 社区版折算参考、Pro 上限目标线以及 GC/retained heap 门禁；但整体尚未达到 Pro 上限，核心游戏热路径、WebGL/minigame 真机内存表、完全泛型共享等价、元数据优化和生产级加固仍在补齐。

不推荐口径：

- “专业版能力已全部落地”
- “整体性能已经超过 HybridCLR 专业版”
- “本地实测超过 HybridCLR 社区版”
