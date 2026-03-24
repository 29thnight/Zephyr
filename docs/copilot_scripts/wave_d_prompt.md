# Wave D — Task 1.4: NaN-Boxing Implementation

## Project Context

Project Zephyr is a C++20 game scripting VM. The main source is split across `.inl` files included by `src/zephyr.cpp`. Wave A/B/C optimizations are complete. Now implement Wave D.

Key files:
- `src/zephyr.cpp` — main file (includes all .inl files)
- `src/zephyr_types.inl` — Value struct, GcObject, object types (search here for current Value definition)
- `src/zephyr_compiler.inl` — BytecodeOp, BytecodeInstruction, BytecodeCompiler
- `src/zephyr_gc.inl` — GC implementation
- `src/zephyr_vm.inl` — VM dispatch loop
- `tests/tests.cpp` — test suite
- `include/zephyr/api.hpp` — public API
- `docs/plan.md` — full plan reference

## Task 1.4: NaN-Boxing (Value 16B → 8B)

### Goal
Replace the current `Value` struct (likely using a tagged union ~16 bytes) with a single `uint64_t bits_` NaN-boxed representation (8 bytes).

### NaN-Boxing Encoding

```
Double:      normal IEEE 754 double (exponent not all-ones, or mantissa=0)
Pointer:     0xFFFC_0000_0000_0000 | ptr48
Int48:       0xFFFE_0000_0000_0000 | (value & 0x0000_FFFFFFFFFFFF)
Nil:         0xFFFA_0000_0000_0000
Bool(true):  0xFFFB_0000_0000_0001
Bool(false): 0xFFFB_0000_0000_0000
HostHandle:  0xFFF8_0000_0000_0000 | (slot << 32) | generation
```

### Implementation Rules

1. Change `Value` struct to use `uint64_t bits_` as the ONLY data field (8 bytes).
2. Keep ALL existing public API methods: `is_int()`, `is_double()`, `is_bool()`, `is_nil()`, `is_string()`, `is_object()`, `as_int()`, `as_double()`, `as_bool()`, `as_object()`, factory methods `Value::integer()`, `Value::from_double()`, `Value::boolean()`, `Value::nil()` etc.
3. Update internal implementations of all these methods to use NaN-boxing bit manipulation.
4. Update everywhere that directly accesses Value internals: `gc_fixup_value`, `value_to_string`, `values_equal`, `apply_binary_op`, and the VM dispatch loop.
5. Int48 range check: if integer value is out of 48-bit signed range, throw a runtime error.
6. Add a `static_assert(sizeof(Value) == 8)` compile-time check.
7. Do NOT change public-facing behavior — existing tests must still pass.

### Steps

1. First, read `src/zephyr_types.inl` (or wherever Value is defined) to understand the current struct layout.
2. Search for all `Value` field accesses across the codebase to enumerate what needs updating.
3. Implement the NaN-boxed Value.
4. Update all affected sites.
5. Build: `msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /m`
6. Run tests: `x64\Release\zephyr_tests.exe`
7. Fix any failures.
8. After tests pass, update `docs/process.md` — mark 1.4 NaN-boxing as ✅ 완료 with a note.

### Working Directory
`C:\Users\lance\OneDrive\Documents\Project Zephyr`

### Success Criteria
- `static_assert(sizeof(Value) == 8)` compiles
- All existing tests pass
- `docs/process.md` updated
