# hotc233 与 HybridCLR 社区版 / 专业版差距说明

更新时间：2026-06-26  
最低支持：Unity / Tuanjie 2022+  
当前性能口径：`RuntimeFast`、PC IL2CPP Player、热更 C# 经 hotc 转换后执行，原生 IL2CPP = 100%。

## 结论

| 问题 | 当前结论 |
|------|----------|
| 专业版能力是否全部生产等价落地 | 没有。hotc233 已有兼容入口、RuntimeFast、加载策略、热修复入口、报告链路和部分解释器快路径，但完全泛型共享等价、元数据内存优化、生产级加密、解释器栈诊断和 WebGL/minigame 真机内存表仍需补齐。 |
| 性能是否达到 HybridCLR 专业版上限 | 没有。当前 18 项 PC IL2CPP 用例中，4 项机制用例达到或超过专业版纯解释目标上限；游戏业务热循环尚无一项达到上限。 |
| 是否可以说整体超过专业版 | 不能。部分用例达到专业版下限，核心业务热路径仍低于下限。 |
| 是否可以说超过社区版 | 暂不能作为实测结论。本仓库没有安装官方社区版并跑同机同工程对照，只能按官方倍率做粗推。 |

## 对标口径

HybridCLR 官方商业能力包括完全泛型共享、元数据优化、标准解释优化、Hotfix、Assembly.Load 优化、解释器栈诊断、代码加固和访问控制等。hotc233 当前明确排除 DHE，不通过把大量热更逻辑预置进首包 AOT 来换性能。

| 口径 | 以原生 IL2CPP = 100% |
|------|----------------------|
| HybridCLR 专业版纯解释目标下限 | 7.8% |
| HybridCLR 专业版纯解释目标上限 | 76.9% |
| HybridCLR 社区版粗略推算区间 | 约 1.1% - 26.8%，仅由官方倍率反推，非本地实测 |

参考：

- https://www.hybridclr.cn/docs/business
- https://www.hybridclr.cn/docs/business/basicoptimization
- https://www.hybridclr.cn/docs/business/fullgenericsharing
- https://www.hybridclr.cn/docs/business/metadataoptimization

## 当前性能分档

数据来自 demo 工程生成的 `Assets/EditorForBuild/Generated/performance-comparison-report.json`，生成时间为 2026-06-26。

| 分档 | 数量 | 用例 |
|------|------|------|
| 低于专业版下限 | 7 | 小方法调用、闭包委托、战斗 Tick、背包筛选汇总、任务直接派发、技能目标选择、状态序列化 |
| 达到专业版下限但低于上限 | 7 | 泛型实例化、LINQ 链式查询、任务委托派发、Buff 系统 Tick、配置表查找、资源 Manifest 查找、低 GC 帧循环 |
| 达到或超过专业版上限 | 4 | 反射查询、字符串处理、集合操作、异常抛接 |
| 游戏业务热循环达到上限 | 0 | 仍需继续优化 |

## 优先优化项

| 优先级 | 项目 | 目标 |
|--------|------|------|
| P0 | static 热更方法调用 | 降低解释方法调解释方法的 frame 准备、实例检查和分发成本；`CallInterpStatic_void` 已作为通用 fast path 落地。 |
| P0 | delegate / lambda | 优化单播闭包 lambda：单播、解释方法、闭包 target、参数数相同的常见路径直接准备 this 并进入解释执行。 |
| P0 | 状态序列化 | 降低 boxing、字符串/缓冲区搬运、虚调用和对象访问成本。 |
| P0 | WebGL/minigame 内存 | 建立峰值内存、常驻内存、GC alloc/frame、加载时间表。 |
| P1 | 完全泛型共享验证矩阵 | 覆盖值类型泛型、引用类型泛型、泛型虚调用、接口、委托和容器。 |
| P1 | 元数据优化实测矩阵 | 统计补充元数据大小、热更程序集大小、加载峰值和延迟初始化内存。 |

## 发布口径

推荐对外口径：

> hotc233 RuntimeFast 已在部分机制用例达到或超过 HybridCLR 专业版纯解释目标上限，部分业务热循环达到专业版下限；但整体尚未达到专业版上限，核心游戏热路径、WebGL/minigame 内存与 GC、完全泛型共享等价、元数据优化和生产级加固仍在补齐。

不推荐口径：

- “专业版能力已全部落地”
- “整体性能已经超过 HybridCLR 专业版”
- “本地实测超过 HybridCLR 社区版”

