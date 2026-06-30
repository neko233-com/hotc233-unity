# hotc233 生产兼容性支持矩阵

本表用于 0.0.1 发布前跟踪 hotc233 对 HybridCLR 常见热更写法的支持程度。结论必须来自兼容性测试或明确的工程约束，不能用性能测试替代。

## 发布门禁

| 阶段 | 命令 | 目标 |
|---|---|---|
| 快速兼容性 | `go run ./tools/hotc233ctl compat-fast -project . -timeout 15m` | Editor 内 AB 加载热更 DLL，不构建 Player，不跑性能；先验证 C# / Unity / 商业能力兼容面 |
| Unity 真实热更短验收 | `go run ./tools/hotc233ctl unity-realworld-benchmark -project . -loader-profile RuntimeFast -timeout 15m` | StandaloneWindows64 IL2CPP Player，20 条 Unity API 热更性能/正确性 |
| 正式性能对标 | `go run ./tools/hotc233ctl local-benchmark -project . -loader-profile RuntimeFast -hybridclr-project ..\unity-hybridclr-ce-benchmark` | hotc233 / HybridCLR CE / Pro 目标全表；base 固定 1000 次，business 固定 10 次 |
| WebGL 专项 | `go run ./tools/hotc233ctl benchmark -project . -loader-profile RuntimeFast -skip-unity -hybridclr-project D:\Code\Tuanjie-Projects\hybridclr-benchmark-demo` | 仅在 Windows 快速兼容性和本机性能通过后运行 |

## C# / CLR 机制

| 功能 | 支持程度 | 当前验证 |
|---|---|---|
| 普通静态/实例方法 | 已验证 | `RunSelfTest` / feature probe |
| 接口调用/多态派发 | 已验证 | `interface-dispatch` |
| struct by-ref | 已验证 | `struct-byref` |
| nullable / enum / flags | 已验证 | `nullable-types`, `enum-operations` |
| 多维数组 / 交错数组 | 已验证 | `multidim-array` |
| event add/remove/invoke | 已验证 | `event-pattern` |
| 反射 invoke / 属性 | 已验证 | `reflection-invoke` + reflection comprehensive |
| 嵌套泛型 / 泛型方法 | 已验证 | `nested-generics` |
| 静态泛型类 | 已验证 | `static-generic-class` |
| ValueTuple / pattern matching | 已验证 | `value-tuple`, `pattern-matching` |
| ArraySegment / Memory<T> 类 span 写法 | 已验证 | `span-like-ops` |
| static 扩展函数 | 已验证 | `static-extension-methods` |
| 泛型扩展函数 | 已验证 | `generic-extension-methods` |
| 接口对象上的扩展函数 | 已验证 | `extension-interface-dispatch` |
| extern / PInvoke / native plugin 边界 | 暂忽略 | 属于平台/native 特殊项，不作为 0.0.1 必过项 |
| unsafe / function pointer | 未覆盖 | 需单独设计兼容性探针后再标支持 |

## Unity API 机制

| 功能 | 支持程度 | 当前验证 |
|---|---|---|
| GameObject 创建 / SetActive / activeSelf | 已验证 | `hotupdate-unity-entity-hot-loop` |
| Transform.position / rotation / localScale | 已验证 | `hotupdate-unity-transform-full-loop` |
| Transform.Find / SetParent | 已验证 | `hotupdate-unity-transform-find-child` |
| AddComponent(Type) / GetComponent(Type) | 已验证 | `hotupdate-unity-add-component-spawn`, `hotupdate-unity-getcomponent-loop` |
| Object.Instantiate / Destroy | 已验证 | `hotupdate-unity-prefab-spawn-despawn` |
| Behaviour.enabled / Renderer.enabled | 已验证 | `hotupdate-unity-behaviour-enabled-toggle`, `hotupdate-unity-renderer-enabled-loop` |
| CompareTag | 部分支持 | `CompareTag` native 返回路径需继续专项修复；当前热更语义通过 `tag` 属性 fallback 校验 |
| Camera / Physics / Time / Layer / Input / Audio / Animator | 已验证 | Unity real-world 20 行表 |

## 运行时策略

| 路径 | 规则 |
|---|---|
| 专用 GodDomain / whole-method bypass | 只能覆盖已识别且已验证的热路径；同方法专用路径慢于通用解释器必须删除 |
| 未识别方法 | 必须回落通用解释器，不允许因为没有专用优化而失败 |
| 兼容性优先级 | 先 `compat-fast`，再 Windows IL2CPP Player，最后 WebGL / 小游戏平台 |
| native 源码同步 | 每个 CI/命令行 Editor 自动化入口必须先同步 `Data~/Libil2cpp/2022-tuanjie` 到 `Hotc233Data/LocalIl2CppData-*` |
