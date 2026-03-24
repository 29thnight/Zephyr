# Quick-Win Performance Fixes

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Fix 1: host_handle call_value residual overhead

Wave B achieved 691 ns/resolve. Current is 726 ns/resolve (+5%). Same opcount (34,518) and 100% local cache hit rate, so the regression is in call overhead not logic.

### Steps
1. Read src/zephyr_gc.inl — find call_value() and the DAP/trait guards added in Wave E2.
2. Check: are there any extra flag checks (dap_active_, trait registry lookup, profiling_active_) that still execute on EVERY call even when all features are off? Each should be a single predictable bool branch.
3. Check: did Wave E2 add any extra stack frame bookkeeping, scope push/pop, or environment allocation that wasn't there in Wave B?
4. Check the host handle resolution path specifically — find where host handles are resolved (resolve_host_handle or similar). Ensure no temporary std::string or std::string_view construction happens per call that wasn't there before.
5. Fix any unnecessary overhead found. Goal: get back to ~690 ns/resolve range.
6. Build Release x64, run tests, run bench --baseline bench\results\wave_d_baseline.json.
   Confirm host_handle_resolve_cost gate passes with improved ns/resolve vs current 726.

## Fix 2: allocation_full_gc gate always SKIPPED

The gate is skipped because wave_d_baseline.json is missing array_object_churn.mean_full_gc_cycles.

### Steps
1. Read bench/results/wave_d_baseline.json — confirm array_object_churn case exists but mean_full_gc_cycles is 0 (or absent).
2. Read bench/results/v1_baseline.json — confirm it has mean_full_gc_cycles=10 for array_object_churn.
3. The gate compares current full_gc_cycles vs baseline. Since wave_d baseline has 0 full_gc_cycles (GC is so good it never triggers), using wave_d as baseline makes the gate untestable.
4. Solution: update the bench runner or gate config to use v1_baseline.json for the allocation gate specifically, OR add a synthetic entry to wave_d_baseline.json: set array_object_churn.mean_full_gc_cycles to the v1 value (10) so the gate has a meaningful target.
   Preferred: look at how gates are configured (bench source or config file) and set allocation gate to always compare against v1_baseline.json regardless of current baseline.
5. After fix, the gate should show: current=0 vs baseline=10, improvement=100%, which passes (target was 40% improvement).
6. Build, run bench --baseline bench\results\wave_d_baseline.json. Confirm allocation_full_gc gate shows "pass" instead of "skipped".
7. Update docs/process.md with both fixes.

Success: host_handle gate improves (lower ns/resolve than current 726) AND allocation_full_gc gate shows "pass" instead of "skipped". All tests pass.
