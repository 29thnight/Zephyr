# Superinstruction Implementation

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
hot_arithmetic currently: 4,954,235 ns at 12.39 ns/op (400,015 opcodes).
Goal: ~8-10 ns/op via superinstructions (combining common opcode pairs into single dispatch).

## What are superinstructions?
Instead of dispatching Add then StoreLocal as two separate opcodes, emit a single opcode AddStoreLocal that does both in one dispatch iteration. This eliminates one loop iteration, one bounds check, and one branch misprediction per combined sequence.

## Steps

### Step 1 - Analyze hot opcode sequences
Read src/zephyr_gc.inl VM dispatch loop to understand opcode structure.
Read src/zephyr_compiler.inl — find optimize_bytecode() post-pass (Constant Folding peephole from Wave A).
Identify the most common patterns in hot_arithmetic_loop and general VM use:
- LoadLocal + LoadLocal + Add (load two locals, add)
- Add + StoreLocal (add result, store to local)
- LoadLocal + LoadConst + Add
- LoadLocal + StoreLocal (copy local)
- JumpIfFalse preceded by a comparison (Less/Greater/etc + JumpIfFalse → single CmpJumpIfFalse)

### Step 2 - Add superinstruction opcodes to BytecodeOp enum
In src/zephyr_compiler.inl, add new BytecodeOp entries:
  AddStoreLocal       — Add top two, store result to operand slot
  LoadLocalLoadLocal  — Push locals[operand_a] then locals[operand_b] (needs 2 operands)
  LoadLocalAdd        — Load local[operand] then Add with top of stack
  CmpJumpIfFalse      — Compare (Less/Greater/etc encoded in aux) + conditional jump
  LoadLocalAddStoreLocal — full fib-loop body: load a, load b, add, store to local

Focus on the 3 most impactful first:
  SI_ADD_STORE_LOCAL   (Add + StoreLocal N)
  SI_LOAD_LOCAL_ADD    (LoadLocal N + Add)
  SI_CMP_JUMP_IF_FALSE (Less/Greater + JumpIfFalse target)

### Step 3 - Add superinstruction emission to optimize_bytecode()
In optimize_bytecode() in src/zephyr_compiler.inl, after existing constant-folding pass, add a second pass:
  - Scan instruction pairs (ip, ip+1)
  - If pattern matches a superinstruction, replace with single CompactInstruction
    - SI_ADD_STORE_LOCAL: operand = StoreLocal's slot
    - SI_LOAD_LOCAL_ADD: operand = LoadLocal's slot
    - SI_CMP_JUMP_IF_FALSE: operand = jump target, aux = comparison type
  - Remove the second instruction (compact the array + update jump targets via index_map like existing peephole)
  - Apply recursively (same loop structure as existing optimize_bytecode)

### Step 4 - Add superinstruction handlers to VM dispatch loop
In src/zephyr_gc.inl main dispatch loop, add cases for the new opcodes:
  case BytecodeOp::SI_ADD_STORE_LOCAL: {
      Value b = pop(); Value a = pop();
      Value result = (a.is_int() && b.is_int())
          ? Value::integer(a.as_int() + b.as_int())
          : apply_binary_op(a, b, BytecodeOp::Add);
      locals[instr.operand] = result;
      break;
  }
  case BytecodeOp::SI_LOAD_LOCAL_ADD: {
      Value b = pop(); Value a = locals[instr.operand];
      Value result = (a.is_int() && b.is_int())
          ? Value::integer(a.as_int() + b.as_int())
          : apply_binary_op(a, b, BytecodeOp::Add);
      push(result);
      break;
  }
  case BytecodeOp::SI_CMP_JUMP_IF_FALSE: {
      Value b = pop(); Value a = pop();
      bool cond;
      // aux_type encodes which comparison
      if (a.is_int() && b.is_int()) {
          // use instr.aux_type to pick <, >, <=, >=
          cond = compare_ints_by_aux(a.as_int(), b.as_int(), instr.aux_type);
      } else {
          cond = apply_comparison(a, b, instr.aux_type);
      }
      if (!cond) ip = instr.operand - 1; // -1 because loop increments
      break;
  }

If CompactInstruction doesn't have aux_type, use the upper bits of operand to encode it, or add a uint8_t aux field.

### Step 5 - Handle coroutine dispatch loop
If there is a second dispatch loop (for coroutines), apply the same superinstruction cases there too.

### Step 6 - Build and test
Find MSBuild under C:\Program Files\Microsoft Visual Studio\ and build Release x64.
Run x64\Release\zephyr_tests.exe — all must pass.

### Step 7 - Benchmark
Run: x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
Report hot_arithmetic_loop mean_ns. Target: below 4,000,000 ns (under 10 ns/op).

### Step 8 - Update docs/process.md
Mark superinstruction optimization complete with before/after ns/op numbers.

Success: hot_arithmetic mean_ns below 4,000,000 AND all tests pass.
