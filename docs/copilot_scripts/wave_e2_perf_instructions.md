# Wave E2 Performance Pass

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
Wave E2 (pattern matching, traits, DAP, snapshot) caused major regressions vs Wave D hotfix baseline:
- hot_arithmetic:  4,615,640 ns → 7,316,370 ns (+58%)
- host_handle:     718 ns/resolve → 1,190 ns/resolve (+66%)
- coroutine:       1,405 ns/resume → 2,426 ns/resume (+73%)

Gates target (wave_d_baseline.json):
- hot_arithmetic: below 5,209,418 ns (25% improvement over wave_c)
- host_handle: below 790 ns/resolve
- coroutine: below 1,546 ns/resume

## Root causes to fix

### 1. DAP breakpoint check in hot dispatch loop
Find the breakpoint check added in src/zephyr_gc.inl VM dispatch loop.
It likely checks a list/set of breakpoints on every opcode. Fix:
- Guard the entire check behind a plain bool `dap_active_` flag (NOT atomic, NOT volatile).
- When dap_active_ is false, zero cost — one branch that the CPU predicts correctly.

### 2. Trait dispatch overhead in call_value or member access
Find where trait method resolution was added (likely in LoadMember or call_value).
Fix: trait lookup should only happen as a fallback when normal struct field lookup fails.
Ensure the hot path (struct field access via Shape IC) is NOT touched by trait code.
The trait registry lookup should be behind: if (ic_miss && obj_type_may_have_trait).

### 3. Pattern matching overhead in dispatch loop
Check if the new match/guard/OR pattern opcodes added any overhead to non-match code paths.
The new BytecodeOps for guards should only run in match arms, not affect other opcodes.

## Steps

1. Read src/zephyr_gc.inl — search for breakpoint check, trait dispatch, and any new per-opcode overhead added by Wave E2.

2. Fix DAP check: ensure it is `if (dap_active_) { check_breakpoint(ip, current_line); }` where dap_active_ is a plain bool defaulting to false.

3. Fix trait dispatch: ensure trait lookup is NOT on the normal LoadMember/StoreMember fast path. It should only be reached after Shape IC miss AND normal slow-path field lookup miss.

4. Fix any per-opcode overhead added for pattern matching guards that runs outside match contexts.

5. Build: find MSBuild under C:\Program Files\Microsoft Visual Studio\ and build Release x64.

6. Run tests: x64\Release\zephyr_tests.exe — all must pass.

7. Run benchmark: x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   Check all three gates:
   - hot_arithmetic_vs_v1: must pass (below 5,209,418 ns)
   - host_handle_resolve_cost_vs_v1: must pass (below 790 ns/resolve)
   - coroutine_resume_cost_vs_v1: must pass (below 1,546 ns/resume)

8. If any gate still fails, read the failing code path again and optimize further. Repeat build/test/bench.

9. Update docs/process.md with performance pass results.

Success: all 3 required gates pass AND all tests pass.
