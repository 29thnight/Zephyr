# VM Optimization — Superinstruction Fusion & Inline Cache

Zephyr's VM is a register-based bytecode interpreter that achieves Lua 5.5-level or better execution costs through two core optimization techniques: **Superinstruction Fusion** and **Inline Cache (IC)**.

---

## 1. Superinstruction Fusion

### Concept

A superinstruction replaces a recurring sequence of 2–3 bytecode operations with a single opcode — a compile-time optimization. It reduces the number of switch/case entries in the dispatch loop, eliminating the fixed overhead per instruction (PC increment, branch, cache miss, etc.).

Fusion is performed in a **peephole pass** inside `optimize_register_bytecode()`. The pass iterates `while(changed)`, scanning both adjacent and non-adjacent patterns.

### Implemented Superinstructions

#### Basic Fusion (Adjacent Patterns)

| Original Pattern | Fused Opcode | Meaning |
|---|---|---|
| `R_ADD/SUB/MUL` + `R_MOVE(dst, result)` | `R_SI_ADD/SUB/MUL_STORE` | Arithmetic + destination move in one step |
| `R_MODI(tmp, src, imm)` + `R_MOVE(dst, tmp)` | `R_MODI(dst, src, imm)` | Eliminate temporary register |
| `R_ADDI(dst, src, imm)` + `R_JUMP(target)` | `R_ADDI_JUMP` | Increment + branch in one step |
| `R_CMP*` + `R_JUMP_IF_FALSE` | `R_SI_CMP_JUMP_FALSE` | Compare + conditional branch |
| `R_LOAD_INT(tmp, imm)` + `R_SI_CMP_JUMP_FALSE` | `R_SI_CMPI_JUMP_FALSE` | Immediate compare + conditional branch |
| `R_MODI(tmp, src, imm)` + `R_SI_ADD_STORE(dst, accum, tmp)` | `R_SI_MODI_ADD_STORE` | `dst = accum + (src % imm)` |

#### Non-Adjacent Loop Patterns

Loop structures have back-edges (repeat jumps) and loop-tops (condition checks) that are not adjacent. Zephyr detects these non-adjacent patterns via `R_ADDI_JUMP`'s `ic_slot` field (jump target index).

| Pattern | Fused Opcode | Meaning |
|---|---|---|
| `R_ADDI_JUMP(reg, +N)` → target: `R_SI_CMPI_JUMP_FALSE(reg, limit)` | `R_SI_ADDI_CMPI_LT_JUMP` | `reg += N; if reg < limit goto body` |
| `R_SI_MODI_ADD_STORE(accum, accum, iter, div)` + `R_SI_ADDI_CMPI_LT_JUMP(iter, step, limit)` | `R_SI_LOOP_STEP` | Entire loop step as 1 opcode |

### R_SI_LOOP_STEP Detail

The most aggressively fused superinstruction. Executes three operations atomically:

```
accum += iter % div
iter  += step
if iter < limit then goto body_start
```

**Encoding (within the 24-byte CompactInstruction):**

| Field | Role |
|---|---|
| `dst` (uint8) | Accumulator register |
| `src1` (uint8, reinterpreted as int8) | Loop step |
| `src2` (uint8) | Loop counter register (iter) |
| `operand_a` (uint8) | Modulo divisor (div, 1–255) |
| `ic_slot[15:0]` (uint16) | Loop body start address |
| `ic_slot[31:16]` (int16) | Loop upper bound (limit) |

**Fusion Conditions:**
1. `R_SI_MODI_ADD_STORE`'s `dst == src1` (self-accumulation: `accum = accum + ...`)
2. `R_SI_ADDI_CMPI_LT_JUMP`'s `reg == src2` (same loop counter register)
3. `body_start ≤ 0xFFFF` (fits in uint16)
4. `step` ∈ [-128, 127], `limit` ∈ [-32768, 32767]

**Effect (hot_arithmetic benchmark):**

| Stage | ops/iteration | Mean Time |
|---|---|---|
| Base register VM | ~6 | 2,170 µs |
| +R_SI_MODI_ADD_STORE | 3 | 692 µs |
| +R_SI_ADDI_CMPI_LT_JUMP | 2 | 516 µs |
| **+R_SI_LOOP_STEP** | **1** | **~420 µs** |

---

## 2. Inline Cache (IC)

### Concept

An inline cache stores type/shape information computed on the first execution into the instruction's own fields (`ic_shape`, `ic_slot`). On subsequent calls with the same type, the expensive lookup is skipped entirely.

The mutable fields of `CompactInstruction` are used:
```cpp
mutable Shape*   ic_shape;   // cached Shape* or type pointer
mutable uint32_t ic_slot;    // cached field index or state flag
```

### R_BUILD_STRUCT IC

The `R_BUILD_STRUCT` opcode, which creates struct literals, performs the following work on each execution:

**Cold path (without IC, every call):**
1. `parse_type_name()` — parse type name string by `::`
2. `expect_struct_type()` — traverse environment chain + `unordered_map` lookup
3. Temporary `std::vector<Value> bs_fields(count)` heap allocation
4. `allocate<StructInstanceObject>()` — object allocation
5. `initialize_struct_instance()` — shape lookup + `field_values.assign(N, nil)`
6. Per-field: `field_slot(name)` (name → index string lookup) + `enforce_type` + `validate_handle_store`

**Warm path (with IC):**
1. Check `ic_shape != nullptr && ic_slot == 1` (single branch)
2. `reinterpret_cast<StructTypeObject*>(ic_shape)` to obtain type pointer
3. `allocate<StructInstanceObject>(type)` — object allocation
4. `bs_inst->shape = type->cached_shape` — set cached Shape* directly
5. `field_values.reserve(N)` + `push_back` × N — single-pass field init
6. `note_struct_field_write()` — write barrier (no-op for young objects)

Parsing, environment traversal, string lookups, and type validation are all eliminated.

**IC population (after first call):**
- Verify result object is a `StructInstance`
- `metadata.names[i] == type->fields[i].name` — declaration order matches field order
- If matched: `ic_shape = type`, `ic_slot = 1`

### StructTypeObject Shape Cache

`initialize_struct_instance()` previously called `shape_for_struct_type()` on every instantiation:

```cpp
// Before: every call
std::vector<std::string> field_names = collect_struct_field_names(type); // vector alloc
std::string key = make_key(field_names);                                 // string concat
Shape* shape = Shape::cache_.find(key)->second;                         // hashmap lookup
```

Adding `mutable Shape* cached_shape = nullptr` to `StructTypeObject` computes the shape only once on the first instantiation and returns it directly thereafter:

```cpp
// After: compute only once
if (type->cached_shape == nullptr)
    type->cached_shape = shape_for_struct_type(type);
instance->shape = type->cached_shape;
```

---

## 3. Final Benchmarks (vs Lua 5.5)

| Case | Before (v1) | Current | Lua 5.5 | Ratio |
|---|---|---|---|---|
| hot_arithmetic | 1,000 ms | ~420 µs | 394 µs | **1.07×** |
| array_object_churn | — | ~1,050 µs | 1,909 µs | **0.55×** ✓ |
| host_handle_entity | — | ~224 µs | 303 µs | **0.74×** ✓ |
| coroutine_yield_resume | — | ~220 µs | 923 µs | **0.24×** ✓ |

array_churn improved **56%** after R_BUILD_STRUCT IC + Shape cache (2,330 µs → 1,050 µs), **~2× faster than Lua 5.5**.

---

## 4. Debugging Superinstructions

Use `zephyr dump-bytecode <file>` to inspect fused opcodes:

```
0  R_LOAD_INT      dst=r0 value=0          ; sum = 0
1  R_LOAD_INT      dst=r1 value=0          ; i = 0
2  R_SI_LOOP_STEP  accum=r0 iter=r1 div=3 step=1 limit=70000 body=2
3  R_RETURN        src=r0
```

Use `zephyr stats <file>` to print superinstruction fusion counts and hit rate:

```
superinstruction_fusions: 3 (hit_rate: 75.00%)
```
