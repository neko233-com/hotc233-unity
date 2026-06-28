#ifndef HOTC233_TRANSFORM_POLICY_H
#define HOTC233_TRANSFORM_POLICY_H

// GodDomain: transform recognizes hot shapes → minimal IR → whole-method bypass.
// Generic Interpreter_Execute dispatch loop is COLD PATH ONLY — not a P1 optimization target.

#ifndef HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM
#define HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM 1
#endif

#ifndef HOTC233_ENABLE_UNITY_KERNEL_GODDOMAIN
#define HOTC233_ENABLE_UNITY_KERNEL_GODDOMAIN 0
#endif
#ifndef HOTC233_ENABLE_DEDICATED_TRANSFORM
#define HOTC233_ENABLE_DEDICATED_TRANSFORM HOTC233_ENABLE_GOD_DOMAIN_TRANSFORM
#endif

// GodDomain IL scan builders (CallAOTStatic, etc.) skip generic TransformBodyImpl.

#ifndef HOTC233_COMMUNITY_BASELINE
#define HOTC233_COMMUNITY_BASELINE 0
#endif

// P0: offline call-trace fusion (CallAOTStatic 10× f4, etc.). Part of GodDomain transform, not dispatch tuning.
#ifndef HOTC233_ENABLE_PRO_CALL_TRACE
#define HOTC233_ENABLE_PRO_CALL_TRACE 1
#endif

// P0: transform-time bake of methodPointer into resolveDatas (0 => runtime interp fallback on COLD path only).
#ifndef HOTC233_ENABLE_DIRECT_CALLSITE_CACHE
#define HOTC233_ENABLE_DIRECT_CALLSITE_CACHE 1
#endif

// FORBIDDEN as performance work on MSVC: threaded dispatch (168 HOTC233_EXEC_* labels) net-negative.
// Hot paths must use TryExecuteHotc233FastPath bypass, not faster switch loops.
#ifndef HOTC233_ENABLE_THREADED_DISPATCH
#if defined(_MSC_VER) && !defined(__clang__)
#define HOTC233_ENABLE_THREADED_DISPATCH 0
#else
#define HOTC233_ENABLE_THREADED_DISPATCH 0
#endif
#endif

// DEPRECATED / FORBIDDEN default: RegI32 lowering without typed ABI boundary (L1 regressed 2–9×).
#ifndef HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM
#define HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM 0
#endif

#ifndef HOTC233_ENABLE_PRO_TRACE_FOLDING
#define HOTC233_ENABLE_PRO_TRACE_FOLDING 0
#endif

// Narrow register IR: BinOpVarVarVar_Add_i4 chains -> RunRegI32AddTrace only (no RegI32 lowering pass).
#ifndef HOTC233_ENABLE_BINOP_I4_TRACE
#define HOTC233_ENABLE_BINOP_I4_TRACE 0
#endif

// P0: fault-in AOT code page at transform time (off timed first-call path). Off on emscripten.
#ifndef HOTC233_ENABLE_AOT_CODE_PRETOUCH
#define HOTC233_ENABLE_AOT_CODE_PRETOUCH 1
#endif

// Generic dispatch/M2N bridge must not be optimized as hot path (documentation gate for reviewers).
#ifndef HOTC233_ALLOW_GENERIC_DISPATCH_HOTPATH_OPTIMIZATION
#define HOTC233_ALLOW_GENERIC_DISPATCH_HOTPATH_OPTIMIZATION 0
#endif

#endif
