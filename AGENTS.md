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
- `performance-report.md`：性能结论和基准项。
- `platform-matrix-report.json`：平台构建/验证矩阵。
- 所有菜单、MCP 工具、日志需要输出报告绝对路径或 `report-links.md`。

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
- demo 与自动化必须提供分组性能菜单，并且每次对比都写出性能表。
- 微信小游戏/minigame(WebGL2) 热更必须关闭 `weixinMiniGameUseSlimMetaFileFormat` 和全部 `m_SlimFeaturesWeixinMiniGame`。
- 只读报告守门必须能通过 `go run ./hotc233ctl validate-reports -project <demo>`，不得要求 Unity editor 路径或触发 batchmode。
- 修改解释器 opcode 时，必须同步 `Instruction.h`、`Instruction.cpp`、`Interpreter_Execute.cpp`、transform 选择逻辑和宿主 `hotc233ctl validate-reports` 的指令表守门；RuntimeFast 热路径至少覆盖 `System.Math.Min/Max` signed int/long intrinsic 和 20/24/28/32 字节无引用 struct array store。
- 修改 loader、性能菜单、AssetLib233/minigame 对接、报告字段或配置导入器时，同步更新 README、宿主 demo 文档、AGENTS 和 CI 守门。

## 修改守则

- 不要把宿主项目生成的 `.meta` 强行加入子仓库；如果必须新增序列化资产，需要先说明为什么它需要稳定 GUID。
- 新增功能必须进入 `Hotc233FeatureCatalog` 和报告解析器。
- 新增热更资源必须进入 `HotUpdateResourceCatalog`，并由 `Hotc233TestResourceBuilder` 自动创建。
- 新增场景覆盖项必须更新 `Hotc233SceneTestHarness` 和 `Hotc233MainSceneBuilder`。
- 修改验证输出 token 时，必须同步更新 `HotUpdateVerificationParser`、报告 writer、comparison verifier。
