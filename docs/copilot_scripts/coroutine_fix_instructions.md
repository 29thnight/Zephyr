# Coroutine Regression Fix

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
Superinstruction implementation caused coroutine regression:
- Before: 1,405 ns/resume (gate limit: 1,546 ns)
- After:  1,899 ns/resume (35% regression, gate FAIL)

hot_arithmetic is now excellent (3.59ms, 47% improvement). Do NOT regress it.

## Task: Fix coroutine dispatch loop overhead from superinstructions

### Step 1 - Diagnose
Read src/zephyr_gc.inl — find the coroutine dispatch loop (separate from main VM dispatch loop).
Search for where superinstruction handlers (SI_ADD_STORE_LOCAL, SI_LOAD_LOCAL_ADD, SI_CMP_JUMP_IF_FALSE etc.) were added to the coroutine loop.

Check two possible causes:
A) Superinstruction handlers in coroutine loop have extra overhead vs main loop (e.g., missing the int fast path, doing extra work)
B) The superinstruction peephole pass in optimize_bytecode() is generating suboptimal code for coroutine-style code patterns (yield/resume heavy)

### Step 2 - Check coroutine benchmark pattern
The coroutine bench: creates a coroutine that yields 400 times, resumes 401 times.
The inner loop is yield-heavy, not arithmetic-heavy. Superinstructions targeting Add/Sub/Modulo patterns shouldn't affect it much.
The regression likely comes from: added per-dispatch overhead (extra case checks, larger switch) or the coroutine loop was changed structurally.

### Step 3 - Fix options (try in order)
A) If the coroutine loop has SI_ case handlers that are slower than the normal cases they replaced, optimize them to match the main loop handlers exactly.

B) If the coroutine loop now has unnecessary overhead on EVERY opcode (e.g., checking a superinstruction flag or extra bookkeeping per dispatch), remove it.

C) If the coroutine loop is sharing code with the main loop in a way that adds overhead, separate the hot path again.

D) If the superinstruction peephole accidentally transforms coroutine opcode sequences in a way that's slower (e.g., fusing yield-adjacent opcodes incorrectly), add a guard to skip superinstruction fusion when the sequence crosses a Yield opcode.

### Step 4 - Build and test
Find MSBuild under C:\Program Files\Microsoft Visual Studio\ and build Release x64.
Run x64\Release\zephyr_tests.exe — all must pass.

### Step 5 - Benchmark
Run: x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
Verify:
  - coroutine_resume_cost gate: below 1,546 ns/resume (PASS)
  - hot_arithmetic gate: still below 4,000,000 ns (must NOT regress)
  - All other gates: still pass

### Step 6 - Update docs/process.md
Record fix.

Success: coroutine gate passes AND hot_arithmetic stays below 4,000,000 ns AND all tests pass.
