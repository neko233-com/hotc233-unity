# hotc233 Runtime 重构计划

## 目标口径

hotc233 的下限不是 HybridCLR 社区版，而是 HybridCLR Pro 纯解释能力。性能验收按公开 Pro 区间分层：

| 档位 | 口径 |
|---|---:|
| Pro floor | 约 7.8% native IL2CPP |
| Pro upper tier | 约 76.9% native IL2CPP |
| 社区版参考 | 约 1.1% - 26.8% native IL2CPP |

WebGL/minigame 报告不再用 Pro upper tier 统一卡所有项；硬门禁先按 Pro floor。Pro upper tier 作为优秀档和后续专项优化目标。

## 优化决策规则

每次解释器优化都必须跑 WebGL RuntimeFast 报告，并在提交或交接说明中列出全部操作性能表。表格最左列必须是 hotc233 operation，至少包含本轮 ms、相对上轮、native IL2CPP 百分比、Pro floor、HybridCLR 社区版上限和 Pro 上限差距。

专项优化只看未达 Pro floor 的操作；已经达到 Pro floor 的操作只做回归监控，除非优化来自同一条通用路径且风险很低。

| 状态 | 处理 |
|---|---|
| `hotc233PercentOfWebGLIl2Cpp >= 7.6%` | 不再专项优化，保留回归监控 |
| `hotc233PercentOfWebGLIl2Cpp < 7.6%` | 允许专项优化，但必须是通用解释器/元数据/调用边界能力 |
| 连续 2 次同一方向无收益或回退 | 停止该方向，记录失败原因 |
| 只提升单一 benchmark 且业务行回退 | 立即撤回 |

当前已达 Pro floor 的操作：LINQ 链式查询、反射查询、字符串处理、集合操作、异常抛接。后续不得把这些项作为主要优化目标。

当前未达 Pro floor 的核心缺口：static 方法调用、泛型实例化、闭包委托、战斗 Tick、背包筛选汇总、任务直接派发、任务委托派发、Buff Tick、技能目标选择、配置表查找、资源 Manifest 查找、状态序列化、低 GC 帧循环、内存稳定哨兵。

## Headless 对标

`hotc233ctl hybridclr-ref` 会在主工程生成独立 headless 对标目录：

```text
External/HybridCLRReference/repos/
```

该目录只用于本地下载官方公开源码，不提交第三方仓库内容。报告输出：

```text
Assets/EditorForBuild/Generated/hybridclr-reference-report.md
Assets/EditorForBuild/Generated/hybridclr-reference-report.html
Assets/EditorForBuild/Generated/hybridclr-reference-report.json
```

当前锁定参考：

| 仓库 | 分支 |
|---|---|
| focus-creative-games/hybridclr | main |
| focus-creative-games/il2cpp_plus | 2022-main-tuanjie |

## 2026-06-26 独立 HybridCLR WebGL 实测

已新增第二个独立 Tuanjie 项目：

```text
D:/Code/Tuanjie-Projects/hybridclr-benchmark-demo
```

该项目固定 `com.code-philosophy.hybridclr` 8.11.0，使用同一套 HybridCLR 官方 performance benchmark 代码形状。命令：

```powershell
cd D:\Code\neko233-Projects\unity-hotc233-demo\tools
go run .\hotc233ctl hybridclr-webgl -project .. -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo -loader-profile RuntimeFast -timeout 60m
```

报告：

```text
Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.md
Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.html
Assets/EditorForBuild/Generated/performance-webgl-hotc-vs-hybridclr-base.json
```

当前同机 WebGL 结果：

| hotc233 operation | hotc / HybridCLR 本地实测 | 需要提升 |
|---|---:|---:|
| hybridclr-binop-add | 82.2% | 1.22x |
| hybridclr-binop-complex | 54.6% | 1.83x |
| hybridclr-array-op | 50.6% | 1.98x |
| hybridclr-vector-op1 | 87.0% | 1.15x |
| hybridclr-vector-op2 | 81.7% | 1.22x |
| hybridclr-quaternion-op | 75.8% | 1.32x |
| hybridclr-call-aot-static-method | 79.8% | 1.25x |
| hybridclr-call-aot-instance-param-int | 69.2% | 1.45x |
| hybridclr-call-aot-instance-param-vector3 | 75.3% | 1.33x |
| hybridclr-call-aot-instance-return-int | 85.0% | 1.18x |
| hybridclr-call-aot-instance-return-vector3 | 78.6% | 1.27x |
| hybridclr-gameobject-create-destroy | 111.7% | 0.89x |
| hybridclr-set-transform-position | 85.0% | 1.18x |
| hybridclr-typeof | 87.9% | 1.14x |

结论：当前 hotc233 并非差 10x，但官方基准中数值复杂运算、数组访问和 AOT instance call 仍明显落后 HybridCLR 8.11.0 本地实测。下一阶段不能继续靠零散 opcode 补丁，必须改解释器执行形态。

## 必须重构的模块

| 模块 | 目标 |
|---|---|
| register IR | 从 IL 栈机统一转成 typed register IR，解释器执行 typed slot，不再反复 push/pop/copy StackData |
| direct-threaded dispatch | WebGL 使用 switch 仍保留兼容路径，但 RuntimeFast 需要 quickened opcode + computed/direct dispatch 等价设计；每个 opcode 只做已解析数据访问 |
| metadata resolve | 在 transform 阶段预解析 method/type/field/string/token，执行期不再重复查元数据；`typeof` 走 token->Il2CppClass 直接缓存 |
| static/direct call | 对可确定目标生成 call site cache：AOT direct thunk、解释器 direct entry、签名适配器分层，避免每次通用解析 |
| instance call | 缓存 this/签名/返回值 copy 策略，值类型参数和 Vector3/Quaternion 走 typed ABI，不进通用 object/metadata 分派 |
| delegate/lambda | 在 transform 阶段识别单播闭包、静态 lambda、实例 lambda、multicast；常见单播解释方法直接准备 this+args 并跳解释入口 |
| array/numeric | 数组元素地址、长度、元素类型在 quicken 阶段缓存；i4/r4/vector 常见循环使用 fused typed op，减少边界检查和中间拷贝 |
| exception/debug frame | 每次解释器进入/退出方法维护轻量帧，用于异常栈、日志和真机调试 |
| memory model | 指令优化、元数据压缩、代码段缩减分开做，WebGL 内存不得用大量缓存换吞吐 |

## 能力现状

| HybridCLR Pro 能力 | hotc233 当前状态 | 接力判断 |
|---|---|---|
| 完全泛型共享 | 有 Unity 2022+ il2cpp GenericSharing 路径和配置入口，但尚未完成 HybridCLR Pro 等价矩阵 | 不能宣称生产等价，先补值类型泛型、泛型虚调用、委托、接口、嵌套泛型测试 |
| 元数据优化 | 有加载策略、完整性校验和 minigame 安全配置；缺少 10%-25% 内存节省的同机报告 | 先做 metadata size/peak heap/loading time 三表 |
| 标准解释优化 | 有 RuntimeFast、局部 fused opcode、call fast path；官方基准仍落后 HybridCLR 8.11.0 1.14x-1.98x | 进入 register IR/quickened call site 重构 |
| Hotfix | 有 loader 替换入口和 HybridCLR facade 方向 | 继续补包内示例和回滚验证 |
| 代码加固/加密 | 有 policy/hash/解密入口和 XOR 示例，不是生产级安全方案 | 接入 AES/自定义 provider 和篡改测试 |
| 解释器栈诊断 | 已有 `GetInterpreterStackTraceJson` API 和 native frame 导出 | 异常路径、PDB 行号和 WebGL 真机日志继续补齐 |

## 已验证尝试

| 尝试 | 结果 | 决策 |
|---|---|---|
| call boundary direct leaf opcode | static method 有局部提升，但 WebGL 总体和业务行回退 | 撤回，不再按同一路线新增 call opcode |
| 全局 OIO copy propagation | WebGL 总体明显回退，执行次数上升 | 撤回，不再全局启用 |
| InitInlineLocals tail dispatch | 总体小幅提升，但 delegate/lambda 明显回退 | 撤回 |
| copy+ldind 值转发 | WebGL 总体约 3.1% 提升，opcode 总数下降 | 保留 |
| `Add_i4+copy` 与 `copy+copy` 复用大指令融合 | delegate 局部提升，但多数业务行回退，最弱项更差 | 撤回 |

## Pro 内存口径

HybridCLR Pro 公开/业务口径需要同时纳入报告：

| 项目 | 口径 |
|---|---:|
| 指令优化模块额外占用 | 约 700K |
| 每解释线程额外占用 | 约 1.2M |
| 元数据内存优化 | 约 10% - 25% |
| 热更程序集二进制代码段缩减 | 约 5.2x |

因此 hotc233 的 WebGL/minigame 路线必须同时追求性能和低内存，不能只提高指令吞吐。

## 栈帧能力

`Hotc233.RuntimeApi.GetInterpreterStackTraceJson(int maxFrames)` 和 `HybridCLR.RuntimeApi.GetInterpreterStackTraceJson(int maxFrames)` 已作为稳定 API 暴露。native 端会通过解释器 `MachineState::CollectFrames` 导出当前线程解释器帧：

```json
{"success":true,"frameTracking":true,"totalFrames":1,"frames":[{"index":0,"method":"Game.HotUpdate::Tick","rawIp":"0x...","ilOffset":42,"line":128,"filePath":"Assets/HotUpdate/Game.cs"}]}
```

该 API 用于 WebGL 真机报错定位和与 HybridCLR Pro 调试体验对齐。后续异常路径必须继续复用同一套 frame push/pop 与 PDB 映射，保留 hot update 方法名、IL offset/native pc、文件行号信息。
