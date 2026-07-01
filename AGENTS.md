# Hotc233 Agent Rules

本文件是 hotc233 包内维护规范。后续自动化代理、编辑器脚本、CI 和人工修改都必须遵守。

## 核心定位

- hotc233 不是 HybridCLR 的安装器外壳。
- hotc233 使用包内内置 `Data~/Libil2cpp` 作为唯一可信运行时来源。
- 项目侧不要求像 HybridCLR 一样执行外部 install 流程；只允许“同步内置运行时到本地工作目录”。
- 用户可见文案使用“内置运行时”“同步”“就绪”，避免使用“安装 HybridCLR”“Install HybridCLR”等语义。

## 落地顺序（2026-06-27 起）

**先商业、后性能。** Pro 对标分两条轨：

1. **商业轨（P0）**：`pro-landing-matrix.json` 中 Hotfix/热重载/加固/访问控制/Assembly 缓存/栈诊断等 — 有/无能力，默认 CI 必须绿。
2. **性能轨（P1+）**：**专用 transform + whole-method bypass**（见 `benchmark-docs/god-domain-architecture.md`）；**禁止** dispatch/M2N 桥接微优化替代专用 IR。

**分层 floor 规则**：`typeof=1000%`；HybridCLR 商业版公开算术项对应行（当前 `hybridclr-binop-add` / `hybridclr-binop-complex`）为 `500%`；其它官方 base 默认 `300%`。低于 100% 说明连 CE 基线都没超过，必须优先排查架构方向。

## 可量化汇报硬规则

- 所有行为必须可量化：运行时、解释器、loader、metadata、ABI、transform、文档、CI、报告字段和 package 接入修改，都必须给出数字口径；禁止只说“已优化”“更快”“更稳”。
- 修改前必须记录包内基线：当前 blocker 数、失败项、opcode profile 摘要、性能最弱行、相关报告路径、命令和关键指标；没有报告时先说明缺口并生成最小可复查基线。
- 修改后必须用同一口径给 before/after：`指标 | 修改前 | 修改后 | 差值 | 结论 | 数据来源`，差值必须写绝对值和百分比（能计算时）。
- 功能与兼容性必须量化：验证项总数、通过数、失败数、blocker 数、异常/崩溃数、超时数、加载的 metadata/assembly 数、热更二进制大小和 hash。
- 性能必须量化：14 条 official base 全量行、hotc/community/Pro 三列、`hotc233PercentOfHybridClr`、Dominance 目标、最弱行排序、profiler 开/关、运行次数、平台和报告文件。
- 解释器/transform 行为必须量化：目标 trace/instinct/fastPathKind 命中次数、通用 dispatch opcode 占比、M2N 热路径出现次数、生成 thunk 数、专用路径与通用路径耗时对比。
- 仓库行为必须量化：改动文件数、关键新增/删除行数、生成文件数、归档文件数、`.meta` 变化数；提交前汇报子仓库与宿主仓库 `git status -sb` 摘要。
- 如果某项无法量化，必须明确写出原因、缺失数据、替代指标和下一步补数命令；未补齐前不得把该项标为完成。
- 向用户汇报必须包含数据来源：本地命令输出、宿主 `Assets/EditorForBuild/Generated/*.json`、包内 `benchmark-docs/results/*.json`、CI 链接或具体日志路径；禁止凭记忆或 stale 报告下结论。

## 架构与性能（商业 P0 落地后再推进，2026-06-27 起）

**GodDomain（专用架构）**：性能主路径不是「把通用 opcode dispatch 做快」，而是 **识别热形状 → 专用 transform → whole-method bypass → 不进 14k 行 switch 循环**。权威说明：`benchmark-docs/god-domain-architecture.md`。旧 dispatch/M2N 桥接路线已归档：`benchmark-docs/archive/generic-dispatch-bridge-retired.md`。

**死而后立**：通用 dispatch 微优化、M2N 桥接热路径、在错误 IR 上修 interp fallback，**永久不得**作为 P1 工作。RegI32 lowering、`PRO_EXPERIMENTAL_TRANSFORM`、threaded dispatch、全局 copy propagation 等同理 **永久废弃**。

### 唯一目标

- Unity/Tuanjie 热更必须能跑、必须稳定；在此前提下 **14 条官方 base 每一行都必须有专用 transform + 专用 execute 入口**（或 Instinct/trace 等价物），全面领先社区版并追平 Pro 纯解释器。
- **专用验收铁律**：同方法专用路径必须 **快于** 通用 dispatch；否则删除该专用实现（假专用）。
- 性能是架构取舍的第一约束。禁止「先通用 lowering 跑通，再在 Execute 里桥接优化」。

### 唯一生产路径

| 层级 | 要求 |
|---|---|
| Loader | 仅 `RuntimeFast`；其它 profile 只作兼容别名 |
| Transform | **`TryBuildGodDomain*` 优先** → Instinct / CallCommon*Cached / trace fold → 最后才 `TransformBodyImpl`（冷路径） |
| Execute | **`TryExecuteHotc233FastPath` 优先** → 仅未识别方法进通用解释循环（**不作 P1 优化对象**） |
| Callsite | transform 期 `AllocAndBakeNativeThunkSlot(methodPointer)`；禁止 M2N `argIdxs` 占 14 base 热路径 |
| 元数据 | FGS + 元数据优化 + Assembly 缓存 |
| 竞品 | 只对标 HybridCLR Pro **纯解释器**；**禁止 DHE** |

### 架构实施顺序（不得跳步）

0. **商业 P0**：Hotfix、热重载、加固等 — `validate-reports` 默认门禁。
1. **14 base 专用 builder**：每行 opcode profile 必须出现 trace/instinct/fastPathKind，不得满屏通用 dispatch opcode。
2. **Instinct 表完整移植**：`TransformContext_Instinct.cpp` 对齐 HybridCLR 社区注册表并扩展 Unity API。
3. **Typed ABI（V3/Quaternion）**：struct-by-value direct invoker，禁止 SetTransform/ParamVector3 走 M2N。
4. **冷路径维护**：通用解释循环仅保正确性；**禁止** threaded dispatch / profiler / 单 case 微优化堆在 switch 上。

### 禁止项（代理/CI/人工均不得引入）

- 以优化 `Interpreter_Execute` 通用 switch 当作 Pro 路线（含 threaded dispatch、热路径 profiler）。
- 在无 `RunStatic*CallTrace` / 无 `fastPathKind` 时提交 execute 层 Cached/thunk/fallback「优化」。
- M2N `CallNativeStatic_ret` / `CallNativeInstance_void` 出现在 CallAOTStatic / SetTransform / ParamInt 等 profile。
- 实验 transform 默认开启；demo business 探针、flywheel 充当性能结论。
- 专用比通用 dispatch 还慢仍保留。

细则：`benchmark-docs/god-domain-architecture.md`、`benchmark-docs/comparison-policy.md`、`benchmark-docs/pro-mechanism-landing.md`、`docs/pro-wrong-answer-notebook.md`（WA-009）。

## HybridCLR Pro 优化参考（权威，勿改）

本节是 **HybridCLR Pro 纯解释器** 的已验证落地 playbook（作者在同路线做出 Pro 的机制归纳）。后续代理 **只许按此实施**，不得再改写成「通用 dispatch 微优化」路线。

**L1 未全绿前禁止 Pro 宣传**；L1 全绿后再按 `docs/pro-landing-matrix.json` 的 Dominance 目标压 Pro 估算。

### 六域机制（对应 `benchmark-docs/pro-mechanism-landing.md`）

| 章 | Pro 机制 | hotc233 等价 | 禁止替代 |
|---|---|---|---|
| §一 | 栈式→寄存器 IR / 超级指令 | GodDomain trace opcode + whole-method bypass | 全局 RegI32 / threaded dispatch |
| §二 | 热更→AOT 直调 / ref-getter / typed struct ABI | `CallCommon*Cached` + `AllocAndBakeNativeThunkSlot` + `TryExecuteHotc233FastPath` | M2N `CallNativeInstance_*` 占热路径 |
| §三 | FGS / 元数据裁剪 / token 缓存 | 内置 il2cpp + `enableFullGenericSharing` + stripper | 外部 HybridCLR install |
| §四 | DHE 差分混合 | **非目标**（纯解释器对标） | 首包 AOT 预置热更代码 |
| §五 | Transform 常量折叠 / peephole | `ApplyCommunityPeepholeFusion` + GodDomain IL 扫描 | 无专用 IR 的 execute 补丁 |
| §六 | EH / async / 静态字段直址 | 沿用 HybridCLR 形态 + Instinct 扩展 | — |

### 超越 Pro 的落地顺序（L1 弱项优先）

1. **SetTransform（2.8%）** — `RunInstanceGetTransformSetV3CallTrace` 必须 fire；ref-getter **Cached ABI + 双 thunk**（§二）。
2. **Quaternion（15.4%）** — Q4 struct return **typed trace**（§二）；ret-only shell + native Unity 调用链 bypass。
3. **ParamInt（84%）** — 循环内 `RunInstanceVoidI4x5CallTrace` + **direct thunk**（§二）；GodDomain shell `selfOff=0`。
4. **ArrayOp / ReturnVector3（91–97%）** — typed array / v3 return trace（§一/§五）。
5. **L1 全绿后** — 按 `pro-landing-matrix.json` Dominance（`typeof` 1000%、`CallAOTStatic` 500% 等）逐项压 Pro 估算。

### 诊断口径

- 弱项先 **opcode profile（profiler ON，仅诊断）** 确认 transform 是否生成目标 trace / `fastPathKind`。
- **L1 验收表** 必须 profiler **OFF**（见 `benchmark-docs/reporting-requirements.md`）。
- 假专用（专用比通用 dispatch 慢）必须删除，不得保留。

## 内置运行时规范

- 包内运行时目录：`Assets/neko233/hotc233-unity/Data~/Libil2cpp/2022-tuanjie`。
- 本地工作目录：`Hotc233Data/LocalIl2CppData-{BuildTarget}/il2cpp`，例如 WebGL 使用 `LocalIl2CppData-WebGL`，Android 使用 `LocalIl2CppData-Android`，iOS 使用 `LocalIl2CppData-iOS`，macOS Standalone 使用 `LocalIl2CppData-StandaloneOSX`。不要再用编辑器平台（如 WindowsEditor）推导目标平台运行时目录。
- 子仓库不提交 Unity `.meta` 文件；`.meta` 由宿主 Unity / Tuanjie 项目本地生成，避免国际版 Unity 与 Tuanjie GUID 格式差异导致仓库反复变脏。
- `Generate/All`、构建前检查、EditorForBuild 自动化必须自动调用 `EnsureBuiltinRuntimeReady()`。
- 不允许因为未手动打开窗口而失败；只有包内运行时缺失或 Unity/Tuanjie 版本不兼容时才失败。
- `UNITY_IL2CPP_PATH` 只指向 hotc233 本地工作目录，不指向 HybridCLR 外部安装目录。

## 测试场景规范

- `Assets/Scenes/main.scene` 是主自动化测试台。
- 主场景必须包含 `Hotc233 Automation Root`，并挂载 `AppRoot` 和 `Hotc233SceneTestHarness`。
- 主场景必须保留 12 个覆盖锚点：
  - Runtime Entry
  - CSharp Basic
  - Generic
  - Interface Dispatch
  - LINQ Aggregate
  - ScriptableObject
  - Timeline
  - Unity API
  - YooAsset Integration
  - Scene Management
  - Platform And Device
  - Performance And Reports
- 修改场景结构后运行 `hotc233/EditorForBuild/Build Main Automation Scene` 重建。

## 验证规范

- 功能验证入口：`UnityHotc.CodeHotUpdate.HotUpdateApp.RunFullVerification`。
- `HotUpdateEntry` 只作为旧入口兼容外壳；新代码、场景、EditorForBuild、Player 探针必须调用 `HotUpdateApp`。
- `AppRoot` 必须先通过 `Hotc233.HotUpdateBinaryLoader` 加载全部 RuntimeMetadata 和 `.dll.bytes`，再通过反射调用 `HotUpdateApp`。
- 反射能力必须由 `ReflectionComprehensiveProbe` 覆盖，并进入 `Hotc233FeatureCatalog`、`HotUpdateVerificationParser` 和 comparison verifier。
- 运行时加载和真机探针必须使用 `Hotc233RuntimeDiagnostics` 输出 session、平台、二进制大小/hash、加载顺序和内层异常。
- 自动化菜单：`hotc233/EditorForBuild/Run Full Verification + Comparison`。
- 平台矩阵菜单：`hotc233/EditorForBuild/Run Platform Matrix Verification`。
- 每次验证必须先清理生成测试状态，再强制 `AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport)`。
- 清理范围至少包含 `Assets/Resources-HotUpdate`、`Assets/EditorForBuild/Generated`、`Library/EditorForBuild/AssetBundles`、`Assets/StreamingAssets/Hotc233Probe`、`Assets/StreamingAssets/yoo/DefaultPackage`。
- 失败反馈必须使用 `Debug.LogError` / `Debug.LogException` 和报告文件；禁止在自动化验证路径使用 `EditorUtility.DisplayDialog` 等阻塞弹窗。
- CLI 推荐入口：
  - `go run ./hotc233ctl all ...`
  - `go run ./hotc233ctl matrix -targets StandaloneWindows64,Android,iOS,WebGL ...`
- 报告索引必须生成：`Assets/EditorForBuild/Generated/report-links.md`。

## 报告规范

- `verification-report.json`：机器可读完整验证结果。
- `comparison-report.json`：本项目 HybridCLR 基线矩阵对比。
- `feature-report.md`：中文功能矩阵。
- `platform-matrix-report.json`：平台构建/验证矩阵。
- **性能对标归档（权威）**：`benchmark-docs/性能报告.md`（人类可读全量 14 行表）；`benchmark-docs/results/latest-hotc-vs-hybridclr.json`（机器可读）。
- 宿主 `Assets/EditorForBuild/Generated/` 仅作当次运行临时产物；结论必须同步到 `benchmark-docs/性能报告.md` 与 `benchmark-docs/results/`。
- 所有菜单、MCP 工具、日志需要输出报告绝对路径或 `benchmark-docs/README.md`。
- **性能汇报**：每次 benchmark 后必须输出 **14 条 base 全量三列对比表**（hotc / 社区 / Pro 估算），并声明 **opcode profiler 开/关**（默认关；见 `benchmark-docs/reporting-requirements.md`）。
- **增量状态报告硬门禁**：每次宿主运行 `validate-reports`、过滤 `local-benchmark` 或完整 `local-benchmark` 后，都必须更新根工作区 `性能现状.md`，写明命令、生成时间、报告路径、失败行、blocker 数、撤回路线和下一步假设；失败结果不得只留在 `Generated/` 或聊天记录里。
- 完整 14 base + 10 business `local-benchmark` 后，必须同时刷新本包 `benchmark-docs/性能报告.md`、`benchmark-docs/results/latest-hotc-vs-hybridclr.json` 和对应 Markdown 摘要；过滤定位只更新增量状态，不得替代完整权威报告。
- release/tag 前若 `性能现状.md` 不是最近一次验证结果，视为性能门禁未完成。

## 性能对标规范（2026-06-27 起）

- **唯一文档入口**：`benchmark-docs/`（见 `benchmark-docs/README.md`）。
- **唯一验收 benchmark 代码**：HybridCLR 官方 14 条 base（`unity-hybridclr-ce-benchmark/OfficialBenchmarkProbe.cs` 形状）；demo `PerformanceProbe` 的 business 行只作为业务风险观察，不得替代官方 base。
- **业务代码性能对比**：`business-realworld-*` 只允许作为追加商业观察/业务门禁，必须使用 hotc233 与 HybridCLR 独立工程同名、同迭代、同平台、同 Player 报告计算百分比；禁止通过增大迭代次数制造稳定读数；缺少社区版同名行时必须判为 blocker。
- **唯一日常验收命令**：宿主 `go run ./tools/hotc233ctl local-benchmark`；顺序探针，禁止 multitask / `-parallel-captures`。
- **同机社区版参照**：同级子仓库 `unity-hybridclr-ce-benchmark`。CE 结果默认只产出一次后复用；缓存缺失或 `HOTC233_REFRESH_HYBRIDCLR_CE=1` 才刷新。
- **Pro 终态**；**分层 floor：typeof 1000%、公开算术项 500%、其它 base 300%**。低于 100% 是 CE 基线 blocker；`business-realworld-*` 发布/tag 默认也必须逐行 `>=100% CE`，诊断 observe-only 不得发布。
- 每行必须输出 `floorPercent`、`floorScope`、`floorSource`、`floorStatus`；`HOTC233_COMMUNITY_NEAR_PERCENT` 只作诊断覆盖，报告必须标注 override。
- **废弃**：`flywheel` 作性能验收、`docs/flywheel-automation.md`、`docs/webgl-performance.md`、`docs/performance-peak-plan.md` 作性能依据。
- 修改 loader、解释器、benchmark 形状或报告字段时，同步更新 `benchmark-docs/` 与两份 `AGENTS.md`。

## HybridCLR 差异处理

- 可复用的 native 命名空间或目录结构不等于用户侧安装语义。
- 底层源码、生成路径、宏和 Editor 内部目录必须使用 `hotc233` / `HOTC233` 命名；`hybridclr` 只允许出现在竞品对比或历史说明文档里。
- DHE 等非目标能力必须在功能矩阵标注为模拟、兼容或待实现，不能宣传为已完整等价；当前性能目标只对标 HybridCLR 专业版纯解释器。
- 为满足无缝替代使用习惯，允许用户可见的 `HybridCLR/...` 菜单别名和 `HybridCLR.RuntimeApi` facade，但它们必须只转发到 hotc233 实现。
- 不允许重新引入官方 `com.code-philosophy.hybridclr` 包依赖，也不允许依赖 HybridCLR Editor/Runtime asmdef 才能编译。
- `hotc233/HybridCLR Compatibility/Import HybridCLR Settings` 必须通过序列化文件或弱反射读取旧配置，导入到 `Hotc233Settings` 后继续使用 hotc233 输出目录；只有显式 Mirror Output Paths 时才镜像旧输出目录。
- 性能报告的 HybridCLR 专业版列只能标注为官方 8.11.0 文档目标区间，不能冒充本地 HybridCLR 包实测。
- 专业版目标不得依赖 DHE，不得通过把大量热更逻辑放回首包 AOT 来规避解释器成本。

## RuntimeFast 与 minigame 健康检查

- `HotUpdateBinaryLoader` 默认且唯一生产 profile 为 `RuntimeFast`；`RuntimeOptions` 与其它 profile 名只作兼容别名，**不得**在热路径增加更慢分支。
- **WebGL 性能验收**只作专项，日常生产性能验收走 `benchmark-docs/methodology.md` 中的 `hotc233ctl local-benchmark`。
- 微信小游戏/minigame(WebGL2) 热更必须关闭 `weixinMiniGameUseSlimMetaFileFormat` 和全部 `m_SlimFeaturesWeixinMiniGame`。
- 只读报告守门：`go run ./hotc233ctl validate-reports -project <demo>`；本机 `local-benchmark` 负责分层 floor 对比。
- 修改解释器 opcode 时，必须同步 `Instruction.h`、`Instruction.cpp`、`Interpreter_Execute.cpp`、transform 选择逻辑和宿主 `hotc233ctl validate-reports` 的指令表守门；**P1 只允许改专用 transform / `TryExecuteHotc233FastPath`**，禁止以优化通用 dispatch switch 充当性能工作。
- **禁止**为开发便利在 release 解释器保留 profiler、多 dispatch 模式或实验 opcode；SSA/多核编译等仅能在 L1 全绿且 callsite 架构落地后再立项。
- 修改 loader、解释器、transform 策略、报告字段或配置导入器时，同步更新 `benchmark-docs/`、两份 `AGENTS.md` 和 CI 守门。

## 包结构与复制接入

- `hotc233-unity` 必须保持可复制到任意宿主项目直接编译；不要依赖 `unity-hotc233-benchmark` 的 EditorForBuild、场景或生成目录。
- 包内 asmdef 分层目标为 `Runtime`、`Editor`、`Plugin_xxx`：运行时代码只在 `Runtime`，编辑器生成/分析代码只在 `Editor`，第三方预编译依赖必须放在明确命名的 `Plugin_dnlib`、`Plugin_LZ4` 等目录。
- 调整 `Plugin_xxx/` 时必须验证 `Hotc233.Editor.asmdef`、Unity plugin import settings、CI 和 package export；不得让 Runtime asmdef 引用 dnlib/LZ4。
- 复制接入习惯必须贴近 HybridCLR 社区版：用户配置热更 asmdef、AOT metadata、执行 Generate/All、发布 `.dll.bytes`，但不要求执行外部 install。

## 修改守则

- 不要把宿主项目生成的 `.meta` 强行加入子仓库；如果必须新增序列化资产，需要先说明为什么它需要稳定 GUID。
- 新增功能必须进入 `Hotc233FeatureCatalog` 和报告解析器。
- 新增热更资源必须进入 `HotUpdateResourceCatalog`，并由 `Hotc233TestResourceBuilder` 自动创建。
- 新增场景覆盖项必须更新 `Hotc233SceneTestHarness` 和 `Hotc233MainSceneBuilder`。
- 修改验证输出 token 时，必须同步更新 `HotUpdateVerificationParser`、报告 writer、comparison verifier。
