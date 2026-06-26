# hotc233 巅峰性能总纲

更新时间：2026-06-26  
验收平台：**Tuanjie WebGL 本地 IL2CPP 浏览器**（唯一 IL2CPP 实测入口）  
机器可读矩阵：`pro-landing-matrix.json`  
错题本：`pro-wrong-answer-notebook.md`  
能力路线：`hybridclr-pro-landing-roadmap.md`

---

## 北极星

hotc233 的唯一竞品是 **HybridCLR Pro 纯解释器 + 公开商业能力**，不是社区版，不是 DHE，不是首包 AOT 预置热更逻辑。

| 维度 | 巅峰目标 |
|---|---|
| 性能 | 全部官方 base benchmark 行 `hotc233PercentOfTarget >= 100%`（相对 Pro 架构目标，约 **76.9%** 原生 WebGL IL2CPP） |
| 内存 | 指令优化 ~700KB、每解释线程 ~1.2MB、元数据节省 **10–25%**、热更代码段 ~**5.2x** |
| 能力 | `pro-landing-matrix.json` 10 项商业能力全部 `landed` |
| 微信小游戏 | `RuntimeFast` + minigame 安全配置全绿；`weixinMiniGameUseSlimMetaFileFormat=0` |

**禁止路线**（见错题本 WA-001~008）：通用 linear trace、全局 copy propagation、benchmark 形状 opcode 堆叠、per-method 状态缓存膨胀。

---

## 根因（为什么还差 Pro 4–14x）

差距来自 **解释器架构**，不是 Tuanjie 编译耗时：

```text
IL 栈机 → 大量 Ldloc copy + 巨型 opcode dispatch + 运行时 metadata/callsite 解析
```

`typeof` 已超过 Pro 目标，证明 **transform 期 resolve / cache** 是正确模式。数值、Vector、数组、AOT call 仍差 Pro **4.35x–13.86x**，必须走 typed 架构四阶段，不能继续栈机层 patch。

---

## 四阶段架构（唯一主路径）

```text
Stage A  Typed Register IR     IL stack → i32/f32/Vector slot + copy 消除
Stage B  Typed ABI Callsite    AOT static/instance + Vector3/Quaternion typed layout
Stage C  Typed Array Memory    element size/stride/边界策略 transform 期缓存
Stage D  Pro 商业能力          完全泛型共享 + 元数据优化三表 + Hotfix/加固/访问控制
```

### Stage A — Typed Register IR（P1，进行中）

| 模块 | 文件 | 状态 |
|---|---|---|
| i32 slot 规划 | `Hotc233TypedRegisterIR.h` | landed |
| RegI32* 独立 opcode（无内层 trace） | `Instruction.h`, `Interpreter_Execute.cpp` | landed |
| copy 消除 + fused 拆分 lowering | `TransformContext_TypedRegister.cpp` | landed |
| Vector3/Quaternion slot | 同上 Phase 3 | planned |

### Stage B — Typed ABI Callsite（P1，进行中）

| 模块 | 文件 | 状态 |
|---|---|---|
| static f4 窄 trace | `RunStaticF4CallTrace` | landed（OK-006） |
| static i4 窄 trace | `RunStaticI4CallTrace` | **landed 2026-06-26** |
| direct thunk callsite cache | transform resolveDatas | planned |
| Vector3 param/return ABI | transform + MethodBridge | planned |

### Stage C — Typed Array Memory IR（P1）

| 模块 | 状态 |
|---|---|
| `i4[]` read-add-write fusion | partial（OK-007，仍差 Pro ~8.6x） |
| element stride + 边界 hoist | planned |

### Stage D — Pro 商业能力（P0/P2）

见 `pro-landing-matrix.json` 与 `hybridclr-pro-landing-roadmap.md`。

---

## Tuanjie 性能验证闭环

每次**完整落地批次**后必须跑：

```powershell
# 推荐：一条命令完成 L0 门禁 + 增量 WebGL（见 docs/flywheel-automation.md）
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'
go run ./tools/hotc233ctl flywheel -project . -loader-profile RuntimeFast

# 或分步：
go run ./tools/hotc233ctl validate-reports -project .
go run ./tools/hotc233ctl pro-gate -project .
go run ./tools/hotc233ctl quick -project .
go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
```

飞轮自动化详解：[`flywheel-automation.md`](flywheel-automation.md)

| 层级 | 耗时 | 用途 |
|---|---:|---|
| L0 validate-reports | 秒级 | opcode 表、Pro 文档、RuntimeFast/minigame |
| L1 pro-gate + quick | 秒级 | 能力矩阵、错题本、base JSON 缺口排序 |
| L3 webgl | ~80–95s 正常 | **唯一** IL2CPP 浏览器证据 |
| L4 hybridclr-webgl | 更长 | 同机 HybridCLR 8.11.0 环境校准 |

决策规则：

- WebGL marker **691s 超时** → 立即记 blocked、撤回本轮改动
- 连续 **2 次**同方向无接近 Pro → 停止，转下一 Stage
- `quick` 报告 `reportStale=true` 或上次 webgl failed → 禁止用旧 JSON 判收益

---

## 当前有效性能快照

最新 Tuanjie WebGL RuntimeFast：**2026-06-26T14:12:59Z**（RegI32 + `RunStaticI4CallTrace` 全量 rebuild）

| operation | 本轮 ms | 相对上轮 (14:10) | 相对 12:40 基准 | Stage |
|---|---:|---:|---:|---|
| hybridclr-binop-add | 155.6 | +5.6% | ~2.0x 提升 (308→155) | A |
| hybridclr-binop-complex | 730.8 | +1.4% | 仍慢于基准 (547→731) | A 监控 |
| hybridclr-call-aot-static-method | 103.7 | -8.6% | 待 trace 命中验证 | B 监控 |
| hybridclr-typeof | — | 回归监控 | 已超 Pro | — |

`RunI4LinearTrace` 轮次（691s 超时）**永久作废**，不得引用。

报告：`Assets/EditorForBuild/Generated/performance-webgl-local-il2cpp.json`

---

## 完成定义

1. 全部 base 行 `hotc233PercentOfTarget >= 100%`
2. `pro-landing-matrix.json` 全部 capability `landed`
3. 错题本无 active blocked 复发
4. metadata 三表在 Pro 公开区间
5. `validate-reports` + full verification 全绿

在此之前对外口径见 `hybridclr-gap-analysis.md`。
