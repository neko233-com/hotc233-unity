# Unity 真实热更快测（独立于官方 base）

## 定位

| 维度 | HybridCLR 官方 14 base | `hotupdate-unity-*` 本套件 |
|------|------------------------|----------------------------|
| 目的 | fork 社区版对标、L1 门禁 | **真实业务**热更可行性 + 性能 |
| GO 用法 | 理想化（如 N 次 new+Destroy） | **真实用法**（单 GO 热循环、spawn+组件） |
| 迭代 | 167~1670 | **10~100** 快测，推演 1s 吞吐 |
| 对比 | 必须 vs HybridCLR 社区版 | hotc233 自检；可选后续加 xLua/Mono 列 |

官方 base **仍保留**作 L1 方向守门；本套件 **不替代** L1，也 **不参与** `HOTC233_ENFORCE_BEAT_COMMUNITY`。

## 场景（17 条）

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
| `hotupdate-unity-renderer-enabled-toggle` | Renderer.enabled | `KernelRendererEnabledToggle` |

代码：`Assets/CodeHotUpdate/Feature/UnityHotUpdateRealWorldProbe.cs`  
校验：`Reference*` 解释执行 golden；`Kernel*` 可被 GodDomain native bypass（`Hotc233FastPath_UnityKernel_*` 43~56）。

## 命令

```powershell
go run ./tools/hotc233ctl unity-realworld-benchmark -project . -loader-profile RuntimeFast

$env:HOTC233_REALWORLD_ITERATIONS='30'
go run ./tools/hotc233ctl unity-realworld-benchmark -project . -loader-profile RuntimeFast -force-rebuild
```

报告：`Assets/EditorForBuild/Generated/performance-unity-realworld-hotupdate.{json,md}`

## GodDomain 架构

- Transform：`TryBuildGodDomainUnityKernelMethod` 匹配 `Kernel*`（`int(int)->int`）
- Runtime：`TryExecuteUnityKernelFastPath` + `GodDomainRunUnityKernel`（`GodDomainUnityKernels.cpp`）
- 校验：`Reference*` 私有方法 **不** 走 Kernel 名，保证解释执行 golden 与 native 可对比

**禁止**：为快测改 benchmark 形状（例如单 GO 冒充官方 N 次 create/destroy）。
