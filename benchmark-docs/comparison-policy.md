# 性能对标策略

更新时间：2026-07-01

## 北极星

hotc233-unity 面向生产环境，目标是通用、高性能、商业可用的热更解释器。性能目标参考 HybridCLR 商业版相对 CE 的公开量级，但不同操作使用不同 floor。

## 验收层级

| 层级 | 目标 | 门禁 |
|---|---|---|
| P0 商业能力 | 热更加载、热重载、加固、访问控制、Assembly 缓存、崩溃栈诊断 | `validate-reports` |
| P1 生产性能 | 14 条官方 base 达到分层 floor | `local-benchmark` |
| P2 Pro 接近 | 继续逼近报告中的 Pro 目标估算 | 后续专项 |

## CE 对照原则

- CE 工程只用于产出同机基线，默认不随 hotc233 每轮重复构建。
- CE 报告缓存路径：`Assets/EditorForBuild/Generated/hybridclr-local-player-report.json`。
- 缓存缺失或 `HOTC233_REFRESH_HYBRIDCLR_CE=1` 才刷新 CE。
- 任何性能结论都必须同时引用 hotc233 JSON、CE JSON 和聚合报告。

## 分层 Floor

`local-benchmark` 默认检查：

```text
hybridclr-typeof: 1000%
hybridclr-binop-add / hybridclr-binop-complex: 500%
其它 hybridclr-* base: 300%
```

公开图里的 500% 只覆盖对应算术操作，不扩展到所有 benchmark。低于自身 floor 的 base 行进入下一轮生产级优化清单；低于 100% 则说明连 CE 基础线都没有超过，必须优先排查架构方向。

报告字段要求：

- `floorPercent`: 本行生效 floor；business 默认 `0`。
- `floorScope`: `typeof-public-pro-target`、`hybridclr-business-edition-arithmetic`、`general-base-production-target`、`observe-only` 等。
- `floorSource`: 说明 floor 来自 HybridCLR 官方 Pro/Business 公开口径、hotc233 生产目标或显式环境变量。
- `floorStatus`: `passed`、`failed` 或 `observe-only`。
- `HOTC233_COMMUNITY_NEAR_PERCENT` 只作诊断覆盖；开启后报告顶层必须写 `floorOverrideEnabled=true`。

## 禁止事项

- 禁止修改官方/base 1000 次和 business 10 次。
- 禁止用缺行、过滤行、增大循环、替换 benchmark 形状通过门禁。
- 禁止恢复 xLua 或第三套对照组。
- 禁止用 WebGL/flywheel/Mono 报告替代本机 CE 对照。
- 禁止把大量热更业务预置进首包 AOT 规避解释器成本。

## 优化路线

优先做能普适到生产项目的解释器能力：

1. 热形状识别和专用 transform。
2. whole-method bypass，绕开通用大 switch 热路径。
3. typed ABI，特别是 `Vector3`、`Quaternion`、值类型参数与返回。
4. register/array IR 和低 GC 调用桥。
5. 最后才做 peephole 或单 opcode 微调。
