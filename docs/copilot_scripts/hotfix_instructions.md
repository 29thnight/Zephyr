# hot_arithmetic Optimization Task

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
Project Zephyr C++20 game scripting VM. Wave D complete (NaN-boxing, Shape IC, Instruction Compression).
hot_arithmetic benchmark: 6,874,135 ns. Target: 5,209,418 ns (24% improvement needed, 400,015 opcodes/run).

## Steps

1. Read src/zephyr_gc.inl — find the VM dispatch loop, specifically Add/Sub/Mul int fast paths and Less/Greater/LessEq/GreaterEq/Equal/NotEqual handlers.

2. Expand int fast paths to comparison ops. Add inline fast path before apply_binary_op for Less, Greater, LessEq, GreaterEq, Equal, NotEqual:
   Value b = pop(); Value a = pop();
   if (a.is_int() && b.is_int()) { push(Value::boolean(a.as_int() COMPARISON b.as_int())); break; }
   push(apply_binary_op(a, b, op)); break;

3. Read src/zephyr_types.inl — check is_int() implementation after NaN-boxing. If it is just a bitmask check, verify that the compiler can merge two is_int() calls into one. Leave as-is if already optimal.

4. Search for adaptive nursery / nursery_adaptive or survival_rate check inside the main dispatch loop or per-opcode path. If found in the hot loop, add a counter guard to only trigger every 64 allocations.

5. Search for walk_environment_chain calls inside the lightweight_args execution path. If called unnecessarily, add an early-out.

6. Build: find MSBuild.exe under C:\Program Files\Microsoft Visual Studio\ and build Release x64.

7. Run tests: x64\Release\zephyr_tests.exe — fix any failures.

8. Run benchmark: x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   Report hot_arithmetic_loop mean_ns and gate pass/fail status.

9. Update docs/process.md with the optimization results.

Success: hot_arithmetic_loop mean_ns below 5,209,418 AND all tests pass.
