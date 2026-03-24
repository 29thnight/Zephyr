# Profiler Overhead Fix

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
Wave E1 is complete. The profiler (6.2) added per-opcode/function hooks that cause a minor overhead even when profiling is inactive. The host_handle_resolve_cost benchmark gate is failing: 809.51ns vs limit 790.00ns.

## Task: Zero-cost profiling when inactive

1. Read include/zephyr/api.hpp and src/zephyr_gc.inl — find the profiling hooks added in Wave E1 (start_profiling, stop_profiling, and any per-call or per-opcode timing code in the VM dispatch loop or call_value path).

2. Ensure the profiling active check is a single boolean flag (e.g., profiling_active_) that is false by default.

3. In the hot path (VM dispatch loop and call_value), the profiling hook must be guarded by this flag. If it already is, verify the compiler is actually optimizing it out. If the flag check is not branch-predictor-friendly (e.g., atomic or volatile), change it to a plain bool.

4. If timing calls (e.g., std::chrono::high_resolution_clock::now()) are called unconditionally even when profiling is off, move them inside the if (profiling_active_) guard.

5. If function entry/exit hooks iterate over a vector or map on every call, replace with a flat array or no-op when inactive.

6. Build: find MSBuild under C:\Program Files\Microsoft Visual Studio\ and build Release x64.

7. Run tests: x64\Release\zephyr_tests.exe — ensure all pass.

8. Run benchmark: x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   Check that host_handle_resolve_cost_vs_v1 gate passes (target: below 790.00ns/resolve).

9. Update docs/process.md with the fix note.

Success: all gates pass (or at minimum host_handle_resolve_cost below 790ns) AND all tests pass.
