# hotc233-unity

hotc233-unity 是 Unity / Tuanjie 的 C# 热更新库。目标：一个包内完成运行时加载、元数据补足、编辑器生成、构建接入、自动验证。

> 包名和仓库名为 `hotc233-unity` / `com.neko233.hotc233-unity`。运行时代码命名空间仍保留 `Hotc233`，用于保持 API 兼容。

文档网站：

```text
https://neko233-com.github.io/hotc233-unity/
```

当前工程已接好完整验证链：点击菜单或跑 Go 工具后，自动同步包内内置运行时、编译热更程序集，产出 `.dll.bytes`，打入 AssetBundle，再从 AssetBundle 读回二进制并执行自测入口。

## 能力

- 运行时从 `byte[]` 加载热更程序集。
- 运行时补足裁剪后元数据。
- 支持多 asmdef 热更代码图，按依赖顺序加载。
- 编辑器侧生成桥接代码、裁剪元数据、热更 DLL。
- 自动把 `.dll` 转成 `.dll.bytes`，打进 AssetBundle。
- 自动从 AssetBundle 读回 `.dll.bytes`，验证加载和反射调用。
- 外部自动化工具统一使用 Go 1.26。
- 使用包内 `Data~/Libil2cpp`，不需要 HybridCLR 式外部 install。

## Unity 2022+ 商业能力兼容

hotc233-unity 只维护 Unity / Tuanjie 2022+ 作为最低支持线，不为 2021 及更早版本保留 legacy 分支。当前竞品目标只有 HybridCLR Pro 纯解释能力；商业版常见解释器相关能力都提供 hotc233 对应入口：

| 能力 | hotc233-unity 支持入口 |
|------|------------------------|
| 完全泛型共享 | `enableFullGenericSharing`、`Data~/Libil2cpp/2022-tuanjie/metadata/GenericSharing.*`、AOT 泛型引用分析 |
| 元数据优化 | AOT metadata 裁剪、`LoadRuntimeMetadata`、`Hotc233LoadPolicy.RequireSha256` |
| 标准解释优化 | `HotUpdatePerformanceProfile.RuntimeFast`、离线 IR 合成、解释器快路径 |
| Hotfix 动态热修复 | `HotUpdateBinaryLoader.ReplaceHotUpdateAssembly` |
| 热重载工作流 | `HotUpdateBinaryLoader.ReloadHotUpdateAssemblies` |
| 代码加密 / 加固 | `Hotc233LoadPolicy.DecryptBinary`、`CreateXorProtected`、完整性校验 |
| Assembly.Load 加载优化 | 本地 payload manifest、StreamingAssets 原始 `.dll.bytes`、loader cache |
| 解释器栈崩溃日志 | `Hotc233RuntimeDiagnostics` session / recent events / exception context |
| 访问控制策略 | `Hotc233LoadPolicy.AllowOnly`、`AccessValidator` |

GitHub Pages 文档采用商业能力左侧目录布局，入口为：

```text
https://neko233-com.github.io/hotc233-unity/
```

## 目录

```text
Assets/neko233/hotc233-unity/
  Runtime/              运行时 API 和二进制加载器
  Editor/               编辑器生成、构建处理、设置面板
  Data~/                内置运行时源码、模板、基准库文件
  Plugin_dnlib/         dnlib 编辑器分析依赖
  Plugin_LZ4/           LZ4 编辑器分析依赖
  Documentation~/       架构文档

Assets/EditorForBuild/
  Hotc233BuildAutomation.cs      一键 AB 验证入口
  Generated/                     自动生成的验证产物

tools/hotc233ctl/
  main.go                        Go 1.26 一键构建/测试工具
```

## 运行时最小用法

```csharp
using System;
using Hotc233;

var loader = new HotUpdateBinaryLoader();

loader.LoadRuntimeMetadata(metadataBinaries, HomologousImageMode.SuperSet);
loader.LoadHotUpdateAssemblies(hotUpdateBinaries);

var result = loader.InvokeStatic(
    "UnityHotc.CodeHotUpdate.HotUpdateApp",
    "RunSelfTest");
```

`metadataBinaries` 和 `hotUpdateBinaries` 都是 `IEnumerable<NamedBinary>`。调用方负责从本地文件、远端 CDN、AssetBundle 或资源系统拿到 `byte[]`。
`HotUpdateBinaryLoader` 会通过 `Hotc233RuntimeDiagnostics` 打印 session、平台、二进制大小、短 hash、程序集名和入口调用结果，真机失败时优先看这些日志。重复调用 `InvokeStatic` 时会复用已解析的 Type / MethodInfo。业务热更代码不需要为了 hotc233 写 bridge、强类型入口委托或特殊批处理；入口缓存、profile 选择和性能基准都属于宿主基础设施职责。

代码加密、完整性校验、访问控制和 hotfix 可以通过同一个 loader 接入：

```csharp
var policy = Hotc233LoadPolicy.CreateXorProtected("dev-key")
    .RequireSha256("HotUpdateDlls/Feature_HotUpdate.dll.bytes", expectedSha256);
policy.EnableAccessControl = true;
policy.AllowedBinaryNames.Add("HotUpdateDlls/Feature_HotUpdate.dll.bytes");

var loader = new HotUpdateBinaryLoader
{
    LoadPolicy = policy,
};

loader.LoadHotUpdateAssemblies(hotUpdateBinaries);
loader.ReplaceHotUpdateAssembly(new NamedBinary("HotUpdateDlls/Feature_HotUpdate.dll.bytes", hotfixBytes));
```

可选 loader profile：

```csharp
var loader = new HotUpdateBinaryLoader()
    .UsePerformanceProfile(HotUpdatePerformanceProfile.RuntimeFast);
```

`RuntimeFast` 是默认值，会应用运行时方法体缓存和解释器内联阈值等性能选项，用于对齐 HybridCLR 习惯下的默认高性能热更路径。`RuntimeOptions` 仍保留为兼容别名；`Compatibility` 可用于完全关闭 loader 性能默认值；`Stable` 保留为更保守的 profile；`PreJit` / `Aggressive` 默认会跳过 PreJIT，只有设置 `HOTC233_UNSAFE_PREJIT=1` 才会真正执行。不要把 profile 当成业务代码写法要求。

RuntimeFast 运行时包含面向业务热循环的解释器快路径：`System.Math.Min/Max(int,int)`、`System.Math.Min/Max(long,long)` 直接降为 hotc233 IR，不再走 native call-common；20/24/28/32 字节无引用结构体数组写回使用固定大小 copy，覆盖常见战斗、背包等 struct hot loop。

如果迁移项目里已有 `HybridCLR.RuntimeApi` 调用，可以直接保留调用代码。hotc233 默认提供 `HybridCLR.RuntimeApi` facade，把常见元数据加载、PreJIT 和 runtime option 调用转发到 `Hotc233.RuntimeApi`。新接入旧 HybridCLR 打包生态时，优先把原来的 `HybridCLR.RuntimeApi` 调用点替换为 `HybridCLR.Hotc233_HybridclrAdapt`；这个独立适配类承载 API 兼容，便于只改 class name 接入。

不要在同一工程同时安装官方 `com.code-philosophy.hybridclr` 包；两个包都会定义 `HybridCLR` API。需要对标 HybridCLR 8.11.0 时，使用本仓库性能表里的官方专业版纯解释目标区间。

### 接管已有 HybridCLR 配置

如果项目已经配置过官方 HybridCLR，可直接导入已有热更 asmdef、热更程序集名、AOT metadata 和泛型扫描迭代设置：

```text
hotc233/HybridCLR Compatibility/Import HybridCLR Settings
```

该菜单默认保留 hotc233 自己的输出目录（`Hotc233Data/`、`Hotc233Generate/`），只接管“哪些程序集热更、哪些 AOT 元数据补充”的核心配置。需要完全镜像 HybridCLR 输出路径时，可运行：

```text
hotc233/HybridCLR Compatibility/Import HybridCLR Settings (Mirror Output Paths)
```

CI 可调用：

```powershell
go run ./hotc233ctl import-hybridclr-settings -project D:\Code\neko233-Projects\unity-hotc233-demo
```

## 工程接入

1. 热更代码放入独立 asmdef。
2. 需要热更的 asmdef 放入 Hotc233 设置。
3. 基础程序集放底层，业务程序集引用基础程序集。
4. 构建前运行生成流程，得到热更 DLL 和运行时元数据。
5. 发布时把 `.dll.bytes` 当普通二进制资源交给资源系统。
6. 启动时先加载元数据，再加载热更程序集，最后调用业务入口。

本仓库示例热更图：

```text
Framework_HotUpdate
  -> Feature_HotUpdate
      -> HotUpdateLogic
```

### 如何定义哪些程序集是热更程序集

打开 Unity 菜单：

```text
hotc233/Settings...
```

在 **Hot Update Assembly Definitions** 中拖入需要热更的 `AssemblyDefinitionAsset`。这些 asmdef 会被 `CompileDll` / `Generate/All` 编译成热更 DLL，并在运行时由 `HotUpdateBinaryLoader` 从 `byte[]` 加载。

建议拆分方式：

| 层级 | 示例 | 说明 |
|------|------|------|
| 基础层 | `Framework_HotUpdate` | 日志、基础工具、纯 C# 基础设施。不能引用业务层。 |
| 功能层 | `Feature_HotUpdate` | C# 语法探针、Unity API 探针、可复用玩法功能。 |
| 入口层 | `HotUpdateLogic` | 业务入口、UI/Prefab 绑定入口、`RunFullVerification` 等。 |

不要把主工程 AOT 程序集、第三方插件程序集、Editor 程序集放进热更 asmdef 列表。热更程序集应当是“需要远端更新、运行时通过 byte[] 加载”的业务代码；主工程只通过反射、公共接口、资源实例化或自定义启动器接入它。

### 分发给其他项目

推荐分发方式是 **Git 管理的完整目录**，目标路径固定为：

```text
Assets/neko233/hotc233-unity
```

这个目录里的 `Data~` 是编辑器生成 MethodBridge、AOT 元数据和内置 libil2cpp runtime 的关键输入，不能丢。

### 关于 `.meta`

`hotc233-unity` 子仓库不提交 Unity `.meta` 文件。包本身是代码、Editor 工具、DLL 依赖和 `Data~` 生成输入，不包含 prefab、scene、ScriptableObject 这类必须跨项目保持固定 GUID 的序列化资产。`.meta` 交给宿主 Unity / Tuanjie 项目本地生成，可以避免两套编辑器生成不同 GUID 格式导致仓库反复变脏。

接入项目如果需要在 `ProjectSettings/Hotc233Settings.asset` 中引用热更 asmdef 或包内设置脚本，应以本项目本地生成的 `.meta` GUID 为准，由 `hotc233/Settings...` 或自动化配置流程写入，不要把另一个项目生成的 `.meta` 当作包的一部分复制。

最直接的安装方式：

```bash
mkdir -p Assets/neko233
git clone https://github.com/neko233-com/hotc233-unity.git Assets/neko233/hotc233-unity
```

如果项目想用 submodule 管理：

```bash
mkdir -p Assets/neko233
git submodule add https://github.com/neko233-com/hotc233-unity.git Assets/neko233/hotc233-unity
git submodule update --init --recursive
```

### 项目根目录一键命令行安装

在 Unity 项目根目录执行下面任意一种命令。默认安装到 `Assets/neko233/hotc233-unity`；如果目录已经是 Git checkout，会自动 `git pull --ff-only` 更新。

Windows cmd：

```bat
powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/neko233-com/hotc233-unity/main/install-hotc233-unity.ps1 | iex"
```

PowerShell：

```powershell
irm https://raw.githubusercontent.com/neko233-com/hotc233-unity/main/install-hotc233-unity.ps1 | iex
```

macOS / Linux / Git Bash：

```bash
curl -fsSL https://raw.githubusercontent.com/neko233-com/hotc233-unity/main/install-hotc233-unity.sh | sh
```

如果希望用 submodule 管理，先设置环境变量再执行同一条安装命令：

```bat
set HOTC233_USE_SUBMODULE=1
powershell -NoProfile -ExecutionPolicy Bypass -Command "irm https://raw.githubusercontent.com/neko233-com/hotc233-unity/main/install-hotc233-unity.ps1 | iex"
```

```bash
export HOTC233_USE_SUBMODULE=1
curl -fsSL https://raw.githubusercontent.com/neko233-com/hotc233-unity/main/install-hotc233-unity.sh | sh
```

### 一键 Git 安装 MenuItem

也可以在目标项目中新建 `Assets/Editor/InstallHotc233Unity.cs`，复制下面脚本，然后点击：

```text
Tools/hotc233-unity/Install or Update from Git
```

```csharp
using System;
using System.Diagnostics;
using System.IO;
using UnityEditor;
using Debug = UnityEngine.Debug;

public static class InstallHotc233Unity
{
    private const string RepoUrl = "https://github.com/neko233-com/hotc233-unity.git";
    private const string InstallPath = "Assets/neko233/hotc233-unity";

    [MenuItem("Tools/hotc233-unity/Install or Update from Git")]
    public static void InstallOrUpdate()
    {
        Directory.CreateDirectory("Assets/neko233");

        if (Directory.Exists(Path.Combine(InstallPath, ".git")))
        {
            RunGit("pull --ff-only", InstallPath);
        }
        else if (Directory.Exists(InstallPath))
        {
            throw new InvalidOperationException(
                InstallPath + " already exists but is not a git checkout. Move it away or delete it first.");
        }
        else
        {
            RunGit($"clone {RepoUrl} \"{InstallPath}\"", Directory.GetCurrentDirectory());
        }

        AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
        Debug.Log("[hotc233-unity] Installed or updated at " + InstallPath);
    }

    private static void RunGit(string arguments, string workingDirectory)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = "git",
            Arguments = arguments,
            WorkingDirectory = Path.GetFullPath(workingDirectory),
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };

        using (var process = Process.Start(startInfo))
        {
            if (process == null)
            {
                throw new InvalidOperationException("Failed to start git.");
            }

            string output = process.StandardOutput.ReadToEnd();
            string error = process.StandardError.ReadToEnd();
            process.WaitForExit();

            if (process.ExitCode != 0)
            {
                throw new InvalidOperationException(error);
            }

            if (!string.IsNullOrWhiteSpace(output))
            {
                Debug.Log(output);
            }
        }
    }
}
```

Unity 菜单提供了 `.unitypackage` 导出：

```text
hotc233/Export/Export unitypackage...
hotc233/Export/Export unitypackage to Build/Packages
```

但要注意：Unity 的 `ExportPackage` 不会完整包含 `Data~`、`Documentation~` 这类以 `~` 结尾的 package-private 目录。当前验证导出的 `Build/Packages/hotc233-1.0.0.unitypackage` 约 **0.55 MB**，只包含 Unity AssetDatabase 可见的 `Runtime`、`Editor`、`Plugin_dnlib`、`Plugin_LZ4` 等资产，不包含完整 `Data~/Libil2cpp`。因此：

- 只把这个 `.unitypackage` 给其他项目，会缺内置 runtime data，后续 `hotc233/Generate/All` 可能失败。
- 完整给其他项目使用时，应给完整目录或 UPM/local package，而不是只给 `.unitypackage`。
- `.unitypackage` 菜单适合导出可见脚本/插件资产快照，不适合作为完整 hotc233 runtime 分发包。

### 接入一个新项目需要改多少

通常不需要为了性能改写业务代码，但需要做这几件热更接入边界工作：

1. 把希望热更的业务代码移动到独立 asmdef。
2. 清理主工程对热更业务类型的编译期直接引用。
3. 在 `hotc233/Settings...` 配置热更 asmdef 和 AOT 元数据程序集。
4. 在构建前执行 `hotc233/Generate/All`。
5. 把生成的热更 DLL 和 AOT 元数据 DLL 作为 `.dll.bytes` 交给资源系统发布。
6. 启动时先加载 AOT 元数据，再加载热更 DLL，最后调用热更入口。

如果项目原本已经按 asmdef 分层，改动通常集中在启动加载和资源发布；如果业务代码全部在 `Assembly-CSharp` 且主工程直接到处引用业务类型，需要先拆程序集边界。

### 包体和构建影响

当前 `Assets/neko233/hotc233-unity` 目录约 **10.19 MB / 1228 个文件**：

| 目录 | 体积 | 说明 |
|------|------|------|
| `Data~` | 约 8.23 MB | 内置 `Libil2cpp` runtime 源码、模板和基准文件，是编辑器生成阶段的主要体积来源。 |
| `Plugin_dnlib` | 约 1.12 MB | dnlib 编辑器分析依赖。 |
| `Plugin_LZ4` | 约 0.04 MB | LZ4 编辑器分析依赖。 |
| `Editor` | 约 0.73 MB | 生成命令、构建处理、设置窗口、导出工具。 |
| `Runtime` | 约 0.05 MB | Player 运行时 API、二进制加载器和诊断。 |

完整目录或 UPM/local package 会让工程仓库增加约 10 MB 级别的源码/工具文件。单独导出的 `.unitypackage` 只有约 0.55 MB，但它不是完整分发包，因为缺 `Data~`。最终 Player 包体并不等于直接增加 10 MB：`Editor`、`Documentation~`、大部分生成工具不会进入 Player；真正影响发布包的是你发布的热更 `.dll.bytes`、AOT metadata `.dll.bytes`、资源包清单和平台生成产物。

构建影响主要来自：

- IL2CPP 出包前需要生成 MethodBridge / ReversePInvokeWrapper / link.xml / AOTGenericReference。
- 需要裁剪并发布 AOT 元数据 DLL。
- 资源包会额外携带热更 DLL bytes 和 metadata bytes。
- 首次切平台或强制重建时耗时更明显；后续 `PrebuildCache` 命中会跳过未变化阶段。

## 一键 AB 验证

Unity 菜单：

```text
hotc233/EditorForBuild/Run Full AB Verification
```

它会自动执行：

1. 检查/同步 Hotc233 内置运行时状态。
2. 写入热更 asmdef 设置。
3. 确保 `Assets/Scenes/main.scene` 在 Build Settings。
4. 生成热更 DLL 和运行时元数据。
5. 复制为 `.dll.bytes` 到 `Assets/EditorForBuild/Generated/Payload/`。
6. 构建 `hotc233_binary_payload` AssetBundle。
7. 从 AssetBundle 加载 `.dll.bytes`。
8. 加载运行时元数据。
9. 加载热更程序集。
10. 调用 `UnityHotc.CodeHotUpdate.HotUpdateApp.RunFullVerification`。
11. 写出 `Assets/EditorForBuild/Generated/verification-report.json`。

完整验证包含 `ReflectionComprehensiveProbe`：Assembly/Module/Type、构造、字段/属性、方法重载、私有成员、事件/委托、Attribute、泛型方法/类型、数组/Enum/Nullable、InterfaceMap/嵌套类型、可选参数和 TargetInvocationException。

## C# 语法与反射支持

当前示例工程的 `RunFullVerification` 会执行 95 个 hotc233 探针。已验证通过的 C# 能力如下：

| 类别 | 已验证能力 | 接入注意 |
|------|------------|----------|
| 基础语法 | lambda / 闭包、泛型方法、泛型类型、yield 迭代器、委托闭包、异常过滤器 `when` | 热更代码必须位于 `Hot Update Assembly Definitions` 中登记的 asmdef。 |
| 类型系统 | 接口分派、struct by ref、Nullable、Enum flags/parse、多维数组、锯齿数组、event、ValueTuple、模式匹配、ArraySegment / Memory 类操作 | 发包前运行 `hotc233/Generate/All`，确保 AOT 元数据和 link 保护已刷新。 |
| 反射 | Assembly / Module / Type 查询、构造函数、Activator、字段、属性、方法重载、私有成员、事件、Delegate.CreateDelegate、Attribute、泛型方法、泛型类型、数组、Enum、Nullable、InterfaceMap、嵌套类型、可选参数、TargetInvocationException | 支持反射；被 IL2CPP 裁剪的 AOT 类型仍需要进入 AOT metadata 或 link.xml 保护范围。 |
| LINQ | Sum、Average、Count、LongCount、Min、Max、Aggregate、Distinct、Union、Intersect、Except、GroupBy、Join、Zip、OrderBy、ThenBy、SelectMany、All、Any、Contains、Single、ElementAt、Take、Skip、Concat、SequenceEqual、ToDictionary、ToLookup | 性能敏感路径以 Player / 真机数据为准。 |
| Unity API | ScriptableObject、UnityEvent、Coroutine、UGUI、2D Sprite、3D Mesh、Timeline、async/await Task、SceneManagement、平台/设备/屏幕/输入基础 API | Prefab / ScriptableObject 中的热更类型必须在热更 DLL 加载后再实例化。 |

反射不是“文档声称支持”：当前仓库的 `ReflectionComprehensiveProbe` 会实际调用上述反射场景，失败会让验证流程失败。最终发布仍应以目标平台 IL2CPP 矩阵报告为准。

成功报告包含：

```json
{
  "success": true,
  "message": "HotUpdateLogic loaded ..."
}
```

## 巅峰性能（Tuanjie WebGL）

竞品目标只有 **HybridCLR Pro 纯解释上限**（~76.9% native IL2CPP）。总纲与迭代闭环：

```text
docs/performance-peak-plan.md
docs/webgl-performance.md
docs/hybridclr-pro-landing-roadmap.md
docs/pro-wrong-answer-notebook.md
```

Tuanjie WebGL 验收（每批次架构改动后）：

```powershell
go run ./tools/hotc233ctl validate-reports -project .
go run ./tools/hotc233ctl pro-gate -project .
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'; go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
```

## 性能对比采集

需要 hotc233 / HybridCLR 专业版纯解释目标 / 原生 Mono / 原生 IL2CPP 性能对比时，运行：

```text
hotc233/EditorForBuild/Run Performance Comparison Baselines
```

该入口会在 `StandaloneWindows64` 下构建并运行三种 Player：

1. hotc233：IL2CPP Player 中加载热更 DLL 后执行 `PerformanceProbe.Run()`，loader profile 必须为 `RuntimeFast`。
2. 原生 Mono：主工程原生编译后执行 `NativePerformanceProbe.Run()`。
3. 原生 IL2CPP：主工程原生编译后执行 `NativePerformanceProbe.Run()`。

HybridCLR 专业版列不是本地官方包实测，而是按 HybridCLR 官方性能页换算的目标区间：专业/商业版纯解释器约为原生 AOT 的 `7.8% ~ 76.9%`。该目标不允许通过把大量热更逻辑预置进首包 AOT 来换性能。

性能套件必须覆盖机制成本、游戏业务热循环和 WebGL/minigame 内存风险。当前固定包含 `game-low-gc-frame` 与 `game-memory-stability`：前者要求热循环 `gc0=0,gc1=0,gc2=0`，后者在预分配战斗、配置和序列化缓冲区上记录 `heapBefore`、`heapAfterFullCollect` 和 `retainedDelta`。`validate-reports` 与 `headless` 会读取 PC hotc233、PC 原生 IL2CPP、WebGL hotc233、WebGL 原生 IL2CPP 的原始 JSON，强制 `retainedDelta <= 65536` bytes。

专业版能力完成度、社区版粗推区间和逐项差距见 `docs/hybridclr-gap-analysis.md`。

输出：

```text
Assets/EditorForBuild/Generated/performance-hotc233-player.json
Assets/EditorForBuild/Generated/native-performance-mono.json
Assets/EditorForBuild/Generated/native-performance-il2cpp.json
Assets/EditorForBuild/Generated/performance-comparison-report.md
Assets/EditorForBuild/Generated/performance-comparison-report.html
Assets/EditorForBuild/Generated/performance-comparison-report.json
Assets/EditorForBuild/Generated/performance-minigame-local-il2cpp.md
Assets/EditorForBuild/Generated/performance-minigame-local-il2cpp.html
Assets/EditorForBuild/Generated/minigame-runtimefast-health.md
Assets/EditorForBuild/Generated/headless-performance-gate.md
Assets/EditorForBuild/Generated/headless-performance-gate.html
```

`performance-comparison-report.md` 按 HybridCLR 风格显示比例：原生 IL2CPP ops/s 固定为 **100%**，hotc233 与原生 Mono 都用 `自身 ops/s / 原生 IL2CPP ops/s * 100%` 量化；HybridCLR 专业版列显示官方目标区间。不要用 Editor 安全模式的 `performance-report.json` 冒充这个对比表。

## Go 自动化

外部工具统一 Go 1.26。

```powershell
cd D:\Code\neko233-Projects\unity-hotc233-demo\tools
go test ./hotc233ctl/... ./server_hotupdate/...
go run ./hotc233ctl headless -project D:\Code\neko233-Projects\unity-hotc233-demo -loader-profile RuntimeFast
go run ./hotc233ctl all -project D:\Code\neko233-Projects\unity-hotc233-demo -target StandaloneWindows64 -version dev
go run ./hotc233ctl validate-reports -project D:\Code\neko233-Projects\unity-hotc233-demo
```

命令：

- `build`: Unity batchmode 跑完整 AB 验证。
- `serve`: 启动本地静态 CDN。
- `test`: 启动 CDN 后跑 PlayMode 测试。
- `player`: 构建/复用 Windows Player，真机形态加载 AB 内 `.dll.bytes` 并写报告。
- `device`: 调用外部真机探针；未配置时写 `not_run`。
- `health`: 检查 RuntimeFast、官方 HybridCLR 包禁用、微信小游戏 slim/fbslim 关闭。
- `apply-health`: 一键设置 RuntimeFast + minigame 热更安全选项。
- `minigame`: 生成 AssetLib233 + minigame(WebGL2) 本地 IL2CPP 性能对比表。
- `validate-reports`: 不启动 Unity，只读校验 RuntimeFast 性能表、minigame 健康报告、manifest/lock、slim 设置、低 GC 帧循环和内存稳定哨兵。
- `headless`: 不启动 Unity/Tuanjie，先跑 Go 编译/测试、报告结构、指令表、RuntimeFast/minigame、HybridCLR 适配层、benchmark 覆盖、GC/retained heap 门禁，输出 `headless-performance-gate.json/.md/.html`。
- `import-hybridclr-settings`: 从旧 `ProjectSettings/HybridCLRSettings.asset` 导入配置。
- `all`: 先 `build`，再 `player`，最后 `device`。

编辑器路径查找顺序：

1. `-editor`
2. `HOTC233_UNITY_EDITOR`
3. `UNITY_EDITOR`
4. `TUANJIE_EDITOR`
5. 工具内置 Windows 常用路径

## 文档

架构见：

```text
Assets/neko233/hotc233-unity/Documentation~/architecture.md
```

文档站源码在：

```text
Assets/neko233/hotc233-unity/docs/
```

GitHub Pages 会在 `main` 分支推送后自动发布：

```text
.github/workflows/docs.yml
```

生态和平台口径：

```text
docs/ecosystem.md
docs/platforms.md
```

## 版本与 Release

hotc233-unity 只维护一个 Git 仓库：

```text
https://github.com/neko233-com/hotc233-unity.git
```

多版本分发通过 `package.json` 的 `version`、Git tag 和 GitHub Actions 完成。发布流程：

1. 修改 `package.json` 版本号。
2. 更新 `CHANGELOG.md`。
3. 提交到 `main`。
4. 打 tag：`v<package.json version>`，例如 `v1.0.0`。
5. 推送 tag 后，`.github/workflows/release.yml` 会校验版本、检查内部 native 命名、打包 zip、生成 sha256，并创建 GitHub Release。

文档站发布流程：

1. PR 和 push 都会运行 `.github/workflows/docs.yml` 的 docs 验证。
2. `main` 分支 push 通过后自动发布 `docs/` 到 GitHub Pages。
3. Pages 地址固定为 `https://neko233-com.github.io/hotc233-unity/`。

CI 会阻止 `Data~`、`Editor`、`Runtime` 内部重新出现历史 `hybridclr` 命名；文档里作为竞品对比出现的 HybridCLR 不受这个规则影响。

## 生态路线

hotc233-unity 当前生态重点：

| 方向 | 状态 | 下一步 |
|------|------|--------|
| 单仓库分发 | ✅ | 继续保持 `Assets/neko233/hotc233-unity` Git 管理 |
| 一键安装 | ✅ | 增加更多失败诊断和代理/镜像说明 |
| Release tag | ✅ | 每个版本自动打 zip、sha256、GitHub Release |
| 文档网站 | ✅ | GitHub Pages 自动发布，后续补常见错误和图文流程 |
| 平台矩阵 | ✅ | Windows/Linux/macOS/Android/iOS/WebGL IL2CPP 已验证；Android/iOS 真机、WebGL 浏览器 smoke 继续补 |
| 竞品实测 | ⚠️ | HybridCLR、ILRuntime、xLua 接入同 benchmark 后再填百分比 |
| `.meta` 策略 | ✅ | 子仓库忽略 `.meta`，由宿主项目按 Unity/Tuanjie 本地生成 |

## 约束

- 热更 DLL 必须以 `.dll.bytes` 作为资源发布，避免平台导入器误处理。
- 运行时必须先加载元数据，再加载热更程序集。
- asmdef 依赖必须单向，基础层不能反向引用业务层。
- 自动化生成目录可以删除，下一次验证会重建。
- CI 推荐只调用 Go 工具，不手写 Unity 命令行。
- 规范见 `Assets/neko233/hotc233-unity/AGENTS.md`。
- 与 HybridCLR 差距检查见 `docs/hybridclr-gap-analysis.md`。

## 日志语言

`CompileDll` 和 `Generate/All` 的关键日志支持中文 / English：

```text
hotc233/Language/Auto Detect
hotc233/Language/Chinese
hotc233/Language/English
```

默认是 `Auto Detect`，会根据 `Application.systemLanguage` 自动选择中文或英文。手动切到 `Chinese` 或 `English` 后会写入 `ProjectSettings/Hotc233Settings.asset`，之后重启 Unity 仍会记住。

也可以在 `hotc233/Settings...` 的 `Log Language` 字段里修改。

## 自动化场景保存

`hotc233/Generate/All` 会在生成前自动保存已经有路径的 dirty 场景，避免 Unity 在隐藏 BuildPlayer 阶段弹出 `Scene(s) Have Been Modified` 阻塞自动化。

如果当前打开的是还没有保存路径的 Untitled dirty 场景，hotc233 会直接中止 `Generate/All` 并输出日志。请先手动保存或关闭该场景，再重新运行生成命令。这样不会静默丢场景，也不会让 CI、菜单自动化或 MCP 流程卡在人工点击弹窗上。

## 导出 unitypackage

包自身可以通过 Unity 菜单导出：

```text
hotc233/Export/Export unitypackage...
hotc233/Export/Export unitypackage to Build/Packages
```

默认只导出 Unity AssetDatabase 可见的 `Assets/neko233/hotc233-unity` 资产。示例工程里的 `Assets/CodeHotUpdate`、`Assets/EditorForBuild`、`Assets/Resources-HotUpdate`、`Packages/YooAsset` 不会被打进包里；`Data~` / `Documentation~` 也不会完整进入 `.unitypackage`。

结论：`.unitypackage` **不能作为完整 hotc233 runtime 分发包单独给其他项目使用**。完整分发请使用 UPM/local package 或复制完整 `Assets/neko233/hotc233-unity` 目录。接入方仍要按上文定义热更 asmdef、配置生成项、把 `.dll.bytes` 接入自己的资源系统，并在启动流程里调用加载器。
