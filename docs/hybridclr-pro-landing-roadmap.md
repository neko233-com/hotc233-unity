# HybridCLR Pro 能力落地路线图

更新时间：2026-06-26  
总纲：[`performance-peak-plan.md`](performance-peak-plan.md)  
矩阵：`pro-landing-matrix.json` · 错题本：`pro-wrong-answer-notebook.md`

---

## 总目标

| 维度 | 巅峰目标 |
|---|---|
| 性能 | 全部 base 行 `hotc233PercentOfTarget >= 100%` |
| 能力 | 10 项 Pro 商业能力 `landed` |
| 内存 | 700KB 指令 / 1.2MB 线程 / 10–25% metadata / 5.2x 代码段 |
| 验证 | Tuanjie WebGL RuntimeFast 每批次回归 |

---

## 四阶段架构

```text
Stage A  Typed Register IR      RegI32* + copy 消除
Stage B  Typed ABI Callsite     RunStaticI4/F4CallTrace → direct thunk cache
Stage C  Typed Array Memory     stride/边界 transform 缓存
Stage D  Pro 商业能力           泛型共享 + metadata + Hotfix/加固
```

### Stage A — Typed Register IR

| 子模块 | 文件 | 验收 |
|---|---|---|
| i32 slot | `Hotc233TypedRegisterIR.h` | coverage > 0 |
| RegI32 lowering | `TransformContext_TypedRegister.cpp` | binop-complex/add 向 Pro 收敛 |
| Vector/Quaternion slot | Phase 3 | vector/quaternion 行 |

**2026-06-26**：RegI32 Phase 2 landed；待 WebGL 回归。

### Stage B — Typed ABI Callsite

| 子模块 | 验收 |
|---|---|
| `RunStaticF4CallTrace` | OK-006，过渡 |
| `RunStaticI4CallTrace` | **2026-06-26 landed**，call-aot-static / param-int |
| direct thunk resolveDatas | call-aot-static >= Pro floor |
| Vector3 ABI | param/return-vector3 行 |

### Stage C — Typed Array Memory IR

| 子模块 | 验收 |
|---|---|
| element size/stride 缓存 | array-op 从 8.63x → Pro |
| 与 OK-007 fusion 合并 | typed lowering 后端 |

### Stage D — 完全泛型共享 + 元数据

| 子模块 | 状态 |
|---|---|
| `enableFullGenericSharing` | partial |
| `Hotc233LoadPolicy` / SHA256 | partial |
| metadata bytes/peak/load 三表 | planned |

---

## 能力落地表

| 能力 | 状态 | 探针 | 下一里程碑 |
|---|---|---|---|
| 完全泛型共享 | partial | `CommercialCapabilityProbe` | 虚调用/接口/嵌套 WebGL 矩阵 |
| 元数据优化 | partial | `Hotc233LoadPolicy` | 三表 10–25% |
| 标准解释优化 | **in-progress** | WebGL base JSON | Stage A/B/C |
| 离线指令优化 | partial | opcode profile | 仅 typed IR lowering |
| Hotfix / 热重载 | partial | Replace/Reload API | 包内示例 + 回滚 |
| 代码加固 | partial | XOR + hash | AES provider |
| 访问控制 | partial | AllowOnly | Editor 配置 |
| Assembly.Load 优化 | partial | loader cache | hit/ms 报告 |
| 解释器栈诊断 | **landed** | `GetInterpreterStackTraceJson` | PDB WebGL |

---

## 迭代命令

```powershell
go run ./tools/hotc233ctl validate-reports -project .
go run ./tools/hotc233ctl pro-gate -project .
go run ./tools/hotc233ctl quick -project .
$env:HOTC233_ALLOW_PRO_TARGET_GAP='1'; go run ./tools/hotc233ctl webgl -project . -loader-profile RuntimeFast
```

`pro-gate` → `Assets/EditorForBuild/Generated/pro-landing-gate.json`

---

## 完成定义

1. 全部 capability `landed`
2. 全部 base 行 >= 100% Pro 目标
3. 错题本无 blocked 复发
4. metadata 三表 + 内存预算达标
5. `validate-reports` 全绿
