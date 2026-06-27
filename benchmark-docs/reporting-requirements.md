# 性能对标报告规范

更新时间：2026-06-27

每次跑完 `local-benchmark` / `benchmark` / `arch-self-review` 后，**必须**向负责人汇报 **14 条官方 base 全量对比表**（不得只报弱项子集）。

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

1. **口径**：平台（默认 `StandaloneWindows64 IL2CPP Player`）、迭代次数（167 口径 / 334 medium / 1670 large）、是否 warmup=0。
2. **Loader profile**：必须 `RuntimeFast`。
3. **Opcode profiler 是否开启**（见下节）——**与社区版对比时必须声明**；社区版是 PC 直跑、无 hotc233 诊断开关。
4. **L1 / L2 结论**：是否全面超越社区版；距 Pro 目标差距最大的 3 行。
5. **报告路径**：`Generated/performance-local-hotc-vs-hybridclr-base.md`（临时）；**必更新** [`benchmark-docs/性能报告.md`](性能报告.md)；机器可读 [`benchmark-docs/results/latest-hotc-vs-hybridclr.json`](results/latest-hotc-vs-hybridclr.json)。

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
