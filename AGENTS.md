# Hotc233 Agent Rules

本文件是 hotc233 包内维护规范。后续自动化代理、编辑器脚本、CI 和人工修改都必须遵守。

## 核心定位

- hotc233 不是 HybridCLR 的安装器外壳。
- hotc233 使用包内内置 `Data~/Libil2cpp` 作为唯一可信运行时来源。
- 项目侧不要求像 HybridCLR 一样执行外部 install 流程；只允许“同步内置运行时到本地工作目录”。
- 用户可见文案使用“内置运行时”“同步”“就绪”，避免使用“安装 HybridCLR”“Install HybridCLR”等语义。

## 内置运行时规范

- 包内运行时目录：`Assets/neko233/hotc233/Data~/Libil2cpp/2022-tuanjie`。
- 本地工作目录：`Hotc233Data/LocalIl2CppData-{Application.platform}/il2cpp`。
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
- 若底层源码仍保留 `hybridclr` 命名空间，文档必须说明这是兼容历史 native 布局，不代表依赖外部 HybridCLR 安装。
- 商业 DHE 等无法真实等价的能力必须在功能矩阵标注为模拟、兼容或待实现，不能宣传为已完整商业等价。

## 修改守则

- 新增功能必须进入 `Hotc233FeatureCatalog` 和报告解析器。
- 新增热更资源必须进入 `HotUpdateResourceCatalog`，并由 `Hotc233TestResourceBuilder` 自动创建。
- 新增场景覆盖项必须更新 `Hotc233SceneTestHarness` 和 `Hotc233MainSceneBuilder`。
- 修改验证输出 token 时，必须同步更新 `HotUpdateVerificationParser`、报告 writer、comparison verifier。
