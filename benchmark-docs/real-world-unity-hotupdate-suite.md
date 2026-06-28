# Unity 真实热更快测（独立于官方 base）

## 定位

| 维度 | HybridCLR 官方 14 base | `hotupdate-unity-*` 本套件 |
|------|------------------------|----------------------------|
| 目的 | fork 社区版对标、L1 门禁 | **真实业务**热更可行性 + 性能 |
| GO 用法 | 理想化（如 N 次 new+Destroy） | **真实用法**（单 GO 热循环、spawn+组件） |
| 迭代 | 167~1670 | **10~100** 快测，推演 1s 吞吐 |
| 对比 | 必须 vs HybridCLR 社区版 | hotc233 自检；正式发布需接入 HybridCLR/xLua 同形状列 |

官方 base **仍保留**作 L1 方向守门；本套件 **不替代** L1，也 **不参与** `HOTC233_ENFORCE_BEAT_COMMUNITY`。

## 场景（20 条）

| slug | Unity API / 形状 | GodDomain `Kernel*` |
|------|------------------|---------------------|
| `hotupdate-unity-combat-tick` | 纯热更逻辑 | — |
| `hotupdate-unity-quest-dispatch` | 纯热更逻辑 | — |
| `hotupdate-unity-chat-append` | StringBuilder + List | — |
| `hotupdate-unity-entity-hot-loop` | Transform.position + SetActive | `KernelEntityHotLoop` |
| `hotupdate-unity-prefab-spawn-despawn` | Instantiate + GetComponent + Destroy | `KernelPrefabSpawnDespawn` |
| `hotupdate-unity-get-component-loop` | GetComponent\<T\> | `KernelGetComponentLoop` |
| `hotupdate-unity-add-component-spawn` | AddComponent + Destroy | `KernelAddComponentSpawn` |
| `hotupdate-unity-camera-world-to-screen` | Camera.WorldToScreenPoint | `KernelCameraWorldToScreen` |
| `hotupdate-unity-physics-raycast` | Physics.Raycast | `KernelPhysicsRaycast` |
| `hotupdate-unity-transform-full-loop` | position + rotation + localScale | `KernelTransformFullLoop` |
| `hotupdate-unity-behaviour-enable-toggle` | Behaviour.enabled | `KernelBehaviourEnableToggle` |
| `hotupdate-unity-compare-tag-loop` | GameObject.CompareTag | `KernelCompareTagLoop` |
| `hotupdate-unity-transform-find-child` | Transform.Find | `KernelTransformFindChild` |
| `hotupdate-unity-time-delta-loop` | Time.deltaTime / frameCount | `KernelTimeDeltaLoop` |
| `hotupdate-unity-gameobject-layer-loop` | GameObject.layer | `KernelGameObjectLayerLoop` |
| `hotupdate-unity-transform-get-position-loop` | Transform.position 读 | `KernelTransformGetPositionLoop` |
| `hotupdate-unity-input-get-axis-loop` | Input.GetAxisRaw | `KernelInputGetAxisLoop` |

| `hotupdate-unity-audio-source-volume-loop` | AudioSource.volume get/set | `KernelAudioSourceVolumeLoop` |

| `hotupdate-unity-animator-set-float-loop` | Animator.SetFloat / GetFloat | `KernelAnimatorSetFloatLoop` |

| `hotupdate-unity-renderer-enabled-toggle` | Renderer.enabled | `KernelRendererEnabledToggle` |

代码：`Assets/CodeHotUpdate/Feature/UnityHotUpdateRealWorldProbe.cs`  
校验：`Reference*` 解释执行 golden；`Kernel*` 可被 GodDomain native bypass（`Hotc233FastPath_UnityKernel_*`）。

## 命令

```powershell
go run ./tools/hotc233ctl unity-realworld-benchmark -project . -loader-profile RuntimeFast

$env:HOTC233_REALWORLD_ITERATIONS='30'
go run ./tools/hotc233ctl unity-realworld-benchmark -project . -loader-profile RuntimeFast -force-rebuild
```

报告：`Assets/EditorForBuild/Generated/performance-unity-realworld-hotupdate.{json,md}`

## 兼容性与性能分离

`unity-realworld-benchmark` 只证明 20 行真实热更性能探针完整输出且 checksum 正确；它不能替代生产兼容性套件。生产兼容性套件必须：

- 每个 Unity API/AOT 调用形状至少执行 5 次；
- 随机扰动输入值、调用顺序和对象状态；
- 单独覆盖 raw API 形状：`enabled`、`Renderer.enabled`、`GameObject.activeSelf/SetActive`、`Transform.position/rotation/localScale`、`CompareTag(string)`、`GetComponent<T>`、`AddComponent<T>`、`AddComponent(Type)`、`Instantiate/Destroy`、`Input`、`AudioSource`、`Animator`；
- 失败时修解释器/native ABI，不能只修改 benchmark 热更代码写法；
- 通过后再跑性能矩阵。

当前短验收口径（2026-06-28）：`StandaloneWindows64 IL2CPP Player + RuntimeFast + 50 iterations`，20/20 行无 crash、无 correctness failure，Player 内探针约 20ms，整条命令约 46s。此结果只是短验收，不等同于 0.0.1 生产发布完成。

## 发布性能矩阵

正式 `性能报告.md` 必须包含全量表，不允许省略：

- HybridCLR 官方 14 base：hotc233 / HybridCLR 社区版 / Pro 估算 / xLua；
- 业务热更代码：hotc233 / HybridCLR 社区版 / xLua（如同形状脚本存在）；
- Unity API 热更代码：20 条 `hotupdate-unity-*`，并标记是否为 raw Unity API ABI 已独立兼容覆盖；
- 超时、少行、crash、correctness failure 均为未通过，不得写成性能数字。

## GodDomain 架构

- Transform：`TryBuildGodDomainUnityKernelMethod` 匹配 `Kernel*`（`int(int)->int`）
- Runtime：`TryExecuteUnityKernelFastPath` + `GodDomainRunUnityKernel`（`GodDomainUnityKernels.cpp`）
- 校验：`Reference*` 私有方法 **不** 走 Kernel 名，保证解释执行 golden 与 native 可对比

**禁止**：为快测改 benchmark 形状（例如单 GO 冒充官方 N 次 create/destroy）。
