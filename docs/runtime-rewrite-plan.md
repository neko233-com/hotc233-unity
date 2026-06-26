# hotc233 Runtime 重构计划

## 目标口径

hotc233 的竞品目标只有 HybridCLR Pro 纯解释能力。HybridCLR 社区版只作为本机参考测量点，不作为产品目标。性能验收按公开 Pro 区间分层：

| 档位 | 口径 |
|---|---:|
| Pro floor | 约 7.8% native IL2CPP |
| Pro upper tier | 约 76.9% native IL2CPP |
| 社区版参考 | 只作本机 sanity check，不作为目标 |

WebGL/minigame 报告分两层看：

| 层级 | 用途 |
|---|---|
| 本机 HybridCLR 社区版 | 判断本机 benchmark 环境和公开数据是否一致，不作为竞品目标 |
| Pro 架构目标 | 按 HybridCLR Pro 已公开的标准解释优化、typeof 优化、AOT 调用边界收益估算，决定下一步架构重写方向 |

不能只拿 Pro floor 宣称成功。数值、数组、typeof 这类 Pro 已验证能大幅提升的项目，必须继续追 Pro 架构目标；Unity API 边界类项目只作为回归守门。

## 优化决策规则

每次解释器优化都必须跑 WebGL RuntimeFast 报告，并在提交或交接说明中列出全部操作性能表。表格最左列必须是 hotc233 operation，至少包含本轮 ms、相对上轮、native IL2CPP 百分比、Pro floor、HybridCLR 社区版上限和 Pro 上限差距。

专项优化只看未达 Pro floor 的操作；已经达到 Pro floor 的操作只做回归监控，除非优化来自同一条通用路径且风险很低。

| 状态 | 处理 |
|---|---|
| `hotc233PercentOfWebGLIl2Cpp >= 7.6%` | 不再专项优化，保留回归监控 |
| `hotc233PercentOfWebGLIl2Cpp < 7.6%` | 允许专项优化，但必须是通用解释器/元数据/调用边界能力 |
| 连续 2 次同一方向无收益或回退 | 停止该方向，记录失败原因 |
| 只提升单一 benchmark 且业务行回退 | 立即撤回 |

当前已达 Pro floor 的业务操作：LINQ 链式查询、反射查询、字符串处理、集合操作、异常抛接。后续不得把这些项作为主要优化目标。

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

当前同机 WebGL base 结果：

| hotc233 operation | hotc / HybridCLR 社区版 | hotc / Pro 架构目标 | 追 Pro 还需 | 下一步 |
|---|---:|---:|---:|---|
| hybridclr-binop-complex | 53.0% | 7.2% | 13.86x | typed numeric IR |
| hybridclr-call-aot-instance-param-vector3 | 71.3% | 9.7% | 10.30x | typed ABI + callsite cache |
| hybridclr-binop-add | 72.9% | 9.9% | 10.08x | typed numeric IR |
| hybridclr-call-aot-instance-return-vector3 | 73.9% | 10.1% | 9.94x | typed ABI + callsite cache |
| hybridclr-vector-op2 | 80.1% | 10.9% | 9.17x | typed numeric IR |
| hybridclr-vector-op1 | 82.5% | 11.2% | 8.91x | typed numeric IR |
| hybridclr-array-op | 85.2% | 11.6% | 8.63x | typed array quickening |
| hybridclr-quaternion-op | 88.3% | 23.0% | 4.35x | typed numeric IR |
| hybridclr-call-aot-instance-param-int | 65.7% | 43.8% | 2.28x | direct AOT invoker |
| hybridclr-call-aot-static-method | 82.5% | 55.0% | 1.82x | direct static callsite |
| hybridclr-typeof | 358.1% | 243.5% | 0.41x | 已超过目标，保留回归监控 |

结论：`typeof` 已通过 transform 阶段 Type 对象缓存超过本机 HybridCLR 社区版和当前 Pro 目标；base 总体仍未成功，数值/Vector/数组最差仍有 4.35x-13.86x Pro 架构差距。下一阶段必须集中到 typed numeric IR、typed ABI callsite 和 typed array quickening。

## HybridCLR / il2cpp_plus 机制落地状态

hotc233 不是只做 C# wrapper。包内 `Data~/Libil2cpp/2022-tuanjie` 已经包含一套 hotc233 版 libil2cpp 源码补丁，覆盖解释入口、方法指针桥接、AOT 元数据补充和 Unity 2022+ 泛型共享接入：

| 机制 | hotc233 现状 | 生产判断 |
|---|---|---|
| 解释器接入 IL2CPP 方法表 | `Il2CppClass`/`MethodInfo` 增加 `isInterpterImpl`、`interpData`、`methodPointerCallByInterp`、`virtualMethodPointerCallByInterp` | WebGL 本地 IL2CPP 已验证能解释；微信小游戏真机不属于本轮验收 |
| AOT metadata 补充 | `Hotc233.RuntimeApi.LoadMetadataForAOTAssembly`、`AOTHomologousImage`、`Consistent/SuperSetAOTHomologousImage` | 可用，但还没有 Pro 级 10%-25% 元数据内存节省实测 |
| 解释器桥接 stub | `InterpreterModule`、`MethodBridge`、`Managed2Native`、`ReversePInvoke` 生成路径 | 可用，但 typed ABI 覆盖和 direct invoker 仍是性能缺口 |
| Unity 2022+ 泛型共享 | `metadata/GenericSharing.*`、`GenericMethod.cpp`、`enableFullGenericSharing` 配置入口 | 有机制，不等于 HybridCLR Pro 完全泛型共享等价；必须补泛型矩阵 |
| 真机打包风险 | WebGL browser IL2CPP direct `.dll.bytes` 已跑通；Android/iOS/微信小游戏真机不是本轮短验收 | 本轮只以 WebGL/headless 直接 `.dll.bytes` 和报告门禁验证 |

因此答案是：类似 HybridCLR/il2cpp_plus 的核心插入点已经有一版，不是“打包后完全无法解释”的空壳；但它还没完成 HybridCLR Pro 等价矩阵，不能把当前状态宣传成 Pro 全能力落地。

## 优先级调整

当前顺序固定为：先补齐商业能力硬门禁，再做解释器性能。`RunFullVerification` 必须包含 `CommercialCapabilityProbe`，AB/Player 验证必须写出 `commercialCapabilityProbe` 结构化字段；`validate-reports` 会要求商业兼容能力表中非解释器能力全绿。`标准解释优化` 与 `离线指令优化` 属于最后阶段，不能因为 `RuntimeFast` 开关存在就标记为完成。

指令集合并现在降级为 P3。除非它来自 typed IR 后端自然 lowering，否则不再优先新增单个 benchmark 形状 opcode。接下来的 P0 是商业能力验证与报告门禁：

| 优先级 | 模块 | 为什么先做 |
|---|---|---|
| P0 | 商业能力结构化探针 | full generic sharing surface、metadata policy、hotfix/reload、code protection、access control、assembly cache、crash log 必须先可验证 |
| P0 | 能力报告硬门禁 | `feature-report.md` 不能把未测能力写成通过；只按 HybridCLR Pro 纯解释目标验收 |
| P1 | typed register IR | 当前 `BinOp*`、Vector、Quaternion 最差仍差 Pro 4.35x-13.86x，继续栈机 copy/push/pop 没机会追上 |
| P1 | typed ABI callsite | AOT static/instance、Vector3 参数/返回仍明显落后；必须减少通用 `Managed2Native` argIdx/stub 分派 |
| P1 | typed array memory IR | 单条数组 fusion 命中但仍差 Pro 8.63x，需要缓存元素大小、地址策略和边界策略 |
| P2 | metadata/code-size memory report | WebGL/minigame 内存敏感，不能只报速度；要补 metadata size、peak heap、load time |
| P3 | opcode fusion | 只在 typed IR 确认后做后端 lowering，不再作为主优化路线 |

## 必须重构的模块

| 模块 | 目标 |
|---|---|
| register IR | 从 IL 栈机统一转成 typed register IR，解释器执行 typed slot，不再反复 push/pop/copy StackData |
| dispatch/lowering | WebGL 使用 switch 仍保留兼容路径；RuntimeFast 先建立 typed IR，再由后端 lowering 到 quickened opcode。不要先按 benchmark 形状堆 opcode |
| metadata resolve | 在 transform 阶段预解析 method/type/field/string/token，执行期不再重复查元数据；`typeof` 走 token->Il2CppClass 直接缓存 |
| static/direct call | 对可确定目标生成 call site cache：AOT direct thunk、解释器 direct entry、签名适配器分层，避免每次通用解析 |
| instance call | 缓存 this/签名/返回值 copy 策略，值类型参数和 Vector3/Quaternion 走 typed ABI，不进通用 object/metadata 分派 |
| delegate/lambda | 在 transform 阶段识别单播闭包、静态 lambda、实例 lambda、multicast；常见单播解释方法直接准备 this+args 并跳解释入口 |
| array/numeric | 数组元素地址、长度、元素类型在 typed IR 阶段缓存；i4/r4/vector 先消除栈机中间态，再考虑 fused typed op |
| exception/debug frame | 每次解释器进入/退出方法维护轻量帧，用于异常栈、日志和真机调试 |
| memory model | 指令优化、元数据压缩、代码段缩减分开做，WebGL 内存不得用大量缓存换吞吐 |

## 能力现状

| HybridCLR Pro 能力 | hotc233 当前状态 | 接力判断 |
|---|---|---|
| 完全泛型共享 | 有 Unity 2022+ il2cpp GenericSharing 路径和配置入口，但尚未完成 HybridCLR Pro 等价矩阵 | 不能宣称生产等价，先补值类型泛型、泛型虚调用、委托、接口、嵌套泛型测试 |
| 元数据优化 | 有加载策略、完整性校验和 minigame 安全配置；缺少 10%-25% 内存节省的同机报告 | 先做 metadata size/peak heap/loading time 三表 |
| 标准解释优化 | 有 RuntimeFast、局部 fused opcode、call fast path；base 数值/Vector/数组仍差 Pro 4.35x-13.86x | 进入 typed register IR / typed ABI / typed array memory 重构 |
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
| `i4[]` 读元素 + 局部值 + 写回数组 fused opcode | 新 opcode 命中 10,010,000 次；ArrayOp 仍只有本机 HybridCLR 社区版 91.1%，对 Pro 目标还差 8.07x | 保留，但只作为 typed array quickening 的第一步 |
| `mul_i4 -> sub_i4` numeric fused opcode | BinOpComplex 单项一度提升约 8.1%，但 BinOpAdd、AOT call、Unity API 多项大幅回退 | 撤回；该路线按规则停止，改做 typed numeric IR |
| `ldtoken + Type.GetTypeFromHandle` transform 缓存 Type 对象 | 新 opcode `LdtokenTypeObjectVar` 命中 8,008,001 次；`typeof` 从 3834.7 ms 降到 813.5 ms，达到本机 HybridCLR 社区版 358.1%、Pro 目标 243.5% | 保留；metadata token cache 进入回归监控 |
| 闭包 `mul const + field + add` 方法 fastpath | `delegate-lambda` 从 63.7 ms 降到 45.7 ms，约 +39% 吞吐；只识别通用闭包 IR 形状，不按方法名特化 | 保留；但瓶颈转移到 delegate invoke 分发 |
| 单播解释 delegate invoke inline cache | `delegate-lambda` 进一步到 21.3-22.1 ms，`任务委托派发`多轮有 15%-25% 提升；每个调用点增加 16B-24B resolve cache | 保留；继续补静态 lambda、实例 lambda、multicast 分层 |
| `CallInterpStatic_ret` callee IMI cache | `小方法调用`从 79.6 ms 到 72.8 ms，约 +9.3%；不增大指令尺寸，仅复用 padding 增加 1 个 resolve slot | 保留；仍需 direct static callsite/typed ABI 才能过 Pro floor |
| fastpath 方法 class-init 状态位 | 试图每个解释方法缓存 class-init 状态；WebGL 多项系统性回退，`小方法调用 -17.2%`、`任务委托派发 -15.8%` | 撤回；该方向记 1 次失败，不再扩大方法结构状态缓存 |
| `RunI4AddCopyTrace` 连续 add+copy 热路径 trace | 识别 `BinOpVarVarVar_Add_i4_LdlocVarVar_LdlocVarVar_LdlocVarVar` 连续 run（>=4）；WebGL 浏览器探针可完成，局部 numeric 热循环 dispatch 下降 | 保留；仅限已验证形状，不再扩展为通用 linear trace |
| `RunStaticF4CallTrace` 连续 static f4 call trace | 识别同一 `CallCommonNativeStatic_f4_0` 连续 run（>=3）；WebGL 浏览器探针可完成，static call 局部 dispatch 下降 | 保留；仍需 typed ABI/direct callsite 才能过 Pro floor |
| `RunI4LinearTrace` 通用 i4 linear trace v4 | Tuanjie WebGL 构建通过，但浏览器 CDP 等 `HOTC233_PERFORMANCE_REPORT_JSON` marker 超时（691s）；主线程卡死/极慢，非报告抓取噪音 | 撤回；不再做 benchmark 形状的通用 linear trace，改走 typed register IR |

## 架构优先性能迭代循环

性能差距主要来自解释器架构（栈机 dispatch、元数据 resolve、AOT 调用边界），不是 Tuanjie 编译耗时。迭代必须分层，禁止跳过下层直接跑完整 WebGL：

| 层级 | 命令 | 耗时量级 | 用途 |
|---|---|---:|---|
| L0 | `go test ./tools/hotc233ctl/...` + `validate-reports` | 秒级 | opcode 表、RuntimeFast/minigame 报告完整性 |
| L1 | `headless` | 秒级 | 工具编译、指令表、benchmark 覆盖、只读性能 JSON 完整性 |
| L2 | `quick` | 秒级 | 只读 base JSON，按架构桶排序缺口；**不**打 Unity/WebGL |
| L3 | `webgl` | 约 80–95s（正常） | 唯一 IL2CPP 浏览器实测入口；必须 `RuntimeFast` |
| L4 | `hybridclr-webgl` | 更长 | 同机 HybridCLR 8.11.0 base 对照 |
| L5 | `perf` / full verification | 最长 | base 改善后再跑业务行与 Unity 特性 |

决策规则：

| 信号 | 动作 |
|---|---|
| L0/L1 失败 | 禁止进入 L3；先修表/报告/配置 |
| `quick` 报告 `reportStale=true` 或最近一次 `webgl` result JSON 为 `failed` | 禁止用旧 JSON 做收益判断；先重跑 L3 |
| WebGL 浏览器 marker 超时或 CDP 无响应 | 立即记失败并撤回本轮架构改动；不要继续等满 timeout |
| 连续 2 次同方向无接近 Pro 架构目标 | 停止该路线，转 typed register IR / typed ABI / typed array memory |
| 新 trace/fusion opcode | 只允许窄形状（已验证 run-length 模式）；禁止通用 linear trace |

保留的 trace lowering：`RunI4AddCopyTrace`、`RunStaticF4CallTrace`。已禁止：通用 `RunI4LinearTrace` 及同类“把任意 i4 栈片段压成 interpreter 内 switch 循环”的路线。

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
