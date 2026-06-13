# 平台支持口径

hotc233-unity 走 Unity / Tuanjie IL2CPP 路径。理论上，只要目标平台使用同一套 IL2CPP 生成和运行时补丁链路，就具备移植基础；但发布前仍需要目标平台验证。

## 当前矩阵

| 平台 | 支持口径 | 当前验证 |
|------|----------|----------|
| Windows IL2CPP | ✅ 支持 | ✅ 已构建并运行验证 |
| Linux IL2CPP | ⚠️ 理论支持 | ❌ 当前仓库未验证 |
| macOS IL2CPP | ⚠️ 理论支持 | ❌ 当前仓库未验证 |
| iOS IL2CPP | ✅ 支持 | ✅ 已构建验证；未真机部署 |
| Android IL2CPP | ✅ 支持 | ✅ 已构建验证；真机 smoke 待补 |
| WebGL 2 / WebGL IL2CPP | ✅ 支持 | ✅ 已构建验证；浏览器 smoke 待补 |

## 为什么不能只说“一次 IL2CPP 全平台通用”

IL2CPP C++ 生成链路确实提供了跨平台基础，但平台差异仍会影响最终结果：

- Android Gradle、NDK、JDK 和 ABI 打包。
- iOS Xcode、签名、bitcode/linker、资源路径。
- WebGL 的文件加载、压缩、浏览器运行环境和线程限制。
- Windows/Linux/macOS 的播放器路径、动态库加载和大小写敏感文件系统。
- Unity/Tuanjie 不同版本的生成目录和构建后处理。

因此文档口径固定为：

1. **理论支持**：机制上应当能跑，但本仓库还没有该平台验证报告。
2. **构建验证**：Unity/Tuanjie 已经成功构建，并执行仓库验证入口。
3. **运行验证**：Player、真机或浏览器已经启动并写出 hotc233 报告。
4. **发版验证**：目标项目自己的资源系统、CDN、版本回滚和灰度链路也跑通。

## Release 前建议

| 阶段 | 必跑项 |
|------|--------|
| 每个 PR | Windows IL2CPP 构建验证、包元数据检查、文档站检查 |
| 每个 tag | Windows/Android/iOS/WebGL 构建矩阵、Release 打包、Pages 发布 |
| 游戏发版 | Android 真机、iOS 真机、WebGL 浏览器托管 smoke、长时间运行日志 |
