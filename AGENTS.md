# Hotc233 Agent Rules

本文件是 hotc233 包内维护规范。后续自动化代理、编辑器脚本、CI 和人工修改都必须遵守。

## 核心定位

- hotc233 不是 HybridCLR 的安装器外壳。
- hotc233 使用包内内置 `Data~/Libil2cpp` 作为唯一可信运行时来源。
- 项目侧不要求像 HybridCLR 一样执行外部 install 流程；只允许“同步内置运行时到本地工作目录”。
- 用户可见文案使用“内置运行时”“同步”“就绪”，避免使用“安装 HybridCLR”“Install HybridCLR”等语义。

## 内置运行时规范

- 包内运行时目录：`Assets/neko233/hotc233-unity/Data~/Libil2cpp/2022-tuanjie`。
- 本地工作目录：`Hotc233Data/LocalIl2CppData-{Application.platform}/il2cpp`。
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
- **性能对标归档（权威）**：`benchmark-docs/results/latest-hotc-vs-hybridclr.json` / `.md`。
- 宿主 `Assets/EditorForBuild/Generated/` 仅作当次运行临时产物；结论必须同步到 `benchmark-docs/results/`。
- 所有菜单、MCP 工具、日志需要输出报告绝对路径或 `benchmark-docs/README.md`。

## 性能对标规范（2026-06-27 起）

- **唯一文档入口**：`benchmark-docs/`（见 `benchmark-docs/README.md`）。
- **唯一验收 benchmark 代码**：HybridCLR 官方 14 条 base（`hybridclr-benchmark-demo/OfficialBenchmarkProbe.cs` 形状）；demo `PerformanceProbe` 的 business 行不得进入 Pro/社区版硬验收。
- **唯一验收命令**：宿主 `go run ./tools/hotc233ctl benchmark`；顺序探针，禁止 multitask / `-parallel-captures`。
- **同机社区版参照**：`D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo`（HybridCLR 8.11.0 + Tuanjie 2022.3.62t10）；测试环境与该工程完全一致（WebGL IL2CPP + 相同探针链路）。
- **Pro 终态**；**L1 绝对门槛：同机 14 条 base 必须全面快于 HybridCLR 社区版**（fork 基线，任一条未赢即方向错误）。
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

- `HotUpdateBinaryLoader` 默认 profile 必须是 `RuntimeFast`；`RuntimeOptions` 只作为兼容别名。
- **WebGL 性能验收**走 `benchmark-docs/methodology.md` 中的 `hotc233ctl benchmark`；必须生成并归档 `benchmark-docs/results/latest-hotc-vs-hybridclr.*`。
- 微信小游戏/minigame(WebGL2) 热更必须关闭 `weixinMiniGameUseSlimMetaFileFormat` 和全部 `m_SlimFeaturesWeixinMiniGame`。
- 只读报告守门：`go run ./hotc233ctl validate-reports -project <demo>`；`HOTC233_ENFORCE_BEAT_COMMUNITY=1` 启用 L1（全面超越社区版）。
- 修改解释器 opcode 时，必须同步 `Instruction.h`、`Instruction.cpp`、`Interpreter_Execute.cpp`、transform 选择逻辑和宿主 `hotc233ctl validate-reports` 的指令表守门；RuntimeFast 热路径至少覆盖 `System.Math.Min/Max` signed int/long intrinsic 和 20/24/28/32 字节无引用 struct array store。
- 超越 HybridCLR 专业版纯解释器的路线必须按稳定性优先推进：SSA、常量折叠、热路径 profiling、多核增量编译、低/无 GC 生成都需要先通过 WebGL/PC 回归和性能表，再扩大平台范围。
- 修改 loader、性能菜单、AssetLib233/minigame 对接、报告字段或配置导入器时，同步更新 README、宿主 demo 文档、AGENTS 和 CI 守门。

## 包结构与复制接入

- `hotc233-unity` 必须保持可复制到任意宿主项目直接编译；不要依赖 `unity-hotc233-demo` 的 EditorForBuild、场景或生成目录。
- 包内 asmdef 分层目标为 `Runtime`、`Editor`、`Plugin_xxx`：运行时代码只在 `Runtime`，编辑器生成/分析代码只在 `Editor`，第三方预编译依赖必须放在明确命名的 `Plugin_dnlib`、`Plugin_LZ4` 等目录。
- 调整 `Plugin_xxx/` 时必须验证 `Hotc233.Editor.asmdef`、Unity plugin import settings、CI 和 package export；不得让 Runtime asmdef 引用 dnlib/LZ4。
- 复制接入习惯必须贴近 HybridCLR 社区版：用户配置热更 asmdef、AOT metadata、执行 Generate/All、发布 `.dll.bytes`，但不要求执行外部 install。

## 修改守则

- 不要把宿主项目生成的 `.meta` 强行加入子仓库；如果必须新增序列化资产，需要先说明为什么它需要稳定 GUID。
- 新增功能必须进入 `Hotc233FeatureCatalog` 和报告解析器。
- 新增热更资源必须进入 `HotUpdateResourceCatalog`，并由 `Hotc233TestResourceBuilder` 自动创建。
- 新增场景覆盖项必须更新 `Hotc233SceneTestHarness` 和 `Hotc233MainSceneBuilder`。
- 修改验证输出 token 时，必须同步更新 `HotUpdateVerificationParser`、报告 writer、comparison verifier。
