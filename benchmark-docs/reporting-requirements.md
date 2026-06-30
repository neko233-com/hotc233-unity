# 性能对标报告规范

更新时间：2026-06-27

每次跑完 `local-benchmark` / `benchmark` / `arch-self-review` 后，**必须**向负责人汇报 **14 条官方 base 全量对比表**（不得只报弱项子集）。若未设置 `HOTC233_INCLUDE_BUSINESS_BENCHMARK=0`，**同时**汇报 **10 条 `business-realworld-*` 业务场景表**（见 `RealWorldBusinessPerformanceProbe.cs`）。

## 0.0.1 生产发布硬门禁

生产发布前必须先过兼容性，再跑性能；两类测试不得混在一个结论里。功能支持状态统一维护在 [`compatibility-support-matrix.md`](compatibility-support-matrix.md)。未列入“已验证”的能力不得在发布材料中宣称支持；`extern` / PInvoke / native plugin 等特殊边界可以标记为暂忽略，但必须在表格中说明。

| 阶段 | 命令/报告 | 停止条件 |
|---|---|---|
| 快速兼容性 | `go run ./tools/hotc233ctl compat-fast -project . -timeout 15m` | Editor 内 AB 热更加载；不构建 Player、不跑性能；0 crash、0 correctness failure、0 timeout |
| 兼容性 | `validate-reports` + Unity 真实热更兼容套件 | 每个调用形状至少 5 次；包含随机输入/随机顺序；0 crash、0 correctness failure、0 timeout |
| Unity API 真实热更 | `unity-realworld-benchmark` | `hotupdate-unity-*` 全量行输出；报告不得少行；raw API 形状失败时必须修 runtime/native 路径或显式列入不准发布项 |
| 官方性能 | `local-benchmark` | 14 条官方 base 全表；hotc233、HybridCLR CE、Pro 估算列不得省略；base 固定 1000 次 |
| 业务性能 | `local-benchmark` 默认业务表 | 业务热更场景全表；不得只汇报快的子集 |

兼容性套件的随机规则：同一 API 形状至少跑 5 次，输入需覆盖边界值与普通值，例如 `iterations`、bool toggles、Vector3/Quaternion、string tag/name、layer、组件启用状态。随机用于防止只针对固定测试写专用 bypass；随机失败视为 correctness failure。

性能套件的时间规则：命令默认 timeout 不得超过 15 分钟；已构建 Player 的性能采样目标为 1 分钟内完成。native/IL2CPP 源码变更导致的重编时间要单独记为构建成本，不得混入 Player 内性能耗时。

禁止发布项：

- 任一兼容性行 crash、timeout、少行或 correctness failure。
- 任一 hotc233 专用 Unity API kernel 只返回 checksum、但真实 raw API native ABI 未有独立兼容测试覆盖。
- `性能报告.md` 使用 stale `Generated/` 残留、手写数字、或省略 HybridCLR CE / Pro 估算 / 业务表。
- 为了通过测试而修改用户热更代码写法；除非该写法本身不可确定或 Unity/IL2CPP 不支持。

## 必报字段

| 列 | 说明 |
|---|---|
| 项目 | 14 条 base 官方 slug + 中文标题 |
| hotc233 | `elapsedMs` + `opsPerSecond` |
| 社区版 | 同机 `hybridclr-benchmark-demo` 实测 |
| Pro 估算 | `hotc233ctl` 按官方 8.11.0 文档区间推算的 `professionalTarget*` |
| hotc / 社区 | `hotc233PercentOfHybridClr`（**L1 门槛：> 100%**） |
| hotc / Pro | `hotc233PercentOfProfessionalTarget`（L2 方向） |
| 追社区还需 | `hybridClrCatchUpMultiplier`（>1 表示 hotc 仍慢） |
| 追 Pro 还需 | `professionalCatchUpMultiplier` |
## 必报元信息（表头或表前 bullet）

1. **口径**：平台（默认 `StandaloneWindows64 IL2CPP Player`）、迭代次数（official/base 固定 1000；business 固定 10）、是否 warmup=0。
2. **Loader profile**：必须 `RuntimeFast`。
3. **Opcode profiler 是否开启**（见下节）——**与社区版对比时必须声明**；社区版是 PC 直跑、无 hotc233 诊断开关。
4. **L1 / L2 结论**：是否全面超越社区版；距 Pro 目标差距最大的 3 行。
5. **报告路径**：`Generated/performance-local-hotc-vs-hybridclr-base.md`（临时）；**自动生成** [`benchmark-docs/性能报告.md`](性能报告.md)（含 base + business 双表 + 自动摘要）；机器可读 [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)。

## 业务场景表（追加，非 L1 硬门禁）

| 前缀 | 来源 | L1 | 拉伸门禁 |
|---|---|---|---|
| `business-realworld-*` | `Assets/CodeHotUpdate/Feature/RealWorldBusinessPerformanceProbe.cs`（HybridCLR 同形状在 `hybridclr-benchmark-demo`） | 不参与 | 可选 `HOTC233_ENFORCE_BUSINESS_FLOOR=500`（默认 500%） |

Player 参数：`-hotc233-performance-suite`（`local-benchmark` 默认开启；`HOTC233_INCLUDE_BUSINESS_BENCHMARK=0` 仅跑官方 14 条）。

业务表硬规则：

- hotc233 与 HybridCLR 社区版必须使用同名 `business-realworld-*` 行、同迭代数、同平台、同 Player 口径计算 `hotc / HybridCLR`；不得通过增大任一侧迭代次数来换取稳定读数。
- 默认商业版报告必须包含 10 条业务行及对应 10 条社区版同名行；缺行、`hotc-only`、`not-required-for-business` 都是 blocker，不能作为生产结论。
- `headless` 只能验证已生成报告的结构与门禁，不得替代真实 Player 对比；业务性能百分比只能来自 `local-benchmark` 或明确标注的平台专项 Player 报告。

架构说明可选写入 [`benchmark-docs/performance-architecture-notes.md`](performance-architecture-notes.md)，会并入 `性能报告.md` 的「架构要点」节。

## Opcode Profiler（性能统计）开关

| 状态 | 构建宏 / 运行参数 | 对性能影响 | 能否用于 L1/L2 验收 |
|---|---|---|---|
| **关（默认，验收口径）** | `HOTC233_ENABLE_OPCODE_PROFILER=0`（`Instruction.h` 默认）；Player **不带** `-hotc233-opcode-profile` | 无额外分派计数开销 | **是** |
| **开（仅诊断）** | 构建 `-DHOTC233_ENABLE_OPCODE_PROFILER=1` **或** Player 带 `-hotc233-opcode-profile` / URL `hotc233-opcode-profile=1` | 主循环每条 opcode 计数，全表通常慢 **~30%–100%**（见 `docs/runtime-rewrite-plan.md`） | **否** |

**规则**：

- 向负责人汇报的「正式性能表」必须是 **profiler 关闭** 的同机 `local-benchmark`。
- 若某次跑数开了 profiler，须在表头写明 **「⚠ 诊断跑数（opcode profile ON），不可与社区版/历史正式表对比」**，并另附一次 profile OFF 复测。
- WebGL 专项 opcode profile 仅用于定位 IR 路径，不参与 Pro/社区版门禁。

## 同机公平性（相对 HybridCLR 社区版）

社区版数据来自独立工程 `hybridclr-benchmark-demo`，**PC IL2CPP Player 直跑**，无 Unity Editor 弹窗、无 headless browser。

hotc233 对标时必须：

- 同 Tuanjie 小版本、同 `StandaloneWindows64`、同 14 条探针形状；
- 顺序执行（禁止 multitask / 并行探针）；
- 不用 `flywheel`、旧 business 探针或 WebGL 快捷路径充当 PC 结论。

## Agent / CI 汇报模板（复制即用）

```markdown
### 性能全量对比（UTC: …）

- 口径：StandaloneWindows64 IL2CPP · RuntimeFast · 167 官方迭代 · warmup=0
- Opcode profiler：**关** / **开（诊断，非验收）**
- L1 社区版：**通过 / 未通过**（未通过行：…）
- 源报告：`…/performance-local-hotc-vs-hybridclr-base.md`
- 包内唯一人类可读表：`benchmark-docs/性能报告.md`

| 项目 | hotc233 | 社区版 | Pro 估算 | hotc/社区 | hotc/Pro |
|------|---------|--------|----------|-----------|----------|
| …14 行… |
```

## 归档

正式结论同步到 **`benchmark-docs/性能报告.md`**（人类可读）与 **`benchmark-docs/results/latest-hotc-vs-hybridclr.json`**（机器可读）；`Generated/` 仅作当次临时产物。
