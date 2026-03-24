# Wave D — Task 4.4: BytecodeInstruction Compression

## Project Context

Project Zephyr is a C++20 game scripting VM. Source is split across `.inl` files included by `src/zephyr.cpp`.

Completed so far:
- Wave A/B/C: all done
- Wave D 1.4: NaN-boxing done (Value = uint64_t, sizeof(Value)==8)
- Wave D 1.5: Shape IC done (StructInstanceObject uses Shape* + vector<Value>, IC slots on BytecodeInstruction)

Key files:
- `src/zephyr_types.inl` — Value, GcObject definitions
- `src/zephyr_compiler.inl` — BytecodeOp, BytecodeInstruction, BytecodeCompiler, BytecodeFunction
- `src/zephyr_gc.inl` — GC + VM dispatch loop
- `tests/tests.cpp` — test suite
- `docs/plan.md` — full plan reference
- `docs/process.md` — progress log (update when done)

## Task 4.4: BytecodeInstruction Compression

### Goal
Shrink `BytecodeInstruction` from its current large size (~120+ bytes due to `std::string`, `std::optional`, `std::vector`, `std::shared_ptr`) down to a compact 12-byte hot struct, moving cold data to a sidecar table.

### Implementation Plan

#### Step 1 — Read current BytecodeInstruction layout
First read `src/zephyr_compiler.inl` to understand the exact current fields of `BytecodeInstruction`.

#### Step 2 — Define CompactInstruction

```cpp
struct CompactInstruction {
    BytecodeOp op;        // 1-4 bytes (enum)
    int32_t operand;      // 4 bytes  (covers int operand, jump offset, constant index, slot index)
    uint32_t span_line;   // 4 bytes  (source line for error reporting)
    // IC slots from 1.5 (mutable, added separately if needed):
    mutable Shape* ic_shape = nullptr;  // pointer — keep inline if fits budget, else sidecar
    mutable uint32_t ic_slot = 0;
};
```

If IC pointers bloat the struct too much, move them to a parallel `std::vector<IcSlot>` sidecar indexed by instruction index.

#### Step 3 — Define InstructionMetadata sidecar (cold data)

```cpp
struct InstructionMetadata {
    std::string string_operand;       // for LoadConst string, LoadGlobal name, etc.
    std::vector<int32_t> jump_table;  // for switch/match
    // any other cold fields
};

// Stored as: std::vector<InstructionMetadata> metadata_; // same size as instructions_
// Access: metadata_[instruction_index]
```

#### Step 4 — Update BytecodeFunction

```cpp
struct BytecodeFunction {
    std::vector<CompactInstruction> instructions;
    std::vector<InstructionMetadata> metadata;   // parallel array, same size
    std::vector<Value> constants;                // constant pool
    // ... existing fields (local_count, upvalue_count, etc.)
};
```

#### Step 5 — Update compiler (emit side)

In `BytecodeCompiler`, wherever instructions are emitted (`emit()`, `emit_jump()`, etc.):
- Fill `CompactInstruction` with op + operand + span_line
- Fill `InstructionMetadata` with string_operand, jump_table if needed

#### Step 6 — Update VM dispatch loop

In the dispatch loop, change instruction access from:
```cpp
const BytecodeInstruction& instr = instructions[ip];
instr.string_operand  // direct access
```
to:
```cpp
const CompactInstruction& instr = instructions[ip];
const InstructionMetadata& meta = metadata[ip];  // only when cold data needed
meta.string_operand
```

Hot opcodes (Add, Sub, Mul, LoadLocal, StoreLocal, Jump, etc.) should NEVER access metadata — they only use `instr.op` and `instr.operand`.

#### Step 7 — Add compile-time size check

```cpp
static_assert(sizeof(CompactInstruction) <= 24, "CompactInstruction too large");
```

Aim for ≤24 bytes including IC slots, ideally ≤16.

### Build & Test

```
msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /m
x64\Release\zephyr_tests.exe
```

Fix any failures. Then update `docs/process.md` to mark 4.4 Instruction Compression as ✅ 완료 with sizeof note.

### Success Criteria
- `static_assert(sizeof(CompactInstruction) <= 24)` passes
- All existing tests pass
- Hot dispatch path accesses no cold metadata for arithmetic/jump opcodes
- `docs/process.md` updated

### Working Directory
`C:\Users\lance\OneDrive\Documents\Project Zephyr`
