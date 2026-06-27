#ifndef HOTC233_TRANSFORM_POLICY_H
#define HOTC233_TRANSFORM_POLICY_H

// Pro-first defaults. Set HOTC233_COMMUNITY_BASELINE=1 only for OSS v8.11 diff/debug.
#ifndef HOTC233_COMMUNITY_BASELINE
#define HOTC233_COMMUNITY_BASELINE 0
#endif

#ifndef HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM
// CONTROL EXPERIMENT (temporary): 0 disables typed-register lowering to isolate
// whether it regresses AOT/Unity call rows. Restore to 1 after measuring.
#define HOTC233_ENABLE_PRO_EXPERIMENTAL_TRANSFORM 0
#endif

#ifndef HOTC233_ENABLE_PRO_TRACE_FOLDING
#define HOTC233_ENABLE_PRO_TRACE_FOLDING 0
#endif

#endif
