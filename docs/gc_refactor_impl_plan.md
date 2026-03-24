# Project Zephyr GC Refactor — Executable Implementation Plan

> Source of truth: `docs/gc_refactor_analysis.md`
> Target: fragmentation-resistant multi-space heap without full moving GC.
> Codebase: single-file VM at `src/zephyr.cpp` (~10,231 lines), public API at `include/zephyr/api.hpp`.

---

# 1. Implementation Phases (Refined)

The analysis doc's Section 9 defines 7 stages. Below they are decomposed into **13 atomic sub-phases**, each independently compilable and runtime-correct.

---

## Phase 1A: GcHeader Field Extension

**Goal**: Add `space_kind` and `size_class` fields to `GcHeader`. No behavioral change.

**Affected files**: `src/zephyr.cpp`
**Affected symbols**: `GcHeader` (lines 1698–1706), `GcSpaceKind` (new enum), `GcFlags` (line 1683)

**Changes**:
1. Add `GcSpaceKind` enum (6 values: `Nursery`, `OldSmall`, `LargeObject`, `Pinned`, `EnvArena`, `CoroArena`) before `GcHeader`.
2. Add `GcMovableBit = 1 << 6` to `GcFlags` enum (line 1683).
3. Add two fields to `GcHeader` after `age`:
   - `GcSpaceKind space_kind = GcSpaceKind::OldSmall;`
   - `uint8_t size_class = 0;`
4. In `allocate<T>()` (line 2284–2292): after setting `GcOldBit`, set `space_kind`:
   ```cpp
   object->header.space_kind = (object->header.flags & GcOldBit)
       ? GcSpaceKind::OldSmall : GcSpaceKind::Nursery;
   ```
   This is purely decorative at this stage — `space_kind` mirrors `GcOldBit`.

**Compile state**: Must compile. No new files. No changed signatures.
**Runtime state**: `gc_verify_full` must pass. `space_kind` is informational only.
**Risk level**: LOW. Additive change. sizeof(GcHeader) grows by 2 bytes. Padding layout may shift — verify with `static_assert(sizeof(GcHeader) == expected)`.

---

## Phase 1B: HeapSpace Abstraction + LegacyHeapSpace

**Goal**: Introduce `HeapSpace` base class and `LegacyHeapSpace` that wraps existing `objects_` list with zero behavioral change.

**Affected files**: `src/zephyr.cpp`
**Affected symbols**: `HeapSpace` (new), `LegacyHeapSpace` (new), `Runtime` class (new members)

**Changes**:
1. Declare `HeapSpace` pure virtual class (Section 8) above `Runtime` class definition (~line 2260).
2. Declare `LegacyHeapSpace : public HeapSpace` that:
   - Owns `GcObject* objects_` (moved from Runtime)
   - Owns `GcObject* sweep_cursor_`, `GcObject* sweep_previous_`
   - Implements `for_each_object` via `next_all` chain
   - `begin_cycle()`: existing color-reset loop from `begin_gc_cycle()` (line 8144–8148)
   - `sweep()`: existing `Runtime::sweep()` body (lines 8201–8228)
   - `live_bytes()`: returns `live_bytes_` (still global on Runtime for now)
   - `free_object(obj)`: `live_bytes_ -= size; delete obj;`
3. Add to Runtime:
   ```cpp
   LegacyHeapSpace legacy_heap_;
   std::vector<HeapSpace*> all_spaces_;  // initialized with {&legacy_heap_}
   ```
4. Move `objects_` from Runtime to `legacy_heap_.objects_`.
5. In `allocate<T>()`: replace `object->header.next_all = objects_; objects_ = object;` with `legacy_heap_.insert(object);`.
6. In `Runtime::~Runtime()`: replace loop with `legacy_heap_.destroy_all();`.

**Temporary compatibility**: `legacy_heap_` is a friend of `Runtime` or nested class, so it can access `live_bytes_` directly.

**Compile state**: Must compile. All `objects_` references now go through `legacy_heap_`.
**Runtime state**: `gc_verify_full` must pass. Semantically identical behavior.
**Risk level**: MEDIUM. This is the most cross-cutting change in Phase 1. Every reference to `objects_` must be redirected. Grep for `objects_` and redirect all 14 call sites listed in Section 3.

---

## Phase 1C: Route begin_gc_cycle / rebuild / count Through all_spaces_

**Goal**: Replace direct `objects_` loops in GC cycle management with `HeapSpace::for_each_object()`.

**Affected symbols**:
- `Runtime::begin_gc_cycle()` (line 8137–8153)
- `Runtime::rebuild_minor_remembered_set()` (line 4161–4194)
- `Runtime::count_remembered_cards()` (line 4097–4133)
- `Runtime::gc_verify_full()` (line 4306+)
- `Runtime::gc_verify_young()` (line 8849+)
- `Runtime::perform_young_collection()` color reset (line 8793–8796)

**Changes**:
For each function, replace `for (GcObject* object = objects_; object; object = object->header.next_all)` with:
```cpp
for (auto* space : all_spaces_) {
    space->for_each_object([&](GcObject* object) {
        // existing body
    });
}
```

**Compile state**: Must compile.
**Runtime state**: `gc_verify_full` must pass. Behavior identical because `all_spaces_` contains only `legacy_heap_`.
**Risk level**: MEDIUM. Loop conversion must be exact. Off-by-one in `rebuild_minor_remembered_set` (two-pass structure) requires careful mapping.

---

## Phase 2A: LargeObjectSpace Implementation

**Goal**: Implement `LargeObjectSpace` class without activating it.

**Affected files**: `src/zephyr.cpp`
**Affected symbols**: `LargeObjectSpace` (new), `LargeObjectNode` (new)

**Changes**:
1. Implement `LargeObjectNode` struct (Section 8: `object`, `alloc_size`, `next`, `prev`).
2. Implement `LargeObjectSpace : public HeapSpace`:
   - `try_alloc(size, kind)`: `new(std::align_val_t{4096}) char[aligned_size]`, placement new GcObject.
   - `free_object(obj)`: unlink node, `obj->~GcObject()`, deallocate page.
   - `for_each_object(cb)`: walk `los_list_`.
   - `begin_cycle()`: walk `los_list_`, set all colors to White.
   - `sweep(rt, budget)`: walk list, White → free or detach_queue, else → reset White.
   - `live_bytes()`, `used_bytes()`, `verify()`.
3. Do NOT register in `all_spaces_` yet. Purely dead code.

**Compile state**: Must compile. New code is unreachable.
**Runtime state**: No change.
**Risk level**: LOW. Dead code.

---

## Phase 2B: Activate LargeObjectSpace

**Goal**: Route `size >= large_object_threshold_bytes_` allocations to LOS.

**Affected symbols**:
- `Runtime::allocate<T>()` (line 2281–2309)
- `Runtime::should_allocate_old()` (line 3644–3646) — split into `should_allocate_los()` + `should_allocate_old()`
- `Runtime::is_large_object()` (line 3652–3654)
- `all_spaces_` initialization

**Changes**:
1. Add `LargeObjectSpace los_;` to Runtime. Add `&los_` to `all_spaces_`.
2. In `allocate<T>()`, insert before existing `should_allocate_old` check:
   ```cpp
   if (object->header.size_bytes >= large_object_threshold_bytes_) {
       los_.insert(object);
       object->header.space_kind = GcSpaceKind::LargeObject;
       object->header.flags |= GcOldBit;  // LOS objects are old-gen
       // DO NOT insert into legacy_heap_
   } else if (should_allocate_old(object_kind, object->header.size_bytes)) {
       legacy_heap_.insert(object);
       object->header.space_kind = GcSpaceKind::OldSmall;
       object->header.flags |= GcOldBit;
   } else {
       legacy_heap_.insert(object);
       object->header.space_kind = GcSpaceKind::Nursery;
   }
   ```
3. `gc_step()` SweepObjects phase: add `los_.sweep(...)` call alongside `legacy_heap_.sweep(...)`.
4. `process_detach_queue()`: check `space_kind`:
   ```cpp
   if (object->header.space_kind == GcSpaceKind::LargeObject)
       los_.free_object(object);
   else
       delete object;
   ```
5. `Runtime::~Runtime()`: add `los_.destroy_all()`.

**Key accounting change**: `live_bytes_` remains global. Both `legacy_heap_` and `los_` decrement it. Add shadow assertion:
```cpp
assert(live_bytes_ == legacy_heap_.live_bytes() + los_.live_bytes());
```

**Compile state**: Must compile.
**Runtime state**: `gc_verify_full` must pass. Add LOS to verify scope.
**Risk level**: HIGH. First real split. Objects now exist in two different structures. Critical to verify:
- `begin_gc_cycle()` resets LOS colors (via `all_spaces_` from Phase 1C)
- `rebuild_minor_remembered_set()` visits LOS objects (via `all_spaces_` from Phase 1C)
- LOS objects participate in remembered set correctly
- `note_write` barrier works for LOS objects (`is_old_object()` still checks `GcOldBit`, which LOS objects have)

---

## Phase 3A: PinnedSpace Implementation

**Goal**: Implement `PinnedSpace` class without activating it.

**Affected files**: `src/zephyr.cpp`
**Affected symbols**: `PinnedSpace` (new)

**Changes**:
1. Implement `PinnedSpace : public HeapSpace`:
   - Page list allocator (bump per page).
   - `sweep()`: color reset only, no object deletion.
   - `free_object()`: explicit unpin — destructor + mark slot free.
   - `for_each_object()`, `begin_cycle()`, `verify()`.
2. Dead code — not registered.

**Compile state**: Must compile.
**Runtime state**: No change.
**Risk level**: LOW.

---

## Phase 3B: Activate PinnedSpace for Permanent Roots

**Goal**: Route `root_environment_`, module environments, `interned_strings_` values, `native_callback_registry_` entries to PinnedSpace.

**Affected symbols**:
- `Runtime::allocate<T>()` — add pinned routing
- `Runtime` constructor (line 3544) — `root_environment_` allocation
- `Runtime::remember_minor_owner()` (line 3972–3987) — remove Root/Module exclusion for PinnedSpace objects
- `GcPinnedBit` (line 1687) — activate

**Changes**:
1. Add `PinnedSpace pinned_;` to Runtime. Add `&pinned_` to `all_spaces_`.
2. Add `allocate_pinned<T>(args...)` method:
   ```cpp
   template<typename T, typename... Args>
   T* allocate_pinned(Args&&... args) {
       auto* object = allocate<T>(std::forward<Args>(args)...);
       // relocate from legacy_heap_ to pinned_
       legacy_heap_.remove(object);
       pinned_.insert(object);
       object->header.space_kind = GcSpaceKind::Pinned;
       object->header.flags |= GcPinnedBit | GcOldBit;
       return object;
   }
   ```
   Alternative: add a `pinned` parameter to `allocate<T>` and route directly.
3. Change `root_environment_` allocation (line 3544): use `allocate_pinned<Environment>(...)`.
4. Change module environment/namespace allocation: use `allocate_pinned`.
5. In `remember_minor_owner()` (line 3978–3980): remove the `Root/Module` early return. Replace with:
   ```cpp
   // Root and Module environments are always roots — no need to track.
   // BUT PinnedSpace objects that reference nursery DO need tracking.
   if (object->kind == ObjectKind::Environment) {
       const auto* env = static_cast<const Environment*>(object);
       if (env->kind == EnvironmentKind::Root || env->kind == EnvironmentKind::Module) {
           // These are always scanned as roots. Skip remembered set.
           return;
       }
   }
   ```
   This is actually the same as current behavior. The critical thing is: do NOT add a blanket `if (space_kind == Pinned) return;` check.
6. `PinnedSpace::sweep()` in `gc_step()`: call alongside others, but it only resets colors.
7. `process_detach_queue()`: add `GcSpaceKind::Pinned` branch → `pinned_.free_object(object)`.

**Compile state**: Must compile.
**Runtime state**: `gc_verify_full` must pass. Pinned objects must appear in verify scope.
**Risk level**: MEDIUM. Key danger: `interned_strings_` map holds `StringObject*`. These must be allocated pinned. If any interned string is allocated normal and later promoted, the pinned invariant breaks. Ensure interned string creation path uses `allocate_pinned`.

---

## Phase 4A: OldSmallSpace Implementation

**Goal**: Implement `OldSmallSpace` with `Span`, `SizeClassList` without activating it.

**Affected files**: `src/zephyr.cpp`
**Affected symbols**: `Span` (new), `SizeClassList` (new), `OldSmallSpace` (new), `kSizeClasses` (new)

**Changes**:
1. Determine size classes by measuring `sizeof()` of all GcObject subclasses:
   ```cpp
   // Measure: sizeof(StringObject), sizeof(ArrayObject), sizeof(StructInstanceObject),
   // sizeof(EnumInstanceObject), sizeof(ScriptFunctionObject), sizeof(NativeFunctionObject),
   // sizeof(UpvalueCellObject), sizeof(StructTypeObject), sizeof(EnumTypeObject),
   // sizeof(ModuleNamespaceObject)
   // Round up to next size class boundary.
   ```
2. Implement `Span`:
   - Page-sized (4096 bytes). `page_base`, `slot_size`, `total_slots`, `live_slots`.
   - `live_bitmap[]` and `free_bitmap[]` — `uint64_t` arrays. Max slots per span = 4096/32 = 128, so 2 × `uint64_t` suffices.
   - `alloc_slot()`: scan `free_bitmap` for first set bit, clear it, return pointer.
   - `free_slot(GcObject*)`: compute slot index from pointer, set `free_bitmap` bit, clear `live_bitmap`.
3. Implement `SizeClassList`: `partial_spans`, `full_spans`, `empty_spans` — doubly-linked via `Span::next_span/prev_span`.
4. Implement `OldSmallSpace`:
   - `try_alloc(size, kind)`: find size class, alloc from partial or empty span.
   - `free_object(obj)`: compute span from pointer (`page_base = obj & ~0xFFF`), call `span->free_slot(obj)`.
   - `sweep(rt, budget)`: iterate all non-empty spans, sweep each slot.
   - `for_each_object(cb)`: iterate all live slots across all spans.
   - `begin_cycle()`: iterate all live slots, reset color.
   - `verify()`: validate bitmap consistency.
5. Dead code — not registered.

**Compile state**: Must compile.
**Runtime state**: No change.
**Risk level**: LOW. Dead code. But must pass compilation with all bitmap operations correct.

---

## Phase 4B: Activate OldSmallSpace

**Goal**: Route non-large old object allocations to OldSmallSpace.

**Affected symbols**:
- `Runtime::allocate<T>()` — old routing
- `Runtime::promote_object()` (line 3656–3677)
- `Runtime::process_detach_queue()` (line 8236–8247)
- `Runtime::~Runtime()`

**Changes**:
1. Add `OldSmallSpace old_small_;` to Runtime. Add `&old_small_` to `all_spaces_`.
2. In `allocate<T>()`, change the old routing:
   ```cpp
   // was: legacy_heap_.insert(object); for old objects
   // now:
   if (size >= large_object_threshold) {
       los_.insert(object);
       space_kind = LargeObject;
   } else if (is_pinned) {
       pinned_.insert(object);
       space_kind = Pinned;
   } else if (should_allocate_old(kind, size)) {
       old_small_.insert(object);  // NEW
       space_kind = OldSmall;
   } else {
       legacy_heap_.insert(object);  // young still in legacy
       space_kind = Nursery;
   }
   ```
3. **Critical**: `old_small_.insert(object)` must handle `new T()` objects. The object was already constructed by `new T()`. OldSmallSpace must NOT allocate memory for the object — it must track the `new T()`-allocated pointer via its span/slot system.

   **Problem**: OldSmallSpace's slab allocation conflicts with `new T()`. Two options:

   **Option A (phased)**: OldSmallSpace initially just maintains an intrusive linked list (like `LegacyHeapSpace`), and slab allocation is introduced in a later sub-phase. This keeps `new T()` + `delete` intact.

   **Option B (direct)**: Replace `new T()` with placement new into OldSmallSpace slab memory. This requires `allocate<T>()` to call `old_small_.try_alloc(sizeof(T))` to get raw memory, then placement-new the object.

   **Recommendation**: Option A first (Phase 4B), then Option B (Phase 4C).

4. Phase 4B: OldSmallSpace is a linked-list space (like LegacyHeapSpace but only for old non-large objects). Objects still use `new T()` / `delete`. The separation is purely organizational.
5. `promote_object()`: promoted objects must move from `legacy_heap_` to `old_small_`:
   ```cpp
   legacy_heap_.remove(object);
   old_small_.insert(object);
   object->header.space_kind = GcSpaceKind::OldSmall;
   ```
6. `process_detach_queue()`: add `OldSmall` branch → `old_small_.free_object(object)` (which calls `delete`).
7. `Runtime::~Runtime()`: add `old_small_.destroy_all()`.

**Compile state**: Must compile.
**Runtime state**: `gc_verify_full` must pass. Add assertion: every old non-large non-pinned object has `space_kind == OldSmall`.
**Risk level**: HIGH. Promotion path changes. `legacy_heap_.remove(object)` during young sweep must correctly unlink from legacy list.

---

## Phase 4C: OldSmallSpace Slab Allocation (Optional, Deferred)

**Goal**: Replace `new T()` / `delete` for old objects with placement new into Span slabs.

**Changes**:
1. `allocate<T>()` for old path:
   ```cpp
   void* mem = old_small_.try_alloc(sizeof(T), alignof(T));
   auto* object = new(mem) T(std::forward<Args>(args)...);
   ```
2. `old_small_.free_object(obj)`:
   ```cpp
   obj->~GcObject();  // destructor only (frees internal vectors/maps)
   span->free_slot(obj);  // mark slot free in bitmap
   // do NOT call ::operator delete
   ```
3. `Runtime::~Runtime()`: `old_small_.destroy_all()` calls destructors on all live slots, then frees all span pages.

**Compile state**: Must compile.
**Runtime state**: `gc_verify_full` must pass. fragmentation metrics now meaningful for OldSmallSpace.
**Risk level**: HIGH. Slab allocation changes memory ownership model. Every `delete` for old objects must be replaced. ASAN must not report leaks on span pages.

---

## Phase 5A: NurserySpace Implementation

**Goal**: Implement bump-pointer `NurserySpace` without activating it.

**Affected files**: `src/zephyr.cpp`
**Affected symbols**: `NurseryChunk` (new), `NurserySpace` (new)

**Changes**:
1. Implement `NurseryChunk` (Section 8): 256KB chunks, `base`, `bump_ptr`, `limit`, `next_chunk`.
2. Implement `NurserySpace : public HeapSpace`:
   - `try_alloc(size, kind)`: bump allocate from `current_chunk_`. If full, allocate new chunk.
   - `for_each_object(cb)`: walk each chunk linearly using `size_bytes` to step between objects.
   - `begin_cycle()`: walk all objects, reset color to White.
   - `sweep(rt, budget)`: for each young object: if White → finalize. If surviving → promote or age++. Then reset chunk.
   - `free_object(obj)`: no-op for individual objects (chunk-level reclaim).
   - `verify()`: validate all objects within chunk bounds.
3. Dead code — not registered.

**Compile state**: Must compile.
**Runtime state**: No change.
**Risk level**: LOW.

---

## Phase 5B: Activate NurserySpace

**Goal**: Route young allocations to bump-pointer NurserySpace.

**Affected symbols**:
- `Runtime::allocate<T>()` — young routing
- `Runtime::perform_young_collection()` (line 8788–8840)
- `Runtime::sweep_young_objects()` (line 8743–8786)

**Changes**:
1. Add `NurserySpace nursery_;` to Runtime. Add `&nursery_` to `all_spaces_`.
2. In `allocate<T>()`, change young path:
   ```cpp
   } else {
       // was: legacy_heap_.insert(object);
       // now: nursery allocation with placement new
       void* mem = nursery_.try_alloc(sizeof(T));
       auto* object = new(mem) T(std::forward<Args>(args)...);
       object->header.space_kind = GcSpaceKind::Nursery;
       // DO NOT set GcOldBit
       // DO NOT insert into legacy_heap_
   }
   ```
   **Critical**: `new T()` must be replaced with placement new for nursery objects. Otherwise we can't do chunk-level reclaim.
3. `perform_young_collection()`:
   - Replace `for (GcObject* object = objects_; ...)` color reset (line 8793) with `nursery_.begin_cycle()`.
   - Old objects are in `old_small_`, `los_`, `pinned_` — their colors are NOT reset during young collection (correct behavior).
   - But currently (line 8793) ALL objects get color reset. This is wasteful but safe. With nursery split, only nursery objects need reset. But `remembered_objects_` may contain old objects that were marked during young tracing. Those old objects' colors are left as-is (they were never set to non-White by young collection since `mark_young_object` skips old objects).
   - Actually, current code (line 8793–8796) resets ALL objects including old. After split, we only reset nursery. This is correct because `mark_young_object()` (line 3578) skips old objects, so old objects never become Gray during young collection.

   **Wait**: line 8793–8796 resets ALL objects. But `mark_young_root_value` (line 3586–3598) calls `trace_young_references(old_obj)` which does NOT mark the old object — it traverses its children and marks young ones. So old objects stay White throughout young collection. The reset is safe to skip for old objects. ✓

4. `sweep_young_objects()` → delegated to `NurserySpace::sweep()`:
   ```cpp
   // NurserySpace::sweep():
   for each chunk:
       for each object in chunk (linear walk via size_bytes):
           if color == White:
               if GcFinalizableBit: runtime.detach_queue_.push_back(obj)
               obj->~GcObject()  // destructor only, no free
           else:  // survived
               if age+1 >= threshold || is_large:
                   // copy to OldSmallSpace via placement new + memcpy
                   void* new_mem = old_small_.try_alloc(obj->header.size_bytes);
                   memcpy(new_mem, obj, obj->header.size_bytes);
                   // BUT: this breaks internal pointers (vtable, std::vector internals)
   ```

   **CRITICAL PROBLEM**: Nursery bump allocation means objects live in chunk memory. Promotion requires physically copying the object to OldSmallSpace. But C++ objects with virtual tables, `std::vector`, `std::string`, `std::unordered_map` etc. cannot be trivially `memcpy`'d.

   **Resolution**: For Phase 5B, do NOT use bump allocation. Instead, NurserySpace uses `new T()` (like LegacyHeapSpace) but maintains its own linked list via `next_all`. This gives organizational separation without changing allocation mechanics. Bump allocation (Phase 5C) requires solving the trivial-copy problem first, which may require moving to a model where GcObject subclasses store only handles/indices to separate storage.

5. Phase 5B NurserySpace: linked-list based, `new T()` / `delete`, separate from legacy_heap_.
   - `young_allocation_pressure_bytes_` replaced by `nursery_.live_bytes()`.
   - `sweep_young_objects()` → `nursery_.sweep()` which walks nursery's own linked list.
   - Promotion: `nursery_.remove(object); old_small_.insert(object);`.

6. At this point `legacy_heap_` should be empty. All objects route to one of: `nursery_`, `old_small_`, `los_`, `pinned_`.

**Compile state**: Must compile.
**Runtime state**: `gc_verify_full` must pass. `legacy_heap_` should have 0 objects.
**Risk level**: HIGH. This completes the four-way split. Every allocation path must route correctly. Every sweep, every barrier, every verify must cover all four spaces.

---

## Phase 5C: NurserySpace Bump Allocation (Future, Optional)

**Goal**: Replace `new T()` in nursery with bump allocation for allocation speed.

**Prerequisite**: All GcObject subclasses in nursery must be trivially relocatable, OR promotion must use move-construction, OR nursery objects must not be promoted by copy but by re-allocation in old space + trace-based pointer update.

**This phase is deferred.** It requires architectural changes beyond the current refactor scope (Value handle indirection or GcObject move semantics). Document as future work.

---

## Phase 6: Remove next_all and objects_

**Goal**: Remove `GcObject::next_all` field and `LegacyHeapSpace` entirely.

**Prerequisite**: Phase 5B complete. `legacy_heap_` contains 0 objects.

**Changes**:
1. Remove `LegacyHeapSpace` class.
2. Remove `legacy_heap_` from Runtime.
3. Remove `GcHeader::next_all` field.
4. Each space maintains its own iteration structure:
   - `NurserySpace`: own intrusive linked list (add `GcObject* next_in_space` to GcHeader, or use per-space `std::vector<GcObject*>`)
   - `OldSmallSpace`: bitmap iteration across spans (if Phase 4C done) or own linked list
   - `LargeObjectSpace`: `LargeObjectNode` doubly-linked list
   - `PinnedSpace`: page-list based iteration
5. `Runtime::~Runtime()`: `for (auto* space : all_spaces_) space->destroy_all();`
6. `GcHeader` shrinks by 8 bytes.

**Compile state**: Must compile. `next_all` references are compile errors — all must be resolved.
**Runtime state**: `gc_verify_full` must pass. Full gc_stress cycle.
**Risk level**: HIGH. Large cross-cutting change. Must grep for every `next_all` reference.

---

## Phase 7: Selective Compaction Hooks (Future)

**Deferred.** Document only. Requires Value handle indirection.

---

# 2. File-level Change Plan

## File: `src/zephyr.cpp`

### Region: Lines 1665–1706 (GcColor, GcFlags, GcHeader)
- **Phase 1A**: Add `GcSpaceKind` enum before `GcHeader`. Add `GcMovableBit` to `GcFlags`. Add `space_kind`, `size_class` fields to `GcHeader`.
- **Phase 6**: Remove `next_all` field from `GcHeader`.

### Region: Lines 1708–1724 (GcObject)
- **Phase 4C**: Override `operator new`/`operator delete` if using slab allocation. OR, leave untouched and use placement new externally.
- No change in other phases.

### Region: Lines 2260–2310 (Runtime class: allocate<T>, gc_step declarations)
- **Phase 1B**: Move `objects_` to `LegacyHeapSpace`. Add `legacy_heap_`, `all_spaces_` members.
- **Phase 2B**: Add `los_` member.
- **Phase 3B**: Add `pinned_` member. Add `allocate_pinned<T>()`.
- **Phase 4B**: Add `old_small_` member.
- **Phase 5B**: Add `nursery_` member.
- **Phase 2B–5B**: Progressively expand routing logic in `allocate<T>()`.

### Region: Lines 2540–2640 (Runtime member variables)
- **Phase 1B**: Remove `GcObject* objects_` (moved to `LegacyHeapSpace`).
- **Phase 1B**: Remove `sweep_cursor_`, `sweep_previous_` (moved to `LegacyHeapSpace`).
- **All phases**: Add space members and `all_spaces_` vector.

### Region: Lines 3540–3554 (Runtime constructor, destructor)
- **Phase 1B**: Constructor initializes `legacy_heap_`, `all_spaces_`.
- **Phase 3B**: Constructor uses `allocate_pinned` for `root_environment_`.
- **All phases**: Destructor delegates to `all_spaces_`.

### Region: Lines 3556–3600 (mark_object, mark_value, mark_young_*, visit_root_references)
- **No change in any phase.** These are space-agnostic.

### Region: Lines 3644–3677 (should_allocate_old, is_old_object, is_large_object, promote_object)
- **Phase 2B**: `should_allocate_old` refined to exclude large objects (handled by LOS).
- **Phase 4B**: `promote_object()` modified to move object from `nursery_` to `old_small_`.
- **Phase 6**: `is_old_object()` optionally changed to check `space_kind != Nursery`.

### Region: Lines 3972–3987 (remember_minor_owner)
- **Phase 3B**: Verify Root/Module exclusion doesn't accidentally exclude PinnedSpace objects that should be tracked.

### Region: Lines 4048–4095 (note_*_write barrier functions)
- **No change in any phase.** Card marking is per-object (embedded `remembered_cards` vector), not per-space. Barriers call `note_write()` which checks `is_old_object()`.

### Region: Lines 4097–4133 (count_remembered_cards)
- **Phase 1C**: Change to `all_spaces_` iteration.

### Region: Lines 4135–4194 (compact/rebuild_minor_remembered_set)
- **Phase 1C**: `rebuild_minor_remembered_set()` changed to `all_spaces_` iteration.

### Region: Lines 4306+ (gc_verify_full)
- **Phase 1C**: Changed to `all_spaces_` iteration.
- **Each subsequent phase**: Verify new space is included.

### Region: Lines 8137–8153 (begin_gc_cycle)
- **Phase 1C**: Color reset changed to `all_spaces_` iteration.

### Region: Lines 8160–8199 (process_dirty_roots, drain_gray)
- **Phase 2B onwards**: `process_dirty_roots()` line 8179: `sweep_cursor_ = objects_` → delegate to per-space sweep initialization. Each space sets its own cursor.
- **drain_gray**: No change (space-agnostic).

### Region: Lines 8201–8234 (sweep)
- **Phase 1B**: Body moved to `LegacyHeapSpace::sweep()`.
- **Phase 2B onwards**: `gc_step()` SweepObjects phase calls each space's sweep.
- **Phase 6**: `LegacyHeapSpace::sweep()` removed.

### Region: Lines 8236–8247 (process_detach_queue)
- **Phase 2B**: Add space_kind dispatch for free.

### Region: Lines 8249–8290 (stress safe point, note_write overloads)
- **No change** except optional Phase 6 barrier update (`space_kind` instead of `GcOldBit`).

### Region: Lines 8743–8840 (sweep_young_objects, perform_young_collection)
- **Phase 5B**: `sweep_young_objects()` → `nursery_.sweep()`. `perform_young_collection()` color reset → `nursery_.begin_cycle()`.

### Region: Lines 8849–8933 (gc_verify_young)
- **Phase 5B**: Changed to verify nursery space specifically.

### Region: Lines 8935–8985 (gc_step)
- **Phase 2B onwards**: SweepObjects case dispatches to all spaces.

### Region: Lines 8992–9060 (advance_frame/tick/scene, stats)
- **All phases**: Stats population updated to aggregate per-space stats.

## File: `include/zephyr/api.hpp`

### Region: Lines 46–55 (ZephyrGcPhase)
- **No change.**

### Region: Lines 66–73 (ZephyrGcConfig)
- **No change.** Existing config values remain valid.

### Region: Lines 273–303 (ZephyrGcStats)
- **Phase 2B onwards**: Add `ZephyrSpaceStats` sub-structs for each space (Section 11).

---

# 3. Function-level Refactor Plan

## `Runtime::allocate<T>()`

**Current** (line 2281–2309): `new T()`, metadata init, insert at `objects_` head, pressure check.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1B | Replace `objects_ = object` with `legacy_heap_.insert(object)` |
| 2 | 1A | Set `object->header.space_kind` based on `GcOldBit` |
| 3 | 2B | Add LOS routing: `if (size >= threshold) los_.insert(object)` before legacy |
| 4 | 3B | Add pinned routing for `allocate_pinned<T>()` variant |
| 5 | 4B | Add OldSmall routing: old non-large → `old_small_.insert(object)` |
| 6 | 5B | Add Nursery routing: young → `nursery_.insert(object)`. Legacy empty. |
| 7 | 6 | Remove `legacy_heap_` branch entirely |

**Temporary compatibility**: Each phase adds a new branch. Legacy branch handles everything not yet split out. When legacy is empty (Phase 5B), verify with `assert(legacy_heap_.object_count() == 0)`.

---

## `Runtime::sweep()`

**Current** (line 8201–8234): Walk `objects_` via `sweep_cursor_`/`sweep_previous_`, White → delete or detach.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1B | Move body to `LegacyHeapSpace::sweep(rt, budget)` |
| 2 | 2B | `gc_step()` SweepObjects calls `legacy_heap_.sweep()` then `los_.sweep()` |
| 3 | 4B | Add `old_small_.sweep()` call |
| 4 | 5B | Add `nursery_.sweep()` call (or nursery sweep is in young collection only) |
| 5 | 6 | Remove `LegacyHeapSpace::sweep()` |

**Budget distribution**: Each space gets remaining budget. Sweep order: nursery (if applicable) → old_small → los → pinned. Budget is passed by reference, each space consumes what it needs.

---

## `Runtime::sweep_young_objects()`

**Current** (line 8743–8786): Walk ALL `objects_`, filter young, sweep/promote/age.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1C | No change (still walks `legacy_heap_.for_each_object()`) |
| 2 | 5B | Replace with `nursery_.sweep_young(rt)` which only walks nursery objects. Promotion calls `nursery_.remove(obj); old_small_.insert(obj);` |
| 3 | 6 | Remove original function entirely |

---

## `Runtime::begin_gc_cycle()`

**Current** (line 8137–8153): Reset all colors via `objects_` walk, clear queues, set phase.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1C | Color reset → `for (auto* s : all_spaces_) s->begin_cycle();` |
| 2 | All | Queue clears (gray_stack, dirty_roots, etc.) remain in `begin_gc_cycle()` |

---

## `Runtime::promote_object()`

**Current** (line 3656–3677): Set `GcOldBit`, rebuild cards, `remember_minor_owner`.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 4B | After setting `GcOldBit`, also: `nursery_.remove(object); old_small_.insert(object); object->header.space_kind = OldSmall;` |
| 2 | 4B | Card rebuild functions unchanged (they operate on the object itself) |
| 3 | 4B | `remember_minor_owner` unchanged |

---

## `Runtime::note_write()` (both overloads)

**Current** (line 8258–8290): Check `is_old_object()`, barrier for inter-gen and incremental.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1A–5B | No change. `is_old_object()` checks `GcOldBit` which is set on all non-nursery objects. Correct across all spaces. |
| 2 | 6 (optional) | Replace `is_old_object(owner)` with `owner->header.space_kind != GcSpaceKind::Nursery`. Semantically equivalent. |

---

## `Runtime::rebuild_minor_remembered_set()`

**Current** (line 4161–4194): Two passes over `objects_`. Pass 1: clear bits. Pass 2: rebuild cards + add to remembered set.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1C | Both passes → `for (auto* s : all_spaces_) s->for_each_object(...)` |
| 2 | All | Logic inside lambda unchanged |

---

## `Runtime::process_detach_queue()`

**Current** (line 8236–8247): Pop from `detach_queue_`, `delete object`, decrement `live_bytes_`.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 2B | Add space dispatch: `switch (object->header.space_kind) { case LargeObject: los_.free_object(object); break; default: delete object; }` |
| 2 | 3B | Add `Pinned` case |
| 3 | 4B | Add `OldSmall` case |
| 4 | 5B | Add `Nursery` case (unusual — young finalizable objects) |
| 5 | All | Each `free_object()` internally decrements its own `live_bytes_` |

---

## `gc_verify_full()` / `gc_verify_young()`

**Current**: Walk `objects_` to verify reachability, accounting, remembered set.

| Step | Phase | Transformation |
|------|-------|---------------|
| 1 | 1C | `objects_` walk → `all_spaces_` iteration |
| 2 | 2B | Add LOS objects to verification scope |
| 3 | 3B | Add Pinned objects |
| 4 | 4B | Add OldSmall objects |
| 5 | 5B | Add Nursery objects. Add `assert(legacy_heap_.object_count() == 0)` |
| 6 | Each | Add: `assert(sum_space_live_bytes == live_bytes_)` |
| 7 | Each | Add: space_kind ↔ GcOldBit consistency check for every object |
| 8 | 3B | Verify PinnedSpace→Nursery references are in remembered_objects_ |

---

# 4. Data Structure Introduction Plan

## HeapSpace (abstract base)

- **Declared in**: `src/zephyr.cpp`, above Runtime class (~line 2260)
- **Owned by**: Not owned. Abstract base pointer stored in `all_spaces_`.
- **Active in**: Phase 1B
- **Replaces**: The implicit assumption that `objects_` is the single heap

## LegacyHeapSpace

- **Declared in**: `src/zephyr.cpp`, after `HeapSpace`
- **Owned by**: `Runtime legacy_heap_;` (value member)
- **Active in**: Phase 1B–5B
- **Replaces**: Direct `objects_` manipulation in Runtime
- **Removed in**: Phase 6

## LargeObjectSpace

- **Declared in**: `src/zephyr.cpp`, after `HeapSpace`
- **Owned by**: `Runtime los_;` (value member or `unique_ptr`)
- **Active in**: Phase 2B
- **Replaces**: Large objects in `objects_` list
- **Dependencies**: `LargeObjectNode` struct

## PinnedSpace

- **Declared in**: `src/zephyr.cpp`, after `HeapSpace`
- **Owned by**: `Runtime pinned_;` (value member)
- **Active in**: Phase 3B
- **Replaces**: Implicit permanent-root pattern via `pinned_debug_objects_` / root env

## OldSmallSpace

- **Declared in**: `src/zephyr.cpp`, after `HeapSpace`
- **Owned by**: `Runtime old_small_;` (value member)
- **Active in**: Phase 4B (linked-list mode), Phase 4C (slab mode)
- **Replaces**: Old non-large objects in `objects_` list
- **Dependencies**: `Span`, `SizeClassList`, `kSizeClasses`

## NurserySpace

- **Declared in**: `src/zephyr.cpp`, after `HeapSpace`
- **Owned by**: `Runtime nursery_;` (value member)
- **Active in**: Phase 5B (linked-list mode), Phase 5C (bump allocation, deferred)
- **Replaces**: Young objects in `objects_` list
- **Dependencies**: `NurseryChunk` (Phase 5C only)

---

# 5. Transitional Compatibility Strategy

## Coexistence Model

```
Phase 1B:  [LegacyHeapSpace: ALL objects]
Phase 2B:  [LegacyHeapSpace: all except large] + [LOS: large]
Phase 3B:  [Legacy: all except large/pinned] + [LOS] + [Pinned]
Phase 4B:  [Legacy: young only] + [OldSmall: old non-large] + [LOS] + [Pinned]
Phase 5B:  [Nursery: young] + [OldSmall] + [LOS] + [Pinned]  (Legacy EMPTY)
Phase 6:   [Nursery] + [OldSmall] + [LOS] + [Pinned]  (Legacy REMOVED)
```

## When `objects_` is still valid

- Phase 1B–5B: `objects_` lives inside `LegacyHeapSpace`. It is valid but shrinking.
- Phase 5B: `legacy_heap_.objects_ == nullptr`. Assert this.
- Phase 6: `objects_` no longer exists.

## When `space_kind` becomes authoritative

- Phase 1A–4A: `space_kind` is informational. `GcOldBit` is the source of truth for generation.
- Phase 4B+: `space_kind` and `GcOldBit` must agree. Assert: `(flags & GcOldBit) == (space_kind != Nursery)`.
- Phase 6: `space_kind` is authoritative. `GcOldBit` may be kept for backward compatibility or removed.

## Mixed-mode safety

During Phases 2B–4B, objects exist in different spaces but GC algorithms still work because:
1. `begin_gc_cycle()` uses `all_spaces_` → covers all objects. ✓
2. `rebuild_minor_remembered_set()` uses `all_spaces_` → covers all objects. ✓
3. `mark_object()`, `drain_gray()`, `mark_value()` are space-agnostic (they just set color/push gray). ✓
4. `note_write()` checks `is_old_object()` via `GcOldBit`, which is set correctly for all spaces. ✓
5. `visit_root_references()` is space-agnostic. ✓
6. Sweep calls each space's sweep. ✓

## Compile flags

None needed. The approach is additive — new spaces are added alongside legacy, legacy shrinks to empty, then is removed. All code compiles at every phase.

## Safe rollback points

Each phase can be reverted by:
- Removing the new space.
- Routing its objects back to `legacy_heap_`.
- Removing it from `all_spaces_`.
- The code returns to previous phase behavior.

---

# 6. Memory Safety Guarantees (Per-Phase)

## Phase 1A (GcHeader extension)
- **MUST hold**: `sizeof(GcHeader)` is correct. `static_assert` on expected size.
- **MUST hold**: No code reads `space_kind` for decision-making yet (informational only).
- **MUST NOT**: Corrupt existing field offsets. Verify `offsetof(GcHeader, next_all)` unchanged from pre-refactor value.

## Phase 1B (LegacyHeapSpace)
- **MUST hold**: Every object is in exactly one space (`legacy_heap_`).
- **MUST hold**: `legacy_heap_.for_each_object()` visits same set as old `objects_` loop.
- **MUST NOT**: Leave any `objects_` direct reference in Runtime (grep for `objects_`).
- **Assertion**: `legacy_heap_.object_count() == total_allocations_ - total_freed_`.

## Phase 1C (all_spaces_ routing)
- **MUST hold**: `all_spaces_` contains exactly `{&legacy_heap_}`.
- **MUST hold**: `begin_gc_cycle()` resets ALL objects' colors.
- **MUST hold**: `rebuild_minor_remembered_set()` visits ALL old objects.
- **Assertion**: After `begin_gc_cycle()`, count of White objects == total live objects.

## Phase 2B (LOS active)
- **MUST hold**: Every object exists in exactly one of {legacy_heap_, los_}.
- **MUST hold**: LOS objects have `GcOldBit` set AND `space_kind == LargeObject`.
- **MUST hold**: LOS objects participate in remembered set when referencing nursery.
- **MUST hold**: LOS objects are swept during full GC.
- **MUST NOT**: LOS objects appear in `legacy_heap_` list.
- **Assertion**: `los_.live_bytes() + legacy_heap_.live_bytes() == live_bytes_`.

## Phase 3B (PinnedSpace active)
- **MUST hold**: Pinned objects have `GcPinnedBit` AND `GcOldBit` AND `space_kind == Pinned`.
- **MUST hold**: PinnedSpace sweep only resets colors (no delete).
- **MUST hold**: Pinned→Nursery references are tracked in remembered set.
- **MUST NOT**: `remember_minor_owner()` skip PinnedSpace objects that reference nursery.
- **Assertion**: All interned strings have `space_kind == Pinned`.

## Phase 4B (OldSmallSpace active)
- **MUST hold**: Promoted objects move from legacy/nursery to old_small_.
- **MUST hold**: No object exists in both legacy and old_small_.
- **MUST hold**: `promote_object()` correctly unlinks from source space and inserts into old_small_.
- **MUST NOT**: Promote an object that is already old.
- **Assertion**: After young collection, all surviving promoted objects have `space_kind == OldSmall`.

## Phase 5B (NurserySpace active)
- **MUST hold**: ALL allocations route to one of {nursery_, old_small_, los_, pinned_}.
- **MUST hold**: `legacy_heap_` contains 0 objects.
- **MUST hold**: Young collection only touches nursery objects.
- **MUST NOT**: Young sweep delete an old object.
- **Assertion**: `legacy_heap_.object_count() == 0`.
- **Assertion**: `sum(all_spaces live_bytes) == live_bytes_`.

## Phase 6 (next_all removed)
- **MUST hold**: No code references `next_all`.
- **MUST hold**: Each space's iteration is complete (visits all its objects).
- **MUST NOT**: Leak any object by failing to iterate it.
- **Assertion**: `gc_verify_full()` passes with all spaces.

---

# 7. Verification Plan (Phase-by-Phase)

## Phase 1A
- Run: `gc_verify_full()` — must pass unchanged.
- Add: `static_assert(sizeof(GcHeader) == EXPECTED_SIZE)`.
- Add: In `allocate<T>()`, `assert(object->header.space_kind == (is_old ? OldSmall : Nursery))`.

## Phase 1B
- Run: `gc_verify_full()` — must pass.
- Run: `gc_stress` mode — 1000 frame cycles.
- Add: `assert(legacy_heap_.object_count() > 0)` (objects exist).
- Compare: `DEBUG_LEAK_CHECK` counters unchanged.

## Phase 1C
- Run: `gc_verify_full()` — must pass.
- Run: `gc_verify_young()` — must pass.
- Add: After `begin_gc_cycle()`, iterate all spaces and assert all colors == White.
- Add: After `rebuild_minor_remembered_set()`, verify remembered_objects_ count matches manual count.

## Phase 2B
- Run: `gc_verify_full()` — must pass.
- Run: `gc_stress` mode.
- Add: `assert(los_.live_bytes() + legacy_heap_.live_bytes() == live_bytes_)`.
- Add: Allocate 100 large arrays, trigger GC, verify all correctly collected or surviving.
- Add: Verify no large object (size >= 4KB) in `legacy_heap_`.

## Phase 3B
- Run: `gc_verify_full()` — must pass.
- Run: `gc_stress` mode.
- Add: Verify `root_environment_->header.space_kind == Pinned`.
- Add: Verify all interned strings have `space_kind == Pinned`.
- Add: Create scenario: pinned object writes nursery value → verify remembered set entry.

## Phase 4B
- Run: `gc_verify_full()` — must pass.
- Run: `gc_verify_young()` — must pass. Promotion correctness is critical.
- Add: After young collection, verify all promoted objects in `old_small_`.
- Add: Verify no old non-large non-pinned object in `legacy_heap_`.
- Add: `assert(legacy_heap_` only contains young objects`)`.
- Metric: Compare promotion count before/after refactor.

## Phase 5B
- Run: `gc_verify_full()` — must pass.
- Run: `gc_verify_young()` — must pass.
- Run: `gc_stress` with high frequency (budget_work=1).
- Add: `assert(legacy_heap_.object_count() == 0)`.
- Add: `assert(nursery_.live_bytes() + old_small_.live_bytes() + los_.live_bytes() + pinned_.live_bytes() == live_bytes_)`.
- Compare: Full test suite pass.
- Metric: Allocation throughput, GC pause times.

## Phase 6
- Run: Full test suite.
- Run: `gc_verify_full()` with stress mode.
- Run: Extended soak test (10,000+ frames).
- Verify: `next_all` does not appear in codebase (grep).
- Metric: `sizeof(GcHeader)` reduced by 8 bytes.

---

# 8. Testing Strategy

## Per-Phase Unit Tests

### Phase 1A
- `test_gc_header_size`: `static_assert(sizeof(GcHeader) == expected)`.
- `test_space_kind_set_on_alloc`: Allocate young/old objects, verify `space_kind` field.

### Phase 1B
- `test_legacy_heap_insert_and_iterate`: Insert objects, `for_each_object` visits all.
- `test_legacy_heap_destroy_all`: No leaks after `destroy_all()`.

### Phase 1C
- `test_begin_gc_cycle_resets_all_colors`: Verify all objects White after `begin_gc_cycle()`.
- `test_rebuild_remembered_set_via_spaces`: Compare result with old single-list implementation.

### Phase 2B
- `test_los_allocation_routing`: Objects >= 4KB go to LOS.
- `test_los_sweep_frees_dead`: Allocate large, make unreachable, GC, verify freed.
- `test_los_remembered_set`: LOS object writes nursery value → remembered.

### Phase 3B
- `test_pinned_root_environment`: `root_environment_` in PinnedSpace.
- `test_pinned_not_swept`: Pinned object survives GC without being root.
- `test_pinned_to_nursery_barrier`: Pinned→nursery reference tracked.
- `test_interned_string_pinned`: All interned strings in PinnedSpace.

### Phase 4B
- `test_promotion_moves_to_old_small`: Young object survives → promoted → in OldSmall.
- `test_old_direct_alloc_in_old_small`: Environment/Coroutine → OldSmall.
- `test_sweep_old_small`: Dead old objects freed correctly.

### Phase 5B
- `test_nursery_allocation`: Young objects in NurserySpace.
- `test_nursery_sweep`: Dead young objects freed.
- `test_nursery_promote_to_old_small`: Survivor → OldSmall after age threshold.
- `test_legacy_empty`: `legacy_heap_.object_count() == 0` after all routes active.

### Phase 6
- `test_no_next_all`: Compile test — `GcHeader` has no `next_all` field.
- `test_full_cycle_no_legacy`: Complete GC cycle with 4 spaces.

## Stress Tests (All Phases)

```cpp
// Run after each phase:
set_gc_stress(true, 1);  // Minimal budget — maximum GC frequency
for (int i = 0; i < 10000; ++i) {
    // allocate mix of young, old, large, pinned objects
    // write cross-generation references
    // advance_frame(1);
}
gc_verify_full();
gc_verify_young();
assert(live_bytes_ == sum_space_live_bytes());
```

## Regression Tests
- Full existing test suite must pass at every phase boundary.
- `gc_stress` + existing benchmarks.
- `DEBUG_LEAK_CHECK`: `created - destroyed == live_count` at shutdown.
- ASAN/MSAN builds for every phase.

---

# 9. Failure Recovery Plan

## Detection

| Signal | Meaning |
|--------|---------|
| `gc_verify_full()` fails | Object reachability broken, accounting wrong, or space coverage gap |
| `gc_verify_young()` fails | Remembered set incomplete, young sweep incorrect |
| `DEBUG_LEAK_CHECK` mismatch | Object leaked or double-freed |
| ASAN use-after-free | Dangling pointer — object freed but still referenced |
| ASAN heap-buffer-overflow | Slab allocation bounds error (Phase 4C) |
| `live_bytes_` != `sum(space)` | Accounting bug in split |
| `assert(legacy_heap_.object_count() == 0)` fails | Not all object types routed to new spaces |
| Crash in `promote_object()` | Object not properly in source space, or double-promotion |

## Rollback Procedure

For any phase N, to rollback to phase N-1:

1. **Remove the new space member** from Runtime.
2. **Remove the new space from `all_spaces_`**.
3. **Re-route allocations** that went to the new space back to `legacy_heap_`.
4. **Revert `allocate<T>()`** routing to previous phase's version.
5. **Revert `process_detach_queue()`** dispatch.
6. **Run `gc_verify_full()`** to confirm.

Each phase is designed so rollback is a local edit (remove one branch from routing, remove one space).

## Logs/Metrics to Inspect on Failure

- `ZephyrGcStats`: `live_bytes`, `live_objects`, `total_allocations`, `total_promotions`, `barrier_hits`.
- Per-space: `object_count`, `live_bytes`.
- `remembered_objects_.size()`.
- `gray_stack_.size()` (should be 0 outside marking).
- `detach_queue_.size()` (should be 0 outside detach phase).

---

# 10. Final Integration Criteria

## "Done" Definition

| Criterion | Condition |
|-----------|-----------|
| `objects_` removed | `GcHeader::next_all` does not exist. No `objects_` member in Runtime. |
| All spaces active | `nursery_`, `old_small_`, `los_`, `pinned_` all contain objects in normal operation |
| `legacy_heap_` removed | Class and member do not exist |
| `gc_verify_full` passes | In stress mode (budget=1), 10,000+ frames, no failure |
| `gc_verify_young` passes | Same stress conditions |
| Fragmentation metrics | `ZephyrGcStats` reports per-space `live_bytes`, `used_bytes`, `object_count`, fragmentation percentage |
| Accounting consistent | `live_bytes_ == sum(all spaces)` asserted on every GC cycle |
| No regression | Full test suite passes, benchmark within 5% of pre-refactor |
| ASAN clean | No memory errors under stress |
| `DEBUG_LEAK_CHECK` clean | `created - destroyed == final live count` at shutdown |
| `sizeof(GcHeader)` reduced | From ~28B to ~20B (8B savings from removing `next_all`) |
| `GcPinnedBit` active | Actually used in PinnedSpace routing, no longer dead code |
| `space_kind` authoritative | Used in `is_old_object()` or equivalent checks |

## Performance Budget

- Allocation throughput: < 5% regression (due to routing branch in `allocate<T>`)
- Young GC pause: < 5% regression (nursery sweep walks only nursery, not entire heap — potential improvement)
- Full GC pause: neutral (sweep walks same total objects, just via different spaces)
- Memory overhead: +2 bytes per object (GcHeader growth) partially offset by -8 bytes at Phase 6
- Overall: net 6 bytes saved per object at Phase 6 completion

---

*Generated: 2026-03-22 | Execution plan for: `src/zephyr.cpp`, `include/zephyr/api.hpp`*
