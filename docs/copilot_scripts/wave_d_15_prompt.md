# Wave D — Task 1.5: Shape-Based Inline Cache

## Project Context

Project Zephyr is a C++20 game scripting VM. Source is split across `.inl` files included by `src/zephyr.cpp`.

Completed so far:
- Wave A/B/C: all done
- Wave D 1.4: NaN-boxing done (Value is now uint64_t, sizeof(Value)==8)

Key files:
- `src/zephyr_types.inl` — Value, GcObject, StructInstanceObject definition
- `src/zephyr_compiler.inl` — BytecodeOp, BytecodeInstruction, BytecodeCompiler
- `src/zephyr_gc.inl` — GC + VM dispatch loop (LoadMember/StoreMember handlers)
- `tests/tests.cpp` — test suite
- `docs/plan.md` — full plan reference
- `docs/process.md` — progress log (update when done)

## Task 1.5: Shape-Based Inline Cache

### Goal
Replace `StructInstanceObject`'s `unordered_map<string, Value>` fields with a `Shape*` + `vector<Value> field_values` layout, and add per-instruction IC slots so `LoadMember`/`StoreMember` can do O(1) field access on cache hit.

### Implementation Plan

#### Step 1 — Add `Shape` struct (in `src/zephyr_types.inl`)

```cpp
struct Shape {
    std::unordered_map<std::string, uint32_t> field_indices;
    uint32_t field_count = 0;

    // Shape interning: get-or-create a shape from a set of field names (in order)
    static Shape* get_or_create(const std::vector<std::string>& field_names);

private:
    static std::unordered_map<std::string, Shape*> cache_; // keyed by canonical key
};
```

Shape interning key: join field names with `\0` separator in canonical order.

#### Step 2 — Redesign `StructInstanceObject`

Current (likely):
```cpp
struct StructInstanceObject : GcObject {
    std::unordered_map<std::string, Value> fields;
};
```

New:
```cpp
struct StructInstanceObject : GcObject {
    Shape* shape = nullptr;
    std::vector<Value> field_values;

    // Compatibility helpers
    Value get_field(const std::string& name) const;
    void set_field(const std::string& name, Value val);
    bool has_field(const std::string& name) const;
};
```

#### Step 3 — Add IC slots to `BytecodeInstruction`

In `BytecodeInstruction` or a parallel sidecar:
```cpp
mutable Shape* ic_shape = nullptr;
mutable uint32_t ic_slot = 0;
```

Mark as `mutable` so const instruction arrays can be updated at runtime.

#### Step 4 — Update `LoadMember` / `StoreMember` dispatch

```cpp
case BytecodeOp::LoadMember: {
    Value obj = pop();
    const std::string& name = instr.string_operand; // adjust to actual field name access
    if (obj.is_object()) {
        auto* inst = obj.as_object()->as<StructInstanceObject>();
        if (inst) {
            // IC fast path
            if (instr.ic_shape == inst->shape && instr.ic_shape != nullptr) {
                push(inst->field_values[instr.ic_slot]);
                break;
            }
            // IC miss — look up and cache
            auto it = inst->shape->field_indices.find(name);
            if (it != inst->shape->field_indices.end()) {
                instr.ic_shape = inst->shape;
                instr.ic_slot = it->second;
                push(inst->field_values[it->second]);
                break;
            }
        }
    }
    // fallback: slow path (method lookup, etc.)
    push(load_member_slow(obj, name));
    break;
}
```

Apply same pattern to `StoreMember`.

#### Step 5 — Update GC tracing

GC must trace `field_values` vector elements. Find where `StructInstanceObject` fields are traced and update to iterate `field_values`.

#### Step 6 — Update struct construction

Where `StructInstanceObject` is created and fields are assigned (struct literal compilation, `make_struct`-like calls), build the `Shape` via `Shape::get_or_create()` and populate `field_values` in index order.

### Build & Test

```
msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /m
x64\Release\zephyr_tests.exe
```

Fix any failures. Then update `docs/process.md` to mark 1.5 Shape IC as ✅ 완료.

### Success Criteria
- All existing tests pass
- `LoadMember`/`StoreMember` use IC fast path on repeated access
- `docs/process.md` updated

### Working Directory
`C:\Users\lance\OneDrive\Documents\Project Zephyr`
