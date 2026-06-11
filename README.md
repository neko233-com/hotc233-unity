# Hotc233

Hotc233 是 Unity / Tuanjie 的 C# 热更新库。目标：一个包内完成运行时加载、元数据补足、编辑器生成、构建接入、自动验证。

当前工程已接好完整验证链：点击菜单或跑 Go 工具后，自动编译热更程序集，产出 `.dll.bytes`，打入 AssetBundle，再从 AssetBundle 读回二进制并执行自测入口。

## 能力

- 运行时从 `byte[]` 加载热更程序集。
- 运行时补足裁剪后元数据。
- 支持多 asmdef 热更代码图，按依赖顺序加载。
- 编辑器侧生成桥接代码、裁剪元数据、热更 DLL。
- 自动把 `.dll` 转成 `.dll.bytes`，打进 AssetBundle。
- 自动从 AssetBundle 读回 `.dll.bytes`，验证加载和反射调用。
- 外部自动化工具统一使用 Go 1.26。

## 目录

```text
Assets/neko233/hotc233/
  Runtime/              运行时 API 和二进制加载器
  Editor/               编辑器生成、构建处理、设置面板
  Data~/                内置运行时源码、模板、基准库文件
  Plugins/              编辑器分析依赖
  Documentation~/       架构文档

Assets/EditorForBuild/
  Hotc233BuildAutomation.cs      一键 AB 验证入口
  Generated/                     自动生成的验证产物

tools/hotc233ctl/
  main.go                        Go 1.26 一键构建/测试工具
```

## 运行时最小用法

```csharp
using Hotc233;

var loader = new HotUpdateBinaryLoader();

loader.LoadRuntimeMetadata(metadataBinaries, HomologousImageMode.SuperSet);
loader.LoadHotUpdateAssemblies(hotUpdateBinaries);

var result = loader.InvokeStatic(
    "UnityHotc.CodeHotUpdate.HotUpdateEntry",
    "RunSelfTest");
```

`metadataBinaries` 和 `hotUpdateBinaries` 都是 `IEnumerable<NamedBinary>`。调用方负责从本地文件、远端 CDN、AssetBundle 或资源系统拿到 `byte[]`。

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

## 一键 AB 验证

Unity 菜单：

```text
Hotc233/EditorForBuild/Run Full AB Verification
```

它会自动执行：

1. 检查 Hotc233 安装状态。
2. 写入热更 asmdef 设置。
3. 确保 `Assets/Scenes/main.scene` 在 Build Settings。
4. 生成热更 DLL 和运行时元数据。
5. 复制为 `.dll.bytes` 到 `Assets/EditorForBuild/Generated/Payload/`。
6. 构建 `hotc233_binary_payload` AssetBundle。
7. 从 AssetBundle 加载 `.dll.bytes`。
8. 加载运行时元数据。
9. 加载热更程序集。
10. 调用 `UnityHotc.CodeHotUpdate.HotUpdateEntry.RunSelfTest`。
11. 写出 `Assets/EditorForBuild/Generated/verification-report.json`。

成功报告包含：

```json
{
  "success": true,
  "message": "HotUpdateLogic loaded ..."
}
```

## Go 自动化

外部工具统一 Go 1.26。

```powershell
cd D:\Code\Poko-Dev-Projects\unity-hotc\tools
go test ./hotc233ctl/... ./server_hotupdate/...
go run ./hotc233ctl all -project D:\Code\Poko-Dev-Projects\unity-hotc -target StandaloneWindows64 -version dev
```

命令：

- `build`: Unity batchmode 跑完整 AB 验证。
- `serve`: 启动本地静态 CDN。
- `test`: 启动 CDN 后跑 PlayMode 测试。
- `player`: 构建/复用 Windows Player，真机形态加载 AB 内 `.dll.bytes` 并写报告。
- `device`: 调用外部真机探针；未配置时写 `not_run`。
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
Assets/neko233/hotc233/Documentation~/architecture.md
```

## 约束

- 热更 DLL 必须以 `.dll.bytes` 作为资源发布，避免平台导入器误处理。
- 运行时必须先加载元数据，再加载热更程序集。
- asmdef 依赖必须单向，基础层不能反向引用业务层。
- 自动化生成目录可以删除，下一次验证会重建。
- CI 推荐只调用 Go 工具，不手写 Unity 命令行。
