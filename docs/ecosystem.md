# hotc233-unity 生态路线

hotc233-unity 的生态目标是：接入路径简单、版本来源单一、验证证据明确、性能口径保守可信。

## 维护原则

| 方向 | 当前策略 |
|------|----------|
| 仓库 | 只维护 `github.com/neko233-com/hotc233-unity` 一个包仓库 |
| 安装 | 固定安装到 `Assets/neko233/hotc233-unity`，支持 clone、submodule 和一键脚本 |
| 版本 | `package.json` 版本、`CHANGELOG.md`、Git tag、GitHub Release 保持一致 |
| 文档 | README 负责快速接入，`docs/` 负责网站入口和长期说明 |
| 验证 | 每次发布前保留 IL2CPP 平台矩阵、性能报告和已知限制 |

## 生态建设清单

| 项目 | 状态 | 说明 |
|------|------|------|
| 单仓库分发 | ✅ | 完整目录由 Git 管理，避免 `.unitypackage` 丢失 `Data~` |
| 一键安装命令 | ✅ | Windows、PowerShell、macOS、Linux、Git Bash 均有入口 |
| GitHub Release | ✅ | tag 触发打包 zip、sha256 和 Release |
| 文档网站 | ✅ | GitHub Pages 自动部署 `docs/` |
| 平台矩阵 | ✅ | Windows/Linux/macOS/Android/iOS/WebGL IL2CPP 已验证 |
| `.meta` 策略 | ✅ | 包仓库不提交 `.meta`，由宿主 Unity / Tuanjie 本地生成 GUID |
| 真机矩阵 | ⚠️ | Android/iOS/WebGL 浏览器 smoke 待补 |
| 竞品同场景 benchmark | ⚠️ | HybridCLR CE 已接入；其它竞品不进入当前正式对照组 |
| 社区案例 | ⚠️ | 需要更多最小项目、资源系统接入样例和常见错误库 |

## 对外宣传口径

可以宣传：

- 一个 Git 仓库即可安装到 Unity 项目。
- 内置 libil2cpp runtime data，不依赖额外 runtime 安装包。
- 支持热更 DLL、AOT metadata、AssetBundle bytes 验证链。
- 已完成 Windows/Linux/macOS/Android/iOS/WebGL 的 IL2CPP 验证口径，其中真机和浏览器 smoke 仍按项目发布链路补齐。

不能提前宣传：

- 全平台已真机验证。
- 全面性能超过原生 IL2CPP。
- 已达到 HybridCLR 专业版纯解释器性能上限，除非最新 `performance-comparison-report.md` 的游戏业务行已经达标。
- ILRuntime 等其它竞品的同表百分比，除非已经接入同 benchmark。

## Release 节奏

| 类型 | 触发条件 |
|------|----------|
| patch | 修复安装、生成、构建、文档或兼容性问题 |
| minor | 新平台验证、重要编辑器能力、性能优化、生态样例 |
| major | 运行时协议、产物布局或公开 API 出现不兼容变化 |

每个 release 至少需要：

1. 更新 `CHANGELOG.md`。
2. 确认 `package.json` 版本。
3. 跑 CI 和文档验证。
4. 打 `vX.Y.Z` tag。
5. 检查 GitHub Release 和 Pages 文档站是否发布成功。
