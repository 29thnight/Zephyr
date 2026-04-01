// zephyr_gc_impl.cpp — Generational GC: nursery/old-gen allocation, tracing,
// mark-and-sweep, card table, write barrier, and coroutine frame compaction.
#include "zephyr_internal.hpp"
#include "zephyr_vm_dispatch.h"

namespace zephyr {

// Walk every node of an environment parent-chain. Visitor returns true to continue, false to stop.
template <typename Visitor>
void walk_environment_chain(Environment* env, Visitor&& visitor) {
    for (Environment* current = env; current != nullptr; current = current->parent) {
        if (!visitor(current)) {
            break;
        }
    }
}

template <typename T>
T* as_object(const Value& value, ObjectKind kind, const std::string& context) {
    if (!value.is_object() || value.as_object()->kind != kind) {
        fail(context);
    }
    return static_cast<T*>(value.as_object());
}

constexpr const char* kInt48RangeErrorMessage = "Integer value out of 48-bit NaN-boxing range.";

bool try_add_int48(std::int64_t left, std::int64_t right, std::int64_t& out) {
    if ((right > 0 && left > Value::kIntMax - right) ||
        (right < 0 && left < Value::kIntMin - right)) {
        return false;
    }
    out = left + right;
    return true;
}

bool try_sub_int48(std::int64_t left, std::int64_t right, std::int64_t& out) {
    if ((right > 0 && left < Value::kIntMin + right) ||
        (right < 0 && left > Value::kIntMax + right)) {
        return false;
    }
    out = left - right;
    return true;
}

bool try_mul_int48(std::int64_t left, std::int64_t right, std::int64_t& out) {
    if (left == 0 || right == 0) {
        out = 0;
        return true;
    }
    if (left > 0) {
        if (right > 0) {
            if (left > Value::kIntMax / right) {
                return false;
            }
        } else if (right < Value::kIntMin / left) {
            return false;
        }
    } else if (right > 0) {
        if (left < Value::kIntMin / right) {
            return false;
        }
    } else if (left < Value::kIntMax / right) {
        return false;
    }
    out = left * right;
    return Value::int_fits(out);
}

TokenType token_type_for_bytecode_binary_op(BytecodeOp op) {
    switch (op) {
        case BytecodeOp::Add: return TokenType::Plus;
        case BytecodeOp::Subtract: return TokenType::Minus;
        case BytecodeOp::Multiply: return TokenType::Star;
        case BytecodeOp::Divide: return TokenType::Slash;
        case BytecodeOp::Modulo: return TokenType::Percent;
        case BytecodeOp::Equal: return TokenType::EqualEqual;
        case BytecodeOp::NotEqual: return TokenType::BangEqual;
        case BytecodeOp::Less: return TokenType::Less;
        case BytecodeOp::LessEqual: return TokenType::LessEqual;
        case BytecodeOp::Greater: return TokenType::Greater;
        case BytecodeOp::GreaterEqual: return TokenType::GreaterEqual;
        default: return TokenType::Plus;
    }
}

bool evaluate_int_comparison_opcode(BytecodeOp op, std::int64_t left, std::int64_t right) {
    switch (op) {
        case BytecodeOp::Equal: return left == right;
        case BytecodeOp::NotEqual: return left != right;
        case BytecodeOp::Less: return left < right;
        case BytecodeOp::LessEqual: return left <= right;
        case BytecodeOp::Greater: return left > right;
        case BytecodeOp::GreaterEqual: return left >= right;
        default: return false;
    }
}

std::vector<std::string> collect_struct_field_names(const StructTypeObject* type) {
    std::vector<std::string> field_names;
    if (type == nullptr) {
        return field_names;
    }
    field_names.reserve(type->fields.size());
    for (const auto& field : type->fields) {
        field_names.push_back(field.name);
    }
    return field_names;
}

Shape* shape_for_struct_type(const StructTypeObject* type) {
    const auto field_names = collect_struct_field_names(type);
    return Shape::get_or_create(field_names);
}

struct LazyInstructionSpan {
    explicit LazyInstructionSpan(const CompactInstruction& instruction_ref) : instruction(&instruction_ref) {}

    operator const Span&() const {
        if (!cached.has_value()) {
            cached = instruction_span(*instruction);
        }
        return *cached;
    }

    const CompactInstruction* instruction = nullptr;
    mutable std::optional<Span> cached;
};

struct LazyInstructionMetadata {
    LazyInstructionMetadata(const std::vector<InstructionMetadata>& metadata_ref, std::size_t instruction_index)
        : metadata(&metadata_ref), index(instruction_index) {}

    const InstructionMetadata& operator*() const {
        if (cached == nullptr) {
            cached = &(*metadata)[index];
        }
        return *cached;
    }

    const std::vector<InstructionMetadata>* metadata = nullptr;
    std::size_t index = 0;
    mutable const InstructionMetadata* cached = nullptr;
};

void initialize_struct_instance(StructInstanceObject* instance) {
    if (instance == nullptr) {
        return;
    }
    if (instance->type->cached_shape == nullptr) {
        instance->type->cached_shape = shape_for_struct_type(instance->type);
    }
    instance->shape = instance->type->cached_shape;
    const std::size_t field_count = instance->shape == nullptr ? 0 : static_cast<std::size_t>(instance->shape->field_count);
    instance->field_values.assign(field_count, Value::nil());
}

void Environment::trace(Runtime& runtime) {
    runtime.mark_object(parent);
    for (auto& entry : values) {
        if (entry.second.cell != nullptr) {
            runtime.mark_object(entry.second.cell);
        } else {
            runtime.mark_value(entry.second.value);
        }
    }
}

void UpvalueCellObject::trace(Runtime& runtime) {
    runtime.mark_value(value);
}

void ArrayObject::trace(Runtime& runtime) {
    for (const auto& element : elements) {
        runtime.mark_value(element);
    }
}

void ScriptFunctionObject::trace(Runtime& runtime) {
    runtime.mark_object(closure);
    for (auto* cell : captured_upvalues) {
        runtime.mark_object(cell);
    }
}

void StructInstanceObject::trace(Runtime& runtime) {
    runtime.mark_object(type);
    for (const auto& value : field_values) {
        runtime.mark_value(value);
    }
}

void EnumInstanceObject::trace(Runtime& runtime) {
    runtime.mark_object(type);
    for (const auto& value : payload) {
        runtime.mark_value(value);
    }
}

void CoroutineObject::trace(Runtime& runtime) {
    for (const auto& frame : frames) {
        runtime.mark_object(frame.closure);
        runtime.mark_object(frame.root_env);
        runtime.mark_object(frame.current_env);
        runtime.mark_object(frame.global_resolution_env);
        for (auto* cell : frame.captured_upvalues) {
            runtime.mark_object(cell);
        }
        for (auto* scope : frame.scope_stack) {
            runtime.mark_object(scope);
        }
        for (const auto& value : frame.stack) {
            runtime.mark_value(value);
        }
        for (const auto& value : frame.locals) {
            runtime.mark_value(value);
        }
        for (const auto& value : frame.regs) {
            runtime.mark_value(value);
        }
        for (int i = 0; i < frame.reg_count; ++i) {
            runtime.mark_value(frame.inline_regs[i]);
        }
        for (const auto& value : frame.spill_regs) {
            runtime.mark_value(value);
        }
    }
}

void ModuleNamespaceObject::trace(Runtime& runtime) {
    runtime.mark_object(environment);
}

void StructTypeObject::trace(Runtime& runtime) {
    for (auto& [method_name, val] : static_methods) {
        runtime.mark_value(val);
    }
    for (auto& [method_name, val] : instance_methods) {
        runtime.mark_value(val);
    }
}

Runtime::Runtime(ZephyrVMConfig config) : config_(std::move(config)) {
    // Phase 6: four-space heap. LegacyHeapSpace removed (was empty after Phase 5B).
    all_spaces_ = {&los_, &pinned_, &old_small_, &nursery_};
    los_.bind_live_bytes(live_bytes_);
    pinned_.bind_live_bytes(live_bytes_);
    old_small_.bind_live_bytes(live_bytes_);
    old_small_.bump_release_callback_ = [this](GcObject* obj) {
        nursery_.release_promoted_slot(obj);
    };
    nursery_.bind_live_bytes(live_bytes_);

    incremental_trigger_bytes_ = config_.gc.incremental_trigger_bytes;
    nursery_trigger_bytes_ = config_.gc.nursery_trigger_bytes;
    promotion_survival_threshold_ = std::max<std::size_t>(1, config_.gc.promotion_survival_threshold);
    large_object_threshold_bytes_ = std::max<std::size_t>(256, config_.gc.large_object_threshold_bytes);
    young_collection_frequency_ = std::max<std::size_t>(1, config_.gc.young_collection_frequency);
    gc_stress_budget_work_ = config_.gc.incremental_budget_work == 0 ? 1 : config_.gc.incremental_budget_work;
    // Phase 3B: root_environment_ is a permanent root — allocate directly into PinnedSpace.
    // promote_object() is no longer needed (allocate_pinned sets GcPinnedBit|GcOldBit).
    root_environment_ = allocate_pinned<Environment>(nullptr, EnvironmentKind::Root);

    // Pre-allocate register pool for execute_register_bytecode (avoids per-call heap alloc)
    register_pool_.resize(8192, Value::nil());
    rooted_value_vectors_.push_back(&register_pool_);
}

Runtime::~Runtime() {
    // Pinned objects must outlive all other objects since they may be referenced.
    // Destroy: nursery → old_small → LOS → pinned last.
    nursery_.destroy_all();
    old_small_.destroy_all();
    los_.destroy_all();
    pinned_.destroy_all();
}

void Runtime::mark_value(const Value& value) {
    if (!value.is_object()) {
        return;
    }
    mark_object(value.as_object());
}

void Runtime::mark_object(GcObject* object) {
    if (object == nullptr || object->header.color != GcColor::White) {
        return;
    }
    object->header.color = GcColor::Gray;
    gray_stack_.push_back(object);
}

void Runtime::mark_young_value(const Value& value) {
    if (!value.is_object()) {
        return;
    }
    mark_young_object(value.as_object());
}

void Runtime::mark_young_object(GcObject* object) {
    if (object == nullptr || is_old_object(object) || object->header.color != GcColor::White) {
        return;
    }
    object->header.color = GcColor::Gray;
    gray_stack_.push_back(object);
}

void Runtime::mark_young_root_value(const Value& value) {
    if (!value.is_object()) {
        return;
    }
    GcObject* object = value.as_object();
    if (object == nullptr) {
        return;
    }
    if (is_old_object(object)) {
        trace_young_references(object);
    } else {
        mark_young_object(object);
    }
}

void Runtime::visit_root_references(const std::function<void(GcObject*)>& object_visitor,
                                    const std::function<void(const Value&)>& value_visitor) const {
    object_visitor(root_environment_);
    for (auto* environment : active_environments_) {
        object_visitor(environment);
    }
    for (const auto* values : rooted_value_vectors_) {
        if (values == nullptr) {
            continue;
        }
        for (const auto& value : *values) {
            value_visitor(value);
        }
    }
    for (const auto* value : rooted_values_) {
        if (value != nullptr) {
            value_visitor(*value);
        }
    }
    for (auto* coroutine : active_coroutines_) {
        object_visitor(coroutine);
    }
    for (auto* coroutine : suspended_coroutines_) {
        object_visitor(coroutine);
    }
    for (const auto& entry : retained_coroutines_) {
        object_visitor(entry.second);
    }
    for (const auto& entry : interned_strings_) {
        object_visitor(entry.second);
    }
    for (auto* callback : native_callback_registry_) {
        object_visitor(callback);
    }
    for (auto* object : pinned_debug_objects_) {
        object_visitor(object);
    }
    for (const auto& entry : modules_) {
        object_visitor(entry.second.environment);
        object_visitor(entry.second.namespace_object);
    }
}

bool Runtime::accounting_consistent() const {
    // Phase 6: all four spaces track their own live_bytes_ independently.
    // Their sum must equal the global live_bytes_ counter exactly.
    const std::size_t tracked = nursery_.live_bytes() + old_small_.live_bytes()
                              + los_.live_bytes()     + pinned_.live_bytes();
    return tracked == live_bytes_;
}

bool Runtime::should_allocate_old(ObjectKind kind, std::size_t size_bytes) const {
    // Large objects are handled by the LOS branch before this function is called.
    // Environments and Coroutines are long-lived and benefit from old-gen placement.
    (void)size_bytes;
    return kind == ObjectKind::Coroutine || kind == ObjectKind::Environment;
}

bool Runtime::is_old_object(const GcObject* object) const {
    return object != nullptr && (object->header.flags & GcOldBit) != 0;
}

bool Runtime::is_large_object(const GcObject* object) const {
    return object != nullptr && object->header.size_bytes >= large_object_threshold_bytes_;
}

void Runtime::promote_object(GcObject* object) {
    if (object == nullptr || is_old_object(object)) {
        return;
    }
    object->header.flags |= GcOldBit;
    // Phase 5B→5C: caller (sweep_young_objects) has already unlinked the object
    // from nursery_.objects_ and decremented nursery_.live_bytes_own_.
    // old_small_.insert() increments old_small_.live_bytes_.
    // global live_bytes_ stays unchanged (object remains live, just changes space).
    // space_kind must be updated BEFORE insert() so the insert assert passes.
    object->header.space_kind = GcSpaceKind::OldSmall;
    old_small_.insert(object);
    object->header.age = static_cast<std::uint8_t>(std::min<std::size_t>(promotion_survival_threshold_, 255));
    if (object->kind == ObjectKind::Environment) {
        rebuild_environment_cards(static_cast<Environment*>(object));
    } else if (object->kind == ObjectKind::Array) {
        rebuild_array_cards(static_cast<ArrayObject*>(object));
    } else if (object->kind == ObjectKind::StructInstance) {
        rebuild_struct_cards(static_cast<StructInstanceObject*>(object));
    } else if (object->kind == ObjectKind::EnumInstance) {
        rebuild_enum_cards(static_cast<EnumInstanceObject*>(object));
    } else if (object->kind == ObjectKind::Coroutine) {
        rebuild_coroutine_cards(static_cast<CoroutineObject*>(object));
    }
    if (has_direct_young_reference(object)) {
        remember_minor_owner(object);
    }
    ++total_promotions_;
    young_promoted_bytes_this_cycle_ += object->header.size_bytes;
}

// ── Phase 7 (compaction) implementation ──────────────────────────────────────

bool Runtime::is_object_relocatable(const GcObject* obj) noexcept {
    // Environment and CoroutineObject are excluded: Environment has complex
    // parent-chain semantics (Binding* pointers in CoroutineFrameState would
    // dangle); CoroutineObject frames contain raw Binding* pointers into
    // Environment::values entries that cannot be resolved via forwarding.
    switch (obj->kind) {
        case ObjectKind::Environment:
        case ObjectKind::Coroutine:
            return false;
        default:
            return true;
    }
}

GcObject* Runtime::relocate_object(GcObject* old_obj, void* dest) {
    switch (old_obj->kind) {
        case ObjectKind::String:
            return new (dest) StringObject(std::move(*static_cast<StringObject*>(old_obj)));
        case ObjectKind::Array:
            return new (dest) ArrayObject(std::move(*static_cast<ArrayObject*>(old_obj)));
        case ObjectKind::UpvalueCell:
            return new (dest) UpvalueCellObject(std::move(*static_cast<UpvalueCellObject*>(old_obj)));
        case ObjectKind::ScriptFunction:
            return new (dest) ScriptFunctionObject(std::move(*static_cast<ScriptFunctionObject*>(old_obj)));
        case ObjectKind::NativeFunction:
            return new (dest) NativeFunctionObject(std::move(*static_cast<NativeFunctionObject*>(old_obj)));
        case ObjectKind::StructType:
            return new (dest) StructTypeObject(std::move(*static_cast<StructTypeObject*>(old_obj)));
        case ObjectKind::StructInstance:
            return new (dest) StructInstanceObject(std::move(*static_cast<StructInstanceObject*>(old_obj)));
        case ObjectKind::EnumType:
            return new (dest) EnumTypeObject(std::move(*static_cast<EnumTypeObject*>(old_obj)));
        case ObjectKind::EnumInstance:
            return new (dest) EnumInstanceObject(std::move(*static_cast<EnumInstanceObject*>(old_obj)));
        case ObjectKind::ModuleNamespace:
            return new (dest) ModuleNamespaceObject(std::move(*static_cast<ModuleNamespaceObject*>(old_obj)));
        default:
            return nullptr;  // Environment, Coroutine: not relocatable
    }
}

void Runtime::fixup_object_references(GcObject* object) {
    if (object == nullptr) return;
    switch (object->kind) {
        case ObjectKind::String:
        case ObjectKind::NativeFunction:
        case ObjectKind::StructType:
        case ObjectKind::EnumType:
            return;  // no GC references
        case ObjectKind::Environment: {
            auto* env = static_cast<Environment*>(object);
            env->parent = static_cast<Environment*>(gc_resolve_forwarding(env->parent));
            for (auto& [name, binding] : env->values) {
                if (binding.cell != nullptr)
                    binding.cell = static_cast<UpvalueCellObject*>(gc_resolve_forwarding(binding.cell));
                gc_fixup_value(binding.value);
            }
            return;
        }
        case ObjectKind::UpvalueCell: {
            auto* cell = static_cast<UpvalueCellObject*>(object);
            gc_fixup_value(cell->value);
            return;
        }
        case ObjectKind::Array: {
            auto* array = static_cast<ArrayObject*>(object);
            for (auto& element : array->elements) gc_fixup_value(element);
            return;
        }
        case ObjectKind::ScriptFunction: {
            auto* fn = static_cast<ScriptFunctionObject*>(object);
            fn->closure = static_cast<Environment*>(gc_resolve_forwarding(fn->closure));
            for (auto& cell : fn->captured_upvalues)
                cell = static_cast<UpvalueCellObject*>(gc_resolve_forwarding(cell));
            return;
        }
        case ObjectKind::StructInstance: {
            auto* inst = static_cast<StructInstanceObject*>(object);
            inst->type = static_cast<StructTypeObject*>(gc_resolve_forwarding(inst->type));
            for (auto& value : inst->field_values) gc_fixup_value(value);
            return;
        }
        case ObjectKind::EnumInstance: {
            auto* inst = static_cast<EnumInstanceObject*>(object);
            inst->type = static_cast<EnumTypeObject*>(gc_resolve_forwarding(inst->type));
            for (auto& value : inst->payload) gc_fixup_value(value);
            return;
        }
        case ObjectKind::Coroutine: {
            auto* coro = static_cast<CoroutineObject*>(object);
            for (auto& frame : coro->frames) {
                frame.closure = static_cast<Environment*>(gc_resolve_forwarding(frame.closure));
                frame.root_env = static_cast<Environment*>(gc_resolve_forwarding(frame.root_env));
                frame.current_env = static_cast<Environment*>(gc_resolve_forwarding(frame.current_env));
                frame.global_resolution_env = static_cast<Environment*>(gc_resolve_forwarding(frame.global_resolution_env));
                for (auto& cell : frame.captured_upvalues)
                    cell = static_cast<UpvalueCellObject*>(gc_resolve_forwarding(cell));
                for (auto& scope : frame.scope_stack)
                    scope = static_cast<Environment*>(gc_resolve_forwarding(scope));
                for (auto& value : frame.stack) gc_fixup_value(value);
                for (auto& value : frame.locals) gc_fixup_value(value);
                for (auto& owner : frame.local_binding_owners)
                    owner = static_cast<Environment*>(gc_resolve_forwarding(owner));
                for (auto& owner : frame.global_binding_owners)
                    owner = static_cast<Environment*>(gc_resolve_forwarding(owner));
            }
            return;
        }
        case ObjectKind::ModuleNamespace: {
            auto* ns = static_cast<ModuleNamespaceObject*>(object);
            ns->environment = static_cast<Environment*>(gc_resolve_forwarding(ns->environment));
            return;
        }
    }
}

void Runtime::fixup_root_references() {
    // root_environment_ is pinned — resolve for completeness (always no-op).
    root_environment_ = static_cast<Environment*>(gc_resolve_forwarding(root_environment_));

    for (auto& env : active_environments_)
        env = static_cast<Environment*>(gc_resolve_forwarding(env));
    for (auto* values : rooted_value_vectors_) {
        if (values == nullptr) continue;
        // const_cast is safe: the vector is owned by a live C++ stack frame
        // and we are only updating the GcObject* inside its Value elements.
        for (auto& value : *const_cast<std::vector<Value>*>(values))
            gc_fixup_value(value);
    }
    for (auto* value : rooted_values_) {
        if (value != nullptr) gc_fixup_value(*const_cast<Value*>(value));
    }
    for (auto& coro : active_coroutines_)
        coro = static_cast<CoroutineObject*>(gc_resolve_forwarding(coro));
    {
        // unordered_set elements are const — rebuild after forwarding pointer fixup
        std::unordered_set<CoroutineObject*> fixed_suspended;
        fixed_suspended.reserve(suspended_coroutines_.size());
        for (auto* coro : suspended_coroutines_)
            fixed_suspended.insert(static_cast<CoroutineObject*>(gc_resolve_forwarding(coro)));
        suspended_coroutines_ = std::move(fixed_suspended);
    }
    for (auto& [key, coro] : retained_coroutines_)
        coro = static_cast<CoroutineObject*>(gc_resolve_forwarding(coro));
    for (auto& [key, str] : interned_strings_)
        str = static_cast<StringObject*>(gc_resolve_forwarding(str));
    for (auto& cb : native_callback_registry_)
        cb = static_cast<NativeFunctionObject*>(gc_resolve_forwarding(cb));
    for (auto& obj : pinned_debug_objects_)
        obj = gc_resolve_forwarding(obj);
    for (auto& [key, entry] : modules_) {
        entry.environment = static_cast<Environment*>(gc_resolve_forwarding(entry.environment));
        entry.namespace_object = static_cast<ModuleNamespaceObject*>(gc_resolve_forwarding(entry.namespace_object));
    }

    // remembered_objects_ may hold pointers to compacted objects.
    for (auto& obj : remembered_objects_)
        obj = gc_resolve_forwarding(obj);
}

void Runtime::compact_old_generation() {
    assert(gc_phase_ == ZephyrGcPhase::Idle
        && "compact_old_generation: must be called at Idle phase (after full collection)");

    // ── Step 1: collect compaction candidates ─────────────────────────────────
    // Target: promoted bump objects (GcBumpAllocBit) in OldSmallSpace that are
    // relocatable.  Moving them to slab slots frees retained nursery chunks.
    struct CompactionCandidate {
        GcObject* old_obj;
        std::size_t original_size;
    };
    std::vector<CompactionCandidate> candidates;
    old_small_.for_each_object([&](GcObject* obj) {
        if ((obj->header.flags & GcBumpAllocBit) != 0 && is_object_relocatable(obj)) {
            candidates.push_back({obj, obj->header.size_bytes});
        }
    });
    if (candidates.empty()) return;

    // ── Step 2: move objects to slab slots ────────────────────────────────────
    struct MovedObject {
        GcObject* old_obj;
        GcObject* new_obj;
        std::size_t old_size;
        std::size_t new_size;
    };
    std::vector<MovedObject> moved;
    moved.reserve(candidates.size());

    for (auto& c : candidates) {
        void* slab_slot = old_small_.try_alloc_slab(c.original_size);
        if (slab_slot == nullptr) continue;  // no available slab slot

        GcObject* new_obj = relocate_object(c.old_obj, slab_slot);
        if (new_obj == nullptr) continue;  // type not relocatable (shouldn't happen)

        // Determine slab slot size (which may be larger than the object's logical size).
        const std::size_t slot_sz = OldSmallSpace::slot_size_for(c.original_size);
        assert(slot_sz > 0);

        // Adjust header on the new object.
        new_obj->header.size_bytes = static_cast<std::uint32_t>(slot_sz);
        new_obj->header.flags      = (new_obj->header.flags & ~GcBumpAllocBit) | GcSlabBit;
        new_obj->header.next_all   = nullptr;
        new_obj->header.next_gray  = nullptr;
        new_obj->header.color      = GcColor::White;

        // Set forwarding pointer at old location.
        // At Idle phase, color == Gray is an unambiguous sentinel for "forwarded".
        c.old_obj->header.color     = GcColor::Gray;
        c.old_obj->header.next_gray = new_obj;

        moved.push_back({c.old_obj, new_obj, c.original_size, slot_sz});
    }

    if (moved.empty()) return;

    // ── Step 3: fixup OldSmallSpace linked list ──────────────────────────────
    // Replace forwarded nodes with their new locations in O(n).
    old_small_.fixup_linked_list_for_compaction();

    // ── Step 4: fixup all object references across all spaces ────────────────
    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* obj) {
            fixup_object_references(obj);
        });
    }

    // ── Step 5: fixup root references ────────────────────────────────────────
    fixup_root_references();

    // ── Step 6: accounting adjustment ────────────────────────────────────────
    // Slab slots may be larger than the original bump allocation.
    for (auto& m : moved) {
        const std::int64_t delta = static_cast<std::int64_t>(m.new_size) - static_cast<std::int64_t>(m.old_size);
        if (delta != 0) {
            old_small_.adjust_live_bytes(delta);
            live_bytes_ = static_cast<std::size_t>(static_cast<std::int64_t>(live_bytes_) + delta);
        }
    }

    // ── Step 7: destroy old (moved-from) objects and release chunk slots ─────
    for (auto& m : moved) {
        // Old object is in moved-from state (members are empty but valid).
        // Destroy it — the destructor runs on empty containers (no-op).
        m.old_obj->~GcObject();
        // Release the chunk's promoted_count; if it drops to zero the chunk
        // moves to free_chunk_list_ for reuse by future nursery allocations.
        nursery_.release_promoted_slot(m.old_obj);
    }

    total_compactions_ += moved.size();
}

void Runtime::trace_young_references(const GcObject* object) {
    if (object == nullptr) {
        return;
    }
    if (is_old_object(object)) {
        switch (object->kind) {
            case ObjectKind::Environment:
                trace_young_environment(static_cast<Environment*>(const_cast<GcObject*>(object)));
                return;
            case ObjectKind::Array:
                trace_young_array(static_cast<ArrayObject*>(const_cast<GcObject*>(object)));
                return;
            case ObjectKind::StructInstance:
                trace_young_struct(static_cast<StructInstanceObject*>(const_cast<GcObject*>(object)));
                return;
            case ObjectKind::EnumInstance:
                trace_young_enum(static_cast<EnumInstanceObject*>(const_cast<GcObject*>(object)));
                return;
            case ObjectKind::Coroutine:
                trace_young_coroutine(static_cast<CoroutineObject*>(const_cast<GcObject*>(object)));
                return;
            default:
                break;
        }
    }
    visit_object_references(
        object,
        [this](GcObject* child) { mark_young_object(child); },
        [this](const Value& value) { mark_young_value(value); });
}

bool Runtime::value_card_has_young_reference(const std::vector<Value>& values, std::size_t card_index) const {
    const std::size_t begin = card_index * kGcValueCardSpan;
    if (begin >= values.size()) {
        return false;
    }
    const std::size_t end = std::min(values.size(), begin + kGcValueCardSpan);
    for (std::size_t index = begin; index < end; ++index) {
        if (values[index].is_object() && values[index].as_object() != nullptr && !is_old_object(values[index].as_object())) {
            return true;
        }
    }
    return false;
}

bool Runtime::environment_card_has_young_reference(const Environment* environment, std::size_t card_index) const {
    if (environment == nullptr) {
        return false;
    }
    const std::size_t begin = card_index * kGcValueCardSpan;
    if (begin >= environment->binding_names.size()) {
        return false;
    }
    const std::size_t end = std::min(environment->binding_names.size(), begin + kGcValueCardSpan);
    for (std::size_t index = begin; index < end; ++index) {
        const auto binding_it = environment->values.find(environment->binding_names[index]);
        if (binding_it == environment->values.end()) {
            continue;
        }
        if (binding_it->second.cell != nullptr) {
            if (!is_old_object(binding_it->second.cell)) {
                return true;
            }
            continue;
        }
        if (binding_it->second.value.is_object() && binding_it->second.value.as_object() != nullptr &&
            !is_old_object(binding_it->second.value.as_object())) {
            return true;
        }
    }
    return false;
}

bool Runtime::struct_card_has_young_reference(const StructInstanceObject* instance, std::size_t card_index) const {
    if (instance == nullptr) {
        return false;
    }
    const std::size_t begin = card_index * kGcValueCardSpan;
    if (begin >= instance->field_values.size()) {
        return false;
    }
    const std::size_t end = std::min(instance->field_values.size(), begin + kGcValueCardSpan);
    for (std::size_t field_index = begin; field_index < end; ++field_index) {
        const Value& value = instance->field_values[field_index];
        if (value.is_object() && value.as_object() != nullptr && !is_old_object(value.as_object())) {
            return true;
        }
    }
    return false;
}

bool Runtime::owner_is_fully_card_tracked(const GcObject* object) const {
    if (object == nullptr) {
        return false;
    }
    switch (object->kind) {
        case ObjectKind::Environment:
            return static_cast<const Environment*>(object)->kind == EnvironmentKind::Local;
        case ObjectKind::Array:
        case ObjectKind::StructInstance:
        case ObjectKind::EnumInstance:
            return true;
        default:
            return false;
    }
}

bool Runtime::owner_has_dirty_minor_cards(const GcObject* object) const {
    if (object == nullptr) {
        return false;
    }
    switch (object->kind) {
        case ObjectKind::Environment: {
            const auto* environment = static_cast<const Environment*>(object);
            return std::any_of(environment->remembered_cards.begin(), environment->remembered_cards.end(),
                               [](std::uint64_t w) { return w != 0; });
        }
        case ObjectKind::Array: {
            const auto* array = static_cast<const ArrayObject*>(object);
            return std::any_of(array->remembered_cards.begin(), array->remembered_cards.end(),
                               [](std::uint64_t w) { return w != 0; });
        }
        case ObjectKind::StructInstance: {
            const auto* instance = static_cast<const StructInstanceObject*>(object);
            return std::any_of(instance->remembered_cards.begin(), instance->remembered_cards.end(),
                               [](std::uint64_t w) { return w != 0; });
        }
        case ObjectKind::EnumInstance: {
            const auto* instance = static_cast<const EnumInstanceObject*>(object);
            return std::any_of(instance->remembered_cards.begin(), instance->remembered_cards.end(),
                               [](std::uint64_t w) { return w != 0; });
        }
        default:
            return false;
    }
}

void Runtime::trace_young_environment(Environment* environment) {
    if (environment == nullptr || environment->kind != EnvironmentKind::Local) {
        if (environment != nullptr) {
            visit_object_references(
                environment,
                [this](GcObject* child) { mark_young_object(child); },
                [this](const Value& value) { mark_young_value(value); });
        }
        return;
    }
    if (environment->remembered_cards.empty()) {
        visit_object_references(
            environment,
            [this](GcObject* child) { mark_young_object(child); },
            [this](const Value& value) { mark_young_value(value); });
        return;
    }

    // Phase 3.3: bitmap scan — only visit dirty card granules.
    environment->remembered_cards.resize(value_card_count(environment->binding_names.size()), 0);
    for_each_dirty_card(environment->remembered_cards, [&](std::size_t card_index) {
        const std::size_t begin = card_index * kGcValueCardSpan;
        if (begin >= environment->binding_names.size()) {
            environment->remembered_cards[card_index / kGcCardsPerWord] &= ~(std::uint64_t(1) << (card_index % kGcCardsPerWord));
            return;
        }
        const std::size_t end = std::min(environment->binding_names.size(), begin + kGcValueCardSpan);
        for (std::size_t index = begin; index < end; ++index) {
            const auto binding_it = environment->values.find(environment->binding_names[index]);
            if (binding_it == environment->values.end()) {
                continue;
            }
            if (binding_it->second.cell != nullptr) {
                mark_young_object(binding_it->second.cell);
            } else {
                mark_young_value(binding_it->second.value);
            }
        }
        if (!environment_card_has_young_reference(environment, card_index)) {
            environment->remembered_cards[card_index / kGcCardsPerWord] &= ~(std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    });
}

void Runtime::trace_young_value_cards(const std::vector<Value>& values, std::vector<std::uint64_t>& cards) {
    if (cards.empty()) {
        for (const auto& value : values) {
            mark_young_value(value);
        }
        return;
    }

    // Phase 3.3: bitmap scan.
    cards.resize(value_card_count(values.size()), 0);
    for_each_dirty_card(cards, [&](std::size_t card_index) {
        const std::size_t begin = card_index * kGcValueCardSpan;
        if (begin >= values.size()) {
            cards[card_index / kGcCardsPerWord] &= ~(std::uint64_t(1) << (card_index % kGcCardsPerWord));
            return;
        }
        const std::size_t end = std::min(values.size(), begin + kGcValueCardSpan);
        for (std::size_t index = begin; index < end; ++index) {
            mark_young_value(values[index]);
        }
        if (!value_card_has_young_reference(values, card_index)) {
            cards[card_index / kGcCardsPerWord] &= ~(std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    });
}

void Runtime::trace_young_array(ArrayObject* array) {
    if (array == nullptr) {
        return;
    }
    trace_young_value_cards(array->elements, array->remembered_cards);
}

void Runtime::trace_young_struct(StructInstanceObject* instance) {
    if (instance == nullptr) {
        return;
    }
    if (instance->remembered_cards.empty()) {
        for (const auto& value : instance->field_values) {
            mark_young_value(value);
        }
        return;
    }

    // Phase 3.3: bitmap scan.
    instance->remembered_cards.resize(value_card_count(instance->field_values.size()), 0);
    for_each_dirty_card(instance->remembered_cards, [&](std::size_t card_index) {
        const std::size_t begin = card_index * kGcValueCardSpan;
        if (begin >= instance->field_values.size()) {
            instance->remembered_cards[card_index / kGcCardsPerWord] &= ~(std::uint64_t(1) << (card_index % kGcCardsPerWord));
            return;
        }
        const std::size_t end = std::min(instance->field_values.size(), begin + kGcValueCardSpan);
        for (std::size_t field_index = begin; field_index < end; ++field_index) {
            mark_young_value(instance->field_values[field_index]);
        }
        if (!struct_card_has_young_reference(instance, card_index)) {
            instance->remembered_cards[card_index / kGcCardsPerWord] &= ~(std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    });
}

void Runtime::trace_young_enum(EnumInstanceObject* instance) {
    if (instance == nullptr) {
        return;
    }
    trace_young_value_cards(instance->payload, instance->remembered_cards);
}

void Runtime::trace_young_coroutine(CoroutineObject* coroutine) {
    if (coroutine == nullptr) {
        return;
    }
    for (auto& frame : coroutine->frames) {
        mark_young_object(frame.closure);
        mark_young_object(frame.root_env);
        mark_young_object(frame.current_env);
        for (auto* cell : frame.captured_upvalues) {
            mark_young_object(cell);
        }
        for (auto* scope : frame.scope_stack) {
            mark_young_object(scope);
        }
        if (coroutine->suspended) {
            trace_young_value_cards(frame.stack, frame.stack_cards);
            trace_young_value_cards(frame.locals, frame.local_cards);
            if (!frame.regs.empty()) {
                trace_young_value_cards(frame.regs, frame.reg_cards);
            } else {
                for (int i = 0; i < frame.reg_count; ++i) {
                    mark_young_value(frame.inline_regs[i]);
                }
            }
        } else {
            for (const auto& value : frame.stack) {
                mark_young_value(value);
            }
            for (const auto& value : frame.locals) {
                mark_young_value(value);
            }
            for (const auto& value : frame.regs) {
                mark_young_value(value);
            }
            for (int i = 0; i < frame.reg_count; ++i) {
                mark_young_value(frame.inline_regs[i]);
            }
        }
    }
}

bool Runtime::has_direct_young_reference(const GcObject* object) const {
    bool found = false;
    visit_object_references(
        object,
        [this, &found](GcObject* child) {
            if (child != nullptr && !is_old_object(child)) {
                found = true;
            }
        },
        [this, &found](const Value& value) {
            if (value.is_object() && value.as_object() != nullptr && !is_old_object(value.as_object())) {
                found = true;
            }
        });
    return found;
}

void Runtime::remember_minor_owner(GcObject* object) {
    if (object == nullptr || !is_old_object(object)) {
        return;
    }
    if (object->kind == ObjectKind::Environment) {
        const auto* environment = static_cast<const Environment*>(object);
        if (environment->kind == EnvironmentKind::Root || environment->kind == EnvironmentKind::Module) {
            return;
        }
    }
    if ((object->header.flags & GcMinorRememberedBit) != 0) {
        return;
    }
    object->header.flags |= GcMinorRememberedBit;
    remembered_objects_.push_back(object);
}

void Runtime::rebuild_environment_cards(Environment* environment) {
    if (environment == nullptr || environment->kind != EnvironmentKind::Local) {
        if (environment != nullptr) {
            environment->remembered_cards.clear();
        }
        return;
    }
    // Phase 3.3: bitmap rebuild — set one bit per card that has a young reference.
    const std::size_t env_cards = card_count_for_elements(environment->binding_names.size());
    environment->remembered_cards.assign(value_card_count(environment->binding_names.size()), 0);
    for (std::size_t card_index = 0; card_index < env_cards; ++card_index) {
        if (environment_card_has_young_reference(environment, card_index)) {
            environment->remembered_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    }
}

void Runtime::rebuild_array_cards(ArrayObject* array) {
    if (array == nullptr) {
        return;
    }
    const std::size_t arr_cards = card_count_for_elements(array->elements.size());
    array->remembered_cards.assign(value_card_count(array->elements.size()), 0);
    for (std::size_t card_index = 0; card_index < arr_cards; ++card_index) {
        if (value_card_has_young_reference(array->elements, card_index)) {
            array->remembered_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    }
}

void Runtime::rebuild_struct_cards(StructInstanceObject* instance) {
    if (instance == nullptr) {
        return;
    }
    const std::size_t str_cards = card_count_for_elements(instance->field_values.size());
    instance->remembered_cards.assign(value_card_count(instance->field_values.size()), 0);
    for (std::size_t card_index = 0; card_index < str_cards; ++card_index) {
        if (struct_card_has_young_reference(instance, card_index)) {
            instance->remembered_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    }
}

void Runtime::rebuild_enum_cards(EnumInstanceObject* instance) {
    if (instance == nullptr) {
        return;
    }
    const std::size_t enum_cards = card_count_for_elements(instance->payload.size());
    instance->remembered_cards.assign(value_card_count(instance->payload.size()), 0);
    for (std::size_t card_index = 0; card_index < enum_cards; ++card_index) {
        if (value_card_has_young_reference(instance->payload, card_index)) {
            instance->remembered_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
        }
    }
}

void Runtime::rebuild_coroutine_cards(CoroutineObject* coroutine) {
    if (coroutine == nullptr) {
        return;
    }
    for (auto& frame : coroutine->frames) {
        const std::size_t stk_cards = card_count_for_elements(frame.stack.size());
        frame.stack_cards.assign(value_card_count(frame.stack.size()), 0);
        for (std::size_t card_index = 0; card_index < stk_cards; ++card_index) {
            if (value_card_has_young_reference(frame.stack, card_index)) {
                frame.stack_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
            }
        }
        const std::size_t loc_cards = card_count_for_elements(frame.locals.size());
        frame.local_cards.assign(value_card_count(frame.locals.size()), 0);
        for (std::size_t card_index = 0; card_index < loc_cards; ++card_index) {
            if (value_card_has_young_reference(frame.locals, card_index)) {
                frame.local_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
            }
        }
        if (!frame.regs.empty()) {
            const std::size_t reg_cards = card_count_for_elements(frame.regs.size());
            frame.reg_cards.assign(value_card_count(frame.regs.size()), 0);
            for (std::size_t card_index = 0; card_index < reg_cards; ++card_index) {
                if (value_card_has_young_reference(frame.regs, card_index)) {
                    frame.reg_cards[card_index / kGcCardsPerWord] |= (std::uint64_t(1) << (card_index % kGcCardsPerWord));
                }
            }
        }
    }
}

void Runtime::note_environment_binding_write(Environment* environment, std::size_t binding_index, const Value& value) {
    if (environment != nullptr && environment->kind == EnvironmentKind::Local &&
        is_old_object(environment) && binding_index != static_cast<std::size_t>(-1)) {
        set_remembered_card(environment->remembered_cards, environment->binding_names.size(), binding_index);
    }
    note_write(environment, value);
}

void Runtime::note_array_element_write(ArrayObject* array, std::size_t index, const Value& value) {
    if (array != nullptr && is_old_object(array)) {
        set_remembered_card(array->remembered_cards, array->elements.size(), index);
    }
    note_write(array, value);
}

void Runtime::note_struct_field_write(StructInstanceObject* instance, std::size_t field_index, const Value& value) {
    if (instance != nullptr && is_old_object(instance) && field_index != static_cast<std::size_t>(-1)) {
        set_remembered_card(instance->remembered_cards, instance->field_values.size(), field_index);
    }
    note_write(instance, value);
}

void Runtime::note_enum_payload_write(EnumInstanceObject* instance, std::size_t index, const Value& value) {
    if (instance != nullptr && is_old_object(instance)) {
        set_remembered_card(instance->remembered_cards, instance->payload.size(), index);
    }
    note_write(instance, value);
}

std::size_t Runtime::count_remembered_cards() const {
    std::size_t count = 0;
    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            if (object->kind == ObjectKind::Environment) {
                const auto* environment = static_cast<const Environment*>(object);
                for (auto w : environment->remembered_cards) count += static_cast<std::size_t>(std::popcount(w));
                return;
            }
            if (object->kind == ObjectKind::Array) {
                const auto* array = static_cast<const ArrayObject*>(object);
                for (auto w : array->remembered_cards) count += static_cast<std::size_t>(std::popcount(w));
                return;
            }
            if (object->kind == ObjectKind::StructInstance) {
                const auto* instance = static_cast<const StructInstanceObject*>(object);
                for (auto w : instance->remembered_cards) count += static_cast<std::size_t>(std::popcount(w));
                return;
            }
            if (object->kind == ObjectKind::EnumInstance) {
                const auto* instance = static_cast<const EnumInstanceObject*>(object);
                for (auto w : instance->remembered_cards) count += static_cast<std::size_t>(std::popcount(w));
                return;
            }
            if (object->kind != ObjectKind::Coroutine) {
                return;
            }
            const auto* coroutine = static_cast<const CoroutineObject*>(object);
            for (const auto& frame : coroutine->frames) {
                for (auto w : frame.stack_cards) count += static_cast<std::size_t>(std::popcount(w));
                for (auto w : frame.local_cards) count += static_cast<std::size_t>(std::popcount(w));
                for (auto w : frame.reg_cards) count += static_cast<std::size_t>(std::popcount(w));
            }
        });
    }
    return count;
}

void Runtime::compact_minor_remembered_set() {
    std::size_t write_index = 0;
    for (std::size_t read_index = 0; read_index < remembered_objects_.size(); ++read_index) {
        GcObject* object = remembered_objects_[read_index];
        bool keep = false;
        if (object != nullptr && is_old_object(object)) {
            if (owner_is_fully_card_tracked(object)) {
                keep = owner_has_dirty_minor_cards(object);
            } else {
                keep = has_direct_young_reference(object);
            }
        }
        if (!keep) {
            if (object != nullptr && is_old_object(object) && owner_is_fully_card_tracked(object)) {
                ++total_remembered_card_fast_prunes_;
            }
            if (object != nullptr) {
                object->header.flags &= static_cast<std::uint8_t>(~GcMinorRememberedBit);
            }
            continue;
        }
        remembered_objects_[write_index++] = object;
    }
    remembered_objects_.resize(write_index);
}

void Runtime::rebuild_minor_remembered_set() {
    remembered_objects_.clear();
    // Pass 1: clear the minor-remembered bit on every live object.
    for (auto* space : all_spaces_) {
        space->for_each_object([](GcObject* object) {
            object->header.flags &= static_cast<std::uint8_t>(~GcMinorRememberedBit);
        });
    }
    // Pass 2: rebuild cards for old objects and repopulate remembered_objects_.
    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            if (!is_old_object(object)) {
                return;
            }
            if (object->kind == ObjectKind::Environment) {
                rebuild_environment_cards(static_cast<Environment*>(object));
            } else if (object->kind == ObjectKind::Array) {
                rebuild_array_cards(static_cast<ArrayObject*>(object));
            } else if (object->kind == ObjectKind::StructInstance) {
                rebuild_struct_cards(static_cast<StructInstanceObject*>(object));
            } else if (object->kind == ObjectKind::EnumInstance) {
                rebuild_enum_cards(static_cast<EnumInstanceObject*>(object));
            } else if (object->kind == ObjectKind::Coroutine) {
                rebuild_coroutine_cards(static_cast<CoroutineObject*>(object));
            }
            if (object->kind == ObjectKind::Environment) {
                const auto* environment = static_cast<const Environment*>(object);
                if (environment->kind == EnvironmentKind::Root || environment->kind == EnvironmentKind::Module) {
                    return;
                }
            }
            const bool keep = owner_is_fully_card_tracked(object) ? owner_has_dirty_minor_cards(object) : has_direct_young_reference(object);
            if (!keep) {
                return;
            }
            object->header.flags |= GcMinorRememberedBit;
            remembered_objects_.push_back(object);
        });
    }
}

void Runtime::visit_object_references(const GcObject* object, const std::function<void(GcObject*)>& object_visitor,
                                      const std::function<void(const Value&)>& value_visitor) const {
    if (object == nullptr) {
        return;
    }
    switch (object->kind) {
        case ObjectKind::String:
        case ObjectKind::NativeFunction:
        case ObjectKind::StructType:
        case ObjectKind::EnumType:
            return;
        case ObjectKind::Environment: {
            const auto* environment = static_cast<const Environment*>(object);
            object_visitor(environment->parent);
            for (const auto& entry : environment->values) {
                if (entry.second.cell != nullptr) {
                    object_visitor(entry.second.cell);
                } else {
                    value_visitor(entry.second.value);
                }
            }
            return;
        }
        case ObjectKind::UpvalueCell: {
            const auto* cell = static_cast<const UpvalueCellObject*>(object);
            value_visitor(cell->value);
            return;
        }
        case ObjectKind::Array: {
            const auto* array = static_cast<const ArrayObject*>(object);
            for (const auto& element : array->elements) {
                value_visitor(element);
            }
            return;
        }
        case ObjectKind::ScriptFunction: {
            const auto* function = static_cast<const ScriptFunctionObject*>(object);
            object_visitor(function->closure);
            for (auto* cell : function->captured_upvalues) {
                object_visitor(cell);
            }
            return;
        }
        case ObjectKind::StructInstance: {
            const auto* instance = static_cast<const StructInstanceObject*>(object);
            object_visitor(instance->type);
            for (const auto& value : instance->field_values) {
                value_visitor(value);
            }
            return;
        }
        case ObjectKind::EnumInstance: {
            const auto* instance = static_cast<const EnumInstanceObject*>(object);
            object_visitor(instance->type);
            for (const auto& value : instance->payload) {
                value_visitor(value);
            }
            return;
        }
        case ObjectKind::Coroutine: {
            const auto* coroutine = static_cast<const CoroutineObject*>(object);
            for (const auto& frame : coroutine->frames) {
                object_visitor(frame.closure);
                object_visitor(frame.root_env);
                object_visitor(frame.current_env);
                for (auto* cell : frame.captured_upvalues) {
                    object_visitor(cell);
                }
                for (auto* scope : frame.scope_stack) {
                    object_visitor(scope);
                }
                for (const auto& value : frame.stack) {
                    value_visitor(value);
                }
                for (const auto& value : frame.locals) {
                    value_visitor(value);
                }
            }
            return;
        }
        case ObjectKind::ModuleNamespace: {
            const auto* namespace_object = static_cast<const ModuleNamespaceObject*>(object);
            object_visitor(namespace_object->environment);
            return;
        }
    }
}

void Runtime::collect_garbage() {
    const bool had_in_flight_cycle = gc_phase_ != ZephyrGcPhase::Idle || gc_cycle_requested_;
    auto drain_to_idle = [&]() {
        request_gc_cycle();
        std::size_t guard = 0;
        while (gc_phase_ != ZephyrGcPhase::Idle || gc_cycle_requested_) {
            gc_step(std::numeric_limits<std::size_t>::max() / 4);
            if (++guard > 1024 * 1024) {
                fail("GC failed to converge.");
            }
        }
    };

    drain_to_idle();

    // If we started in the middle of an incremental cycle, finish that cycle first and
    // then run one fresh full cycle to clear any floating garbage retained by barriers.
    if (had_in_flight_cycle) {
        drain_to_idle();
    }
}

VoidResult Runtime::gc_verify_full() {
    collect_garbage();
    ++total_gc_verifications_;

    std::vector<std::string> errors;
    constexpr std::size_t kMaxErrors = 16;
    auto push_error = [&](std::string message) {
        if (errors.size() < kMaxErrors) {
            errors.push_back(std::move(message));
        }
    };

    if (gc_phase_ != ZephyrGcPhase::Idle) {
        push_error("GC verification expected Idle phase after full collection.");
    }
    if (gc_cycle_requested_) {
        push_error("GC verification found a pending collection request after full collection.");
    }
    if (!gray_stack_.empty()) {
        push_error("GC verification found non-empty gray stack after full collection.");
    }
    if (!dirty_root_environments_.empty()) {
        push_error("GC verification found dirty root environments after full collection.");
    }
    if (!dirty_objects_.empty()) {
        push_error("GC verification found dirty remembered objects after full collection.");
    }
    if (!detach_queue_.empty()) {
        push_error("GC verification found pending detach queue entries after full collection.");
    }
    if (!active_environments_.empty()) {
        push_error("GC verification found active environments registered while the runtime is idle.");
    }
    if (!rooted_value_vectors_.empty()) {
        push_error("GC verification found rooted value vectors registered while the runtime is idle.");
    }
    if (!rooted_values_.empty()) {
        push_error("GC verification found rooted scalar values registered while the runtime is idle.");
    }

    std::unordered_set<GcObject*> live_objects;
    std::size_t total_bytes = 0;
    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            if (!live_objects.insert(object).second) {
                push_error("GC verification found a duplicate object in the heap list.");
                return;
            }
            total_bytes += object->header.size_bytes;
            if (object->header.size_bytes == 0) {
                push_error("GC verification found a zero-sized heap object.");
            }
            if (object->header.type_id != static_cast<std::uint16_t>(object->kind)) {
                push_error("GC verification found a heap object with mismatched kind/type id.");
            }
            if (object->header.color != GcColor::White) {
                push_error(std::string("GC verification found a non-white object after full collection: ") +
                           object_kind_name(object->kind) + " is " + gc_color_name(object->header.color) + ".");
            }
            if (object->header.next_gray != nullptr) {
                push_error(std::string("GC verification found a non-null next_gray at Idle: ") +
                           object_kind_name(object->kind) + " (gray stack should be empty).");
            }
            if ((object->header.flags & GcDirtyQueuedBit) != 0) {
                push_error(std::string("GC verification found a dirty queued object after full collection: ") +
                           object_kind_name(object->kind) + ".");
            }
            // Phase 1A: space_kind must mirror GcOldBit.
            // Phase 2B will permit LargeObject; Phase 3B EnvArena; Phase 3C CoroArena.
            {
                const bool is_old = (object->header.flags & GcOldBit) != 0;
                const bool space_is_old = object->header.space_kind != GcSpaceKind::Nursery;
                if (is_old != space_is_old) {
                    push_error(std::string("GC verification found space_kind/GcOldBit mismatch on: ") +
                               object_kind_name(object->kind) + ".");
                }
            }
        });
    }

    if (total_bytes != live_bytes_) {
        push_error("GC verification found live byte accounting drift.");
    }

    auto ensure_heap_object = [&](GcObject* object, const std::string& owner) {
        if (object == nullptr) {
            return;
        }
        if (!live_objects.contains(object)) {
            push_error(owner + " points to an object that is not in the live heap list.");
        }
    };

    auto verify_pointer_registry = [&](const auto& registry, const std::string& name, ObjectKind expected_kind) {
        std::unordered_set<GcObject*> seen;
        for (auto* object : registry) {
            if (object == nullptr) {
                push_error(name + " contains a null entry.");
                continue;
            }
            if (!seen.insert(object).second) {
                push_error(name + " contains a duplicate entry.");
            }
            ensure_heap_object(object, name);
            if (object->kind != expected_kind) {
                push_error(name + " contains an object with the wrong kind.");
            }
        }
    };

    verify_pointer_registry(active_coroutines_, "active coroutine registry", ObjectKind::Coroutine);
    verify_pointer_registry(suspended_coroutines_, "suspended coroutine registry", ObjectKind::Coroutine);
    verify_pointer_registry(native_callback_registry_, "native callback registry", ObjectKind::NativeFunction);

    for (const auto& entry : interned_strings_) {
        if (entry.second == nullptr) {
            push_error("Interned string table contains a null entry.");
            continue;
        }
        ensure_heap_object(entry.second, "interned string table");
        if (entry.second->kind != ObjectKind::String) {
            push_error("Interned string table contains a non-string object.");
        }
    }
    for (auto* object : pinned_debug_objects_) {
        ensure_heap_object(object, "pinned debug object registry");
    }
    for (const auto& entry : modules_) {
        ensure_heap_object(entry.second.environment, "module environment");
        ensure_heap_object(entry.second.namespace_object, "module namespace");
    }

    for (std::uint32_t index = 0; index < host_handles_.size(); ++index) {
        const HostHandleEntry& entry = host_handles_[index];
        if (entry.runtime_slot != index) {
            push_error("Host handle table contains an entry with mismatched runtime slot.");
        }
        if (entry.lifetime == ZephyrHostHandleLifetime::Stable) {
            if (!entry.policy.allow_serialize || !entry.policy.allow_cross_scene) {
                push_error("Stable host handle entry lost serialization/cross-scene policy bits.");
            }
        }
        if ((entry.flags & HostHandleStrongResidencyBit) != 0) {
            if (!entry.policy.strong_residency_allowed) {
                push_error("Host handle entry uses strong residency without policy support.");
            }
            if (!entry.residency_owner) {
                push_error("Strong residency host handle entry is missing a residency owner.");
            }
        }
        if (entry.invalid() && entry.residency_owner) {
            push_error("Invalid host handle entry still keeps a residency owner.");
        }
        if (entry.stable_guid.valid()) {
            const auto it = stable_handle_lookup_.find(entry.stable_guid);
            if (it == stable_handle_lookup_.end() || it->second != index) {
                push_error("Stable host handle lookup table is out of sync with host handle entries.");
            }
        }
    }
    for (const auto& [guid, slot] : stable_handle_lookup_) {
        if (slot >= host_handles_.size()) {
            push_error("Stable host handle lookup points past the end of the handle table.");
            continue;
        }
        if (!(host_handles_[slot].stable_guid == guid)) {
            push_error("Stable host handle lookup points at an entry with a mismatched GUID.");
        }
    }

    std::unordered_set<GcObject*> seen_remembered;
    for (auto* object : remembered_objects_) {
        if (object == nullptr) {
            push_error("Minor remembered set contains a null entry.");
            continue;
        }
        if (!seen_remembered.insert(object).second) {
            push_error("Minor remembered set contains a duplicate entry.");
        }
        ensure_heap_object(object, "minor remembered set");
        if (!is_old_object(object)) {
            push_error("Minor remembered set contains a non-old object.");
        }
        if ((object->header.flags & GcMinorRememberedBit) == 0) {
            push_error("Minor remembered set entry is missing its remembered flag.");
        }
        if (!has_direct_young_reference(object)) {
            push_error("Minor remembered set contains a stale owner without a young reference.");
        }
    }
    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            if ((object->header.flags & GcMinorRememberedBit) != 0 && !seen_remembered.contains(object)) {
                push_error("Heap object is marked as minor-remembered but is absent from the remembered set.");
            }
        });
    }

    std::unordered_set<GcObject*> reachable;
    std::vector<GcObject*> worklist;
    auto enqueue_object = [&](GcObject* object, const std::string& owner) {
        if (object == nullptr) {
            return;
        }
        if (!live_objects.contains(object)) {
            push_error(owner + " points to an object that is not in the live heap list.");
            return;
        }
        if (reachable.insert(object).second) {
            worklist.push_back(object);
        }
    };
    auto enqueue_value = [&](const Value& value, const std::string& owner) {
        if (value.is_object()) {
            enqueue_object(value.as_object(), owner);
        }
    };

    visit_root_references(
        [&](GcObject* object) { enqueue_object(object, "GC root"); },
        [&](const Value& value) { enqueue_value(value, "GC root value"); });

    while (!worklist.empty()) {
        GcObject* current = worklist.back();
        worklist.pop_back();
        visit_object_references(
            current,
            [&](GcObject* object) { enqueue_object(object, std::string("Heap object '") + object_kind_name(current->kind) + "'"); },
            [&](const Value& value) { enqueue_value(value, std::string("Heap object '") + object_kind_name(current->kind) + "'"); });
    }

    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            if (!reachable.contains(object)) {
                push_error(std::string("GC verification found an unreachable live object after full collection: ") +
                           object_kind_name(object->kind) + ".");
            }
        });
    }

    // ── Per-space structural verify ───────────────────────────────────────────
    {
        const std::string los_err = los_.verify();
        if (!los_err.empty()) {
            push_error(los_err);
        }
    }
    {
        const std::string pinned_err = pinned_.verify();
        if (!pinned_err.empty()) {
            push_error(pinned_err);
        }
    }
    {
        const std::string oss_err = old_small_.verify();
        if (!oss_err.empty()) {
            push_error(oss_err);
        }
    }
    {
        const std::string nursery_err = nursery_.verify();
        if (!nursery_err.empty()) {
            push_error(nursery_err);
        }
    }

    // ── Per-space live_bytes cross-check ─────────────────────────────────────
    // More informative than the global total_bytes check: identifies which space
    // has drifted when accounting_consistent() fails.
    {
        const std::size_t n_b   = nursery_.live_bytes();
        const std::size_t os_b  = old_small_.live_bytes();
        const std::size_t los_b = los_.live_bytes();
        const std::size_t pin_b = pinned_.live_bytes();
        const std::size_t sum   = n_b + os_b + los_b + pin_b;
        if (sum != live_bytes_) {
            push_error("Per-space live_bytes sum (" + std::to_string(sum) +
                       ") != global live_bytes_ (" + std::to_string(live_bytes_) + ")."
                       " nursery=" + std::to_string(n_b) +
                       " old_small=" + std::to_string(os_b) +
                       " los=" + std::to_string(los_b) +
                       " pinned=" + std::to_string(pin_b) + ".");
        }
    }

    // ── Space-kind vs actual space membership ────────────────────────────────
    // Every object's header.space_kind must match the space that owns it.
    // (Each space's verify() checks this too, but that only covers objects in
    // their own list; this cross-check detects objects that ended up in the
    // wrong space's iteration with a mismatched space_kind.)
    {
        auto check_membership = [&](HeapSpace* space, GcSpaceKind expected, const char* name) {
            space->for_each_object([&](GcObject* obj) {
                if (obj->header.space_kind != expected) {
                    push_error(std::string(name) + ": object has wrong space_kind "
                               "(expected " + std::to_string(static_cast<int>(expected)) +
                               ", got "     + std::to_string(static_cast<int>(obj->header.space_kind)) + ").");
                }
            });
        };
        check_membership(&nursery_,   GcSpaceKind::Nursery,     "NurserySpace membership check");
        check_membership(&old_small_, GcSpaceKind::OldSmall,    "OldSmallSpace membership check");
        check_membership(&los_,       GcSpaceKind::LargeObject, "LargeObjectSpace membership check");
        check_membership(&pinned_,    GcSpaceKind::Pinned,      "PinnedSpace membership check");
    }

    if (!errors.empty()) {
        return make_error<std::monostate>("GC verification failed:\n" + join_strings(errors, "\n"));
    }
    return ok_result();
}

void Runtime::record_gc_pause(std::uint64_t duration_ns, bool is_full) {
    pause_records_.push_back(GCPauseRecord{duration_ns, is_full});
    while (pause_records_.size() > 10000) {
        pause_records_.pop_front();
    }
    if (duration_ns > 16ULL * 1000ULL * 1000ULL) {
        ++frame_budget_miss_count_;
    }
}

std::uint64_t Runtime::gc_pause_percentile(int pct) const {
    if (pause_records_.empty()) {
        return 0;
    }

    const int clamped_pct = std::clamp(pct, 0, 100);
    std::vector<std::uint64_t> durations;
    durations.reserve(pause_records_.size());
    for (const auto& record : pause_records_) {
        durations.push_back(record.duration_ns);
    }
    std::sort(durations.begin(), durations.end());
    const std::size_t rank = std::max<std::size_t>(
        1, static_cast<std::size_t>((durations.size() * static_cast<std::size_t>(clamped_pct) + 99) / 100));
    return durations[std::min(durations.size() - 1, rank - 1)];
}

GCPauseStats Runtime::get_gc_pause_stats() const {
    GCPauseStats stats;
    stats.p50_ns = gc_pause_percentile(50);
    stats.p95_ns = gc_pause_percentile(95);
    stats.p99_ns = gc_pause_percentile(99);
    stats.frame_budget_miss_count = frame_budget_miss_count_;
    return stats;
}

std::uint64_t Runtime::current_runtime_timestamp_ns() const {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(ProfileClock::now() - runtime_start_time_).count());
}

namespace {

TypeRef cached_type_ref_from_name(const std::string& display_name, std::uint32_t span_line) {
    TypeRef type;
    type.span = Span{std::max<std::size_t>(1, static_cast<std::size_t>(span_line)), 1};
    if (display_name.empty()) {
        return type;
    }

    std::size_t start = 0;
    while (start <= display_name.size()) {
        const std::size_t next = display_name.find("::", start);
        if (next == std::string::npos) {
            type.parts.push_back(display_name.substr(start));
            break;
        }
        type.parts.push_back(display_name.substr(start, next - start));
        start = next + 2;
    }
    return type;
}

std::optional<TypeRef> cached_return_type_from_metadata(const InstructionMetadata& metadata, std::uint32_t span_line) {
    if (!metadata.type_name.has_value() || metadata.type_name->empty()) {
        return std::nullopt;
    }
    return cached_type_ref_from_name(*metadata.type_name, span_line);
}

std::vector<Param> cached_params_from_metadata(const InstructionMetadata& metadata, std::uint32_t span_line) {
    std::vector<Param> params;
    params.reserve((metadata.names.size() + 1) / 2);
    for (std::size_t index = 0; index < metadata.names.size(); index += 2) {
        Param param;
        param.name = metadata.names[index];
        param.span = Span{std::max<std::size_t>(1, static_cast<std::size_t>(span_line)), 1};
        if (index + 1 < metadata.names.size() && !metadata.names[index + 1].empty()) {
            param.type = cached_type_ref_from_name(metadata.names[index + 1], span_line);
        }
        params.push_back(std::move(param));
    }
    return params;
}

bool bytecode_cache_roundtrip_supported(const BytecodeFunction* bytecode) {
    if (bytecode == nullptr) {
        return true;
    }

    const std::size_t instruction_count = std::min(bytecode->instructions.size(), bytecode->metadata.size());
    for (std::size_t index = 0; index < instruction_count; ++index) {
        const auto op = bytecode->instructions[index].op;
        const auto& metadata = bytecode->metadata[index];
        switch (op) {
            case BytecodeOp::BindPattern:
            case BytecodeOp::DeclareStruct:
            case BytecodeOp::DeclareEnum:
            case BytecodeOp::DeclareTrait:
            case BytecodeOp::DeclareImpl:
            case BytecodeOp::EvalAstExpr:
            case BytecodeOp::ExecAstStmt:
                return false;
            case BytecodeOp::DeclareFunction:
            case BytecodeOp::MakeFunction:
            case BytecodeOp::MakeCoroutine:
                if (metadata.bytecode == nullptr) {
                    return false;
                }
                break;
            default:
                break;
        }

        if (!bytecode_cache_roundtrip_supported(metadata.bytecode.get())) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::uint64_t Runtime::module_file_mtime(const std::filesystem::path& path) const {
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
}

void Runtime::record_gc_trace_event(GCTraceEvent::Type type, std::size_t heap_bytes_before, std::size_t heap_bytes_after) {
    if (!gc_trace_active_) {
        return;
    }
    gc_trace_events_.push_back(GCTraceEventRecord{type, current_runtime_timestamp_ns(), heap_bytes_before, heap_bytes_after});
    if (gc_trace_events_.size() > 50000) {
        gc_trace_events_.erase(gc_trace_events_.begin(), gc_trace_events_.begin() + (gc_trace_events_.size() - 50000));
    }
}

void Runtime::ensure_coroutine_trace_id(CoroutineObject* coroutine) {
    if (coroutine != nullptr && coroutine->trace_id == 0) {
        coroutine->trace_id = next_coroutine_trace_id_++;
    }
}

void Runtime::record_coroutine_trace_event(CoroutineTraceEvent::Type type, const CoroutineObject* coroutine) {
    if (!coroutine_trace_active_ || coroutine == nullptr || coroutine->trace_id == 0) {
        return;
    }
    coroutine_trace_events_.push_back(CoroutineTraceEvent{type, coroutine->trace_id, current_runtime_timestamp_ns()});
}

void Runtime::record_coroutine_completed(CoroutineObject* coroutine) {
    if (coroutine == nullptr || coroutine->completion_traced) {
        return;
    }
    ensure_coroutine_trace_id(coroutine);
    record_coroutine_trace_event(CoroutineTraceEvent::Type::Completed, coroutine);
    coroutine->completion_traced = true;
}

void Runtime::record_coroutine_destroyed(CoroutineObject* coroutine) {
    if (coroutine == nullptr || coroutine->destroyed_traced) {
        return;
    }
    ensure_coroutine_trace_id(coroutine);
    record_coroutine_trace_event(CoroutineTraceEvent::Type::Destroyed, coroutine);
    coroutine->destroyed_traced = true;
}

RuntimeResult<Value> Runtime::load_bytecode_constant(const BytecodeFunction& function, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= function.constants.size()) {
        return make_error<Value>("Invalid bytecode constant index.");
    }
    const auto& constant = function.constants[static_cast<std::size_t>(index)];
    return std::visit(
        [&](const auto& value) -> RuntimeResult<Value> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return Value::nil();
            } else if constexpr (std::is_same_v<T, bool>) {
                return Value::boolean(value);
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return Value::integer(value);
            } else if constexpr (std::is_same_v<T, double>) {
                return Value::floating(value);
            } else {
                return make_literal_string(value);
            }
        },
        constant);
}

RuntimeResult<Value> Runtime::apply_binary_op(TokenType op, const Value& left, const Value& right, const Span& span,
                                              const std::string& module_name) {
    if (op == TokenType::Plus) {
        if (left.is_number() && right.is_number()) {
            if (left.is_int() && right.is_int()) {
                std::int64_t result = 0;
                if (!try_add_int48(left.as_int(), right.as_int(), result)) {
                    return make_loc_error<Value>(module_name, span, kInt48RangeErrorMessage);
                }
                return Value::integer(result);
            }
            return Value::floating(left.as_float() + right.as_float());
        }
        if (left.is_object() && right.is_object() &&
            left.as_object() != nullptr && right.as_object() != nullptr &&
            left.as_object()->kind == ObjectKind::String &&
            right.as_object()->kind == ObjectKind::String) {
            const auto& ls = static_cast<const StringObject*>(left.as_object())->value;
            const auto& rs = static_cast<const StringObject*>(right.as_object())->value;
            std::string result;
            result.reserve(ls.size() + rs.size());
            result.append(ls);
            result.append(rs);
            return make_string(std::move(result));
        }
        return make_string(value_to_string(left) + value_to_string(right));
    }
    if (op == TokenType::Minus) {
        if (left.is_int() && right.is_int()) {
            std::int64_t result = 0;
            if (!try_sub_int48(left.as_int(), right.as_int(), result)) {
                return make_loc_error<Value>(module_name, span, kInt48RangeErrorMessage);
            }
            return Value::integer(result);
        }
        ZEPHYR_TRY_ASSIGN(left_number, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_number, numeric_value(right, module_name, span));
        return Value::floating(left_number - right_number);
    }
    if (op == TokenType::Star) {
        if (left.is_int() && right.is_int()) {
            std::int64_t result = 0;
            if (!try_mul_int48(left.as_int(), right.as_int(), result)) {
                return make_loc_error<Value>(module_name, span, kInt48RangeErrorMessage);
            }
            return Value::integer(result);
        }
        ZEPHYR_TRY_ASSIGN(left_mul, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_mul, numeric_value(right, module_name, span));
        return Value::floating(left_mul * right_mul);
    }
    if (op == TokenType::Slash) {
        ZEPHYR_TRY_ASSIGN(left_div, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_div, numeric_value(right, module_name, span));
        return Value::floating(left_div / right_div);
    }
    if (op == TokenType::Percent) {
        if (!left.is_int() || !right.is_int()) {
            return make_loc_error<Value>(module_name, span, "Modulo expects integer operands.");
        }
        return Value::integer(left.as_int() % right.as_int());
    }
    if (op == TokenType::EqualEqual) {
        return Value::boolean(values_equal(left, right));
    }
    if (op == TokenType::BangEqual) {
        return Value::boolean(!values_equal(left, right));
    }
    if (op == TokenType::Less) {
        ZEPHYR_TRY_ASSIGN(left_lt, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_lt, numeric_value(right, module_name, span));
        return Value::boolean(left_lt < right_lt);
    }
    if (op == TokenType::LessEqual) {
        ZEPHYR_TRY_ASSIGN(left_le, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_le, numeric_value(right, module_name, span));
        return Value::boolean(left_le <= right_le);
    }
    if (op == TokenType::Greater) {
        ZEPHYR_TRY_ASSIGN(left_gt, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_gt, numeric_value(right, module_name, span));
        return Value::boolean(left_gt > right_gt);
    }
    if (op == TokenType::GreaterEqual) {
        ZEPHYR_TRY_ASSIGN(left_ge, numeric_value(left, module_name, span));
        ZEPHYR_TRY_ASSIGN(right_ge, numeric_value(right, module_name, span));
        return Value::boolean(left_ge >= right_ge);
    }
    return make_loc_error<Value>(module_name, span, "Unsupported binary operator.");
}

RuntimeResult<Value> Runtime::apply_unary_op(TokenType op, const Value& right, const Span& span, const std::string& module_name) {
    if (op == TokenType::Bang) {
        return Value::boolean(!is_truthy(right));
    }
    if (op == TokenType::Minus) {
        if (right.is_int()) {
            if (right.as_int() == Value::kIntMin) {
                return make_loc_error<Value>(module_name, span, kInt48RangeErrorMessage);
            }
            return Value::integer(-right.as_int());
        }
        ZEPHYR_TRY_ASSIGN(number, numeric_value(right, module_name, span));
        return Value::floating(-number);
    }
    if (op == TokenType::Question) {
        if (!right.is_object() || right.as_object()->kind != ObjectKind::EnumInstance) {
            return make_loc_error<Value>(module_name, span, "'?' expects Result-like enum value.");
        }
        auto* enum_value = static_cast<EnumInstanceObject*>(right.as_object());
        if (enum_value->variant == "Ok") {
            if (enum_value->payload.empty()) {
                return Value::nil();
            }
            return enum_value->payload.front();
        }
        if (enum_value->variant == "Err") {
            const Value payload = enum_value->payload.empty() ? Value::nil() : enum_value->payload.front();
            return make_loc_error<Value>(module_name, span, "'?' encountered Err(" + value_to_string(payload) + ").");
        }
        return make_loc_error<Value>(module_name, span, "'?' expects enum variants Ok/Err.");
    }
    return make_loc_error<Value>(module_name, span, "Unsupported unary operator.");
}

TypeRef Runtime::parse_type_name(const std::string& text, const Span& span) const {
    TypeRef type;
    type.span = span;

    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find("::", start);
        if (end == std::string::npos) {
            type.parts.push_back(text.substr(start));
            break;
        }
        type.parts.push_back(text.substr(start, end - start));
        start = end + 2;
    }

    if (type.parts.empty()) {
        type.parts.push_back(text);
    }
    return type;
}

RuntimeResult<Value> Runtime::build_struct_value(Environment* environment, const std::string& type_name, const std::vector<std::string>& field_names,
                                                const std::vector<Value>& field_values, const Span& span, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(type, expect_struct_type(environment, parse_type_name(type_name, span), module_name, span));
    auto* instance = allocate<StructInstanceObject>(type);
    initialize_struct_instance(instance);

    for (std::size_t i = 0; i < field_names.size(); ++i) {
        const std::size_t field_index = instance->field_slot(field_names[i]);
        if (field_index == static_cast<std::size_t>(-1) || field_index >= type->fields.size()) {
            return make_loc_error<Value>(module_name, span, "Unknown struct field '" + field_names[i] + "'.");
        }
        const auto& spec = type->fields[field_index];
        std::optional<std::string> field_type = (std::find(type->generic_params.begin(), type->generic_params.end(), spec.type_name) != type->generic_params.end())
            ? std::optional<std::string>(std::nullopt) : std::optional<std::string>(spec.type_name);
        ZEPHYR_TRY(enforce_type(field_values[i], field_type, span, module_name, "struct field"));
        ZEPHYR_TRY(validate_handle_store(field_values[i], HandleContainerKind::HeapField, span, module_name, "struct field initialization"));
        instance->field_values[field_index] = field_values[i];
        note_struct_field_write(instance, field_index, field_values[i]);
    }

    for (std::size_t field_index = 0; field_index < type->fields.size(); ++field_index) {
        const auto& field_spec = type->fields[field_index];
        if (instance->field_values[field_index].is_nil() && field_spec.type_name != "Nil" && field_spec.type_name != "any") {
            return make_loc_error<Value>(module_name, span, "Missing required field '" + field_spec.name + "' in struct literal.");
        }
    }

    return Value::object(instance);
}

RuntimeResult<Value> Runtime::build_enum_value(Environment* environment, const std::string& enum_name, const std::string& variant_name,
                                              const std::vector<Value>& payload, const Span& span, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(type, expect_enum_type(environment, parse_type_name(enum_name, span), module_name, span));
    const auto variant_it = std::find_if(type->variants.begin(), type->variants.end(),
                                         [&](const EnumVariantSpec& variant) { return variant.name == variant_name; });
    if (variant_it == type->variants.end()) {
        return make_loc_error<Value>(module_name, span, "Unknown enum variant '" + variant_name + "'.");
    }
    if (variant_it->payload_types.size() != payload.size()) {
        return make_loc_error<Value>(module_name, span, "Wrong payload count for enum variant '" + variant_name + "'.");
    }

    auto* instance = allocate<EnumInstanceObject>(type, variant_name);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        ZEPHYR_TRY(enforce_type(payload[i], variant_it->payload_types[i], span, module_name, "enum payload"));
        ZEPHYR_TRY(validate_handle_store(payload[i], HandleContainerKind::HeapField, span, module_name, "enum payload initialization"));
        instance->payload.push_back(payload[i]);
        note_enum_payload_write(instance, instance->payload.size() - 1, payload[i]);
    }
    return Value::object(instance);
}

RuntimeResult<Value> Runtime::get_enum_payload_value(const Value& value, int index, const Span& span, const std::string& module_name) {
    if (!value.is_object() || value.as_object()->kind != ObjectKind::EnumInstance) {
        return make_loc_error<Value>(module_name, span, "Expected enum instance.");
    }
    auto* instance = static_cast<EnumInstanceObject*>(value.as_object());
    if (index < 0 || static_cast<std::size_t>(index) >= instance->payload.size()) {
        return make_loc_error<Value>(module_name, span, "Enum payload index out of range.");
    }
    return instance->payload[static_cast<std::size_t>(index)];
}

RuntimeResult<bool> Runtime::is_enum_variant_value(const Value& value, const std::string& enum_name, const std::string& variant_name,
                                                   int payload_count, const Span& span, const std::string& module_name) {
    (void)module_name;
    if (!value.is_object() || value.as_object()->kind != ObjectKind::EnumInstance) {
        return false;
    }
    auto* instance = static_cast<EnumInstanceObject*>(value.as_object());
    if (!enum_name.empty()) {
        const std::string expected_name = parse_type_name(enum_name, span).parts.back();
        if (instance->type->name != expected_name) {
            return false;
        }
    }
    if (instance->variant != variant_name) {
        return false;
    }
    if (payload_count >= 0 && static_cast<int>(instance->payload.size()) != payload_count) {
        return false;
    }
    return true;
}

RuntimeResult<Value> Runtime::load_member_value(const Value& object,
                                                const CompactInstruction& instruction,
                                                const InstructionMetadata& metadata,
                                                const std::string& module_name) {
    const Span span = instruction_span(instruction);
    if (object.is_object() && object.as_object()->kind == ObjectKind::StructInstance) {
        auto* instance = static_cast<StructInstanceObject*>(object.as_object());
        if (instance->shape != nullptr && instruction.ic_shape == instance->shape &&
            instruction.ic_slot < instance->field_values.size()) {
            return instance->field_values[instruction.ic_slot];
        }

        if (instance->shape != nullptr) {
            const auto it = instance->shape->field_indices.find(metadata.string_operand);
            if (it != instance->shape->field_indices.end() && it->second < instance->field_values.size()) {
                instruction.ic_shape = instance->shape;
                instruction.ic_slot = it->second;
                return instance->field_values[it->second];
            }
        }
    }

    return get_member_value(object, metadata.string_operand, span, module_name);
}

RuntimeResult<Value> Runtime::get_member_value(const Value& object, const std::string& member, const Span& span,
                                               const std::string& module_name) {
    ScopedVectorItem<const Value*> object_root(rooted_values_, &object);
    if (object.is_host_handle()) {
        ZEPHYR_TRY_ASSIGN(resolution, resolve_host_handle(object, span, module_name, "member access"));
        try {
            if (const auto* getter = resolution.entry->host_class->find_getter(member)) {
                return from_public_value((*getter)(resolution.instance));
            }
            return from_public_value(resolution.entry->host_class->get(resolution.instance, member));
        } catch (const std::exception& error) {
            return make_loc_error<Value>(module_name, span, error.what());
        }
    }
    if (!object.is_object()) {
        return make_loc_error<Value>(module_name, span, "Member access requires object receiver.");
    }
    GcObject* raw = object.as_object();
    switch (raw->kind) {
        case ObjectKind::StructInstance: {
            auto* instance = static_cast<StructInstanceObject*>(raw);
            const std::size_t slot = instance->field_slot(member);
            if (slot == static_cast<std::size_t>(-1)) {
                return make_loc_error<Value>(module_name, span, "Unknown struct field '" + member + "'.");
            }
            return instance->field_values[slot];
        }
        case ObjectKind::ModuleNamespace: {
            auto* module = static_cast<ModuleNamespaceObject*>(raw);
            if (std::find(module->exports.begin(), module->exports.end(), member) == module->exports.end()) {
                return make_loc_error<Value>(module_name, span, "Module does not export '" + member + "'.");
            }
            Binding* binding = lookup_binding(module->environment, member);
            if (binding == nullptr) {
                return make_loc_error<Value>(module_name, span, "Module export '" + member + "' is missing.");
            }
            return read_binding_value(*binding);
        }
        case ObjectKind::Coroutine: {
            auto* coroutine = static_cast<CoroutineObject*>(raw);
            if (member == "done") {
                return Value::boolean(coroutine->completed);
            }
            if (member == "suspended") {
                return Value::boolean(coroutine->suspended && !coroutine->completed);
            }
            return make_loc_error<Value>(module_name, span, "Unknown coroutine member '" + member + "'.");
        }
        case ObjectKind::StructType: {
            auto* struct_type = static_cast<StructTypeObject*>(raw);
            const auto it = struct_type->static_methods.find(member);
            if (it != struct_type->static_methods.end()) {
                return it->second;
            }
            return make_loc_error<Value>(module_name, span,
                                         "No static method '" + member + "' on type '" + struct_type->name + "'.");
        }
        default:
            return make_loc_error<Value>(module_name, span, "Unsupported member access receiver.");
    }
}

RuntimeResult<Value> Runtime::store_member_value(const Value& object,
                                                 const Value& value,
                                                 const CompactInstruction& instruction,
                                                 const InstructionMetadata& metadata,
                                                 const std::string& module_name) {
    const Span span = instruction_span(instruction);
    ScopedVectorItem<const Value*> object_root(rooted_values_, &object);
    ScopedVectorItem<const Value*> value_root(rooted_values_, &value);
    if (object.is_object() && object.as_object()->kind == ObjectKind::StructInstance) {
        auto* instance = static_cast<StructInstanceObject*>(object.as_object());

        std::size_t slot = static_cast<std::size_t>(-1);
        if (instance->shape != nullptr && instruction.ic_shape == instance->shape &&
            instruction.ic_slot < instance->field_values.size()) {
            slot = instruction.ic_slot;
        } else if (instance->shape != nullptr) {
            const auto it = instance->shape->field_indices.find(metadata.string_operand);
            if (it != instance->shape->field_indices.end() && it->second < instance->field_values.size()) {
                instruction.ic_shape = instance->shape;
                instruction.ic_slot = it->second;
                slot = it->second;
            }
        }

        if (slot != static_cast<std::size_t>(-1)) {
            if (slot >= instance->type->fields.size()) {
                return make_loc_error<Value>(module_name, span, "Unknown struct field '" + metadata.string_operand + "'.");
            }
            ZEPHYR_TRY(enforce_type(value, instance->type->fields[slot].type_name, span, module_name, "field assignment"));
            ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::HeapField, span, module_name, "field assignment"));
            instance->field_values[slot] = value;
            note_struct_field_write(instance, slot, value);
            return value;
        }
    }

    return set_member_value(object, metadata.string_operand, value, span, module_name);
}

RuntimeResult<Value> Runtime::set_member_value(const Value& object, const std::string& member, const Value& value, const Span& span,
                                               const std::string& module_name) {
    ScopedVectorItem<const Value*> object_root(rooted_values_, &object);
    ScopedVectorItem<const Value*> value_root(rooted_values_, &value);
    if (object.is_host_handle()) {
        ZEPHYR_TRY_ASSIGN(resolution, resolve_host_handle(object, span, module_name, "member assignment"));
        try {
            const ZephyrValue public_value = to_public_value(value);
            if (const auto* setter = resolution.entry->host_class->find_setter(member)) {
                (*setter)(resolution.instance, public_value);
            } else {
                resolution.entry->host_class->set(resolution.instance, member, public_value);
            }
        } catch (const std::exception& error) {
            return make_loc_error<Value>(module_name, span, error.what());
        }
        return value;
    }
    if (!object.is_object()) {
        return make_loc_error<Value>(module_name, span, "Member assignment requires object receiver.");
    }
    GcObject* raw = object.as_object();
    if (raw->kind == ObjectKind::StructInstance) {
        auto* instance = static_cast<StructInstanceObject*>(raw);
        const std::size_t field_slot = instance->field_slot(member);
        if (field_slot == static_cast<std::size_t>(-1)) {
            return make_loc_error<Value>(module_name, span, "Unknown struct field '" + member + "'.");
        }
        if (field_slot < instance->type->fields.size()) {
            ZEPHYR_TRY(enforce_type(value, instance->type->fields[field_slot].type_name, span, module_name, "field assignment"));
        }
        ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::HeapField, span, module_name, "field assignment"));
        instance->field_values[field_slot] = value;
        note_struct_field_write(instance, field_slot, value);
        return value;
    }
    return make_loc_error<Value>(module_name, span, "Unsupported member assignment target.");
}

RuntimeResult<Value> Runtime::get_index_value(const Value& object, const Value& index, const Span& span, const std::string& module_name) {
    if (!index.is_int()) {
        return make_loc_error<Value>(module_name, span, "Index must be Int.");
    }
    if (!object.is_object() || object.as_object()->kind != ObjectKind::Array) {
        return make_loc_error<Value>(module_name, span, "Indexing requires Array.");
    }
    auto* array = static_cast<ArrayObject*>(object.as_object());
    const auto offset = static_cast<std::size_t>(index.as_int());
    if (offset >= array->elements.size()) {
        return make_loc_error<Value>(module_name, span, "Array index out of range.");
    }
    return array->elements[offset];
}

RuntimeResult<Value> Runtime::set_index_value(const Value& object, const Value& index, const Value& value, const Span& span,
                                              const std::string& module_name) {
    if (!object.is_object() || object.as_object()->kind != ObjectKind::Array) {
        return make_loc_error<Value>(module_name, span, "Index assignment requires array.");
    }
    if (!index.is_int()) {
        return make_loc_error<Value>(module_name, span, "Array index must be Int.");
    }
    auto* array = static_cast<ArrayObject*>(object.as_object());
    const auto offset = static_cast<std::size_t>(index.as_int());
    if (offset >= array->elements.size()) {
        return make_loc_error<Value>(module_name, span, "Array index out of range.");
    }
    ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::ArrayElement, span, module_name, "array element assignment"));
    array->elements[offset] = value;
    note_array_element_write(array, offset, value);
    return value;
}

RuntimeResult<Value> Runtime::call_member_value(const Value& object, const std::string& member, const std::vector<Value>& args, const Span& span,
                                                const std::string& module_name) {
    ScopedVectorItem<const Value*> object_root(rooted_values_, &object);
    ScopedVectorItem<const std::vector<Value>*> args_root(rooted_value_vectors_, &args);
    if (object.is_host_handle()) {
        ZEPHYR_TRY_ASSIGN(resolution, resolve_host_handle(object, span, module_name, "method call"));
        if (const auto* method = resolution.entry->host_class->find_method(member)) {
            auto public_args_lease = acquire_public_args_buffer(args.size());
            auto& public_args = public_args_lease.args();
            for (const auto& value : args) {
                public_args.push_back(to_public_value(value));
            }
            try {
                return from_public_value((*method)(resolution.instance, public_args));
            } catch (const std::exception& error) {
                return make_loc_error<Value>(module_name, span, error.what());
            }
        }
    }

    if (object.is_object() && object.as_object()->kind == ObjectKind::StructInstance) {
        auto* instance = static_cast<StructInstanceObject*>(object.as_object());
        const std::size_t slot = instance->field_slot(member);
        if (slot != static_cast<std::size_t>(-1)) {
            return call_value(instance->field_values[slot], args, span, module_name);
        }
        // Check freestanding impl instance methods
        if (instance->type != nullptr) {
            const auto im_it = instance->type->instance_methods.find(member);
            if (im_it != instance->type->instance_methods.end()) {
                std::vector<Value> dispatch_args;
                dispatch_args.reserve(args.size() + 1);
                dispatch_args.push_back(object);
                dispatch_args.insert(dispatch_args.end(), args.begin(), args.end());
                return call_value(im_it->second, dispatch_args, span, module_name);
            }
        }
        ZEPHYR_TRY_ASSIGN(trait_method, resolve_trait_method(object, member, span, module_name));
        if (trait_method.has_value()) {
            std::vector<Value> dispatch_args;
            dispatch_args.reserve(args.size() + 1);
            dispatch_args.push_back(object);
            dispatch_args.insert(dispatch_args.end(), args.begin(), args.end());
            return call_value(*trait_method, dispatch_args, span, module_name);
        }
    } else if (object.is_object() && object.as_object()->kind == ObjectKind::EnumInstance) {
        ZEPHYR_TRY_ASSIGN(trait_method, resolve_trait_method(object, member, span, module_name));
        if (trait_method.has_value()) {
            std::vector<Value> dispatch_args;
            dispatch_args.reserve(args.size() + 1);
            dispatch_args.push_back(object);
            dispatch_args.insert(dispatch_args.end(), args.begin(), args.end());
            return call_value(*trait_method, dispatch_args, span, module_name);
        }
    }

    ZEPHYR_TRY_ASSIGN(callee, get_member_value(object, member, span, module_name));
    return call_value(callee, args, span, module_name);
}

RuntimeResult<Value> Runtime::execute_register_bytecode(const BytecodeFunction& chunk, const std::vector<Param>& params, Environment* call_env,
                                                        ModuleRecord& module, const Span& call_span,
                                                        const std::vector<UpvalueCellObject*>* captured_upvalues,
                                                        const std::vector<Value>* call_args) {
    (void)params;
    ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(&chunk, call_span, module.name, "Register bytecode chunk"));

    const std::size_t reg_count = static_cast<std::size_t>(std::max({chunk.max_regs, chunk.local_count, 0}));
    const std::size_t spill_count = static_cast<std::size_t>(std::max(chunk.spill_count, 0));

    // Use pre-allocated register pool to avoid heap allocation on recursive calls.
    const std::size_t reg_base = register_sp_;
    const std::size_t total_needed = reg_count + spill_count;
    register_sp_ += total_needed;
    if (register_sp_ > register_pool_.size()) {
        register_pool_.resize(register_sp_ * 2, Value::nil());
    }
    Value* regs_data = register_pool_.data() + reg_base;
    std::fill_n(regs_data, total_needed, Value::nil());
    if (call_args != nullptr) {
        for (std::size_t index = 0; index < call_args->size() && index < reg_count; ++index) {
            regs_data[index] = (*call_args)[index];
        }
    }
    // Restore register_sp_ on exit (RAII guard)
    struct RegPoolGuard {
        std::size_t& sp; std::size_t saved; Value* base; std::size_t count;
        ~RegPoolGuard() { std::fill_n(base, count, Value::nil()); sp = saved; }
    } reg_guard{register_sp_, reg_base, regs_data, total_needed};

    Value* __restrict spill_ptr = spill_count > 0 ? regs_data + reg_count : nullptr;

    // ── Iterative call frame stack (Lua-style SP movement) ──────────────
    struct RegCallFrame {
        std::size_t ip;
        std::size_t reg_base;
        std::size_t reg_count;
        std::size_t dst;
        const BytecodeFunction* chunk;  // null = same function (skip pointer restore)
        Environment* call_env;
        const std::string* module_name;
        Environment* module_env;
        const std::vector<UpvalueCellObject*>* upvalues;
    };
    std::vector<RegCallFrame> iterative_call_stack_;
    iterative_call_stack_.reserve(64);

    // Mutable frame state — changes on push/pop
    const BytecodeFunction* active_chunk = &chunk;
    Environment* active_call_env = call_env;
    const std::string* active_module_name = &module.name;
    Environment* active_module_env = module.environment;
    std::size_t active_reg_base = reg_base;
    std::size_t active_reg_count = reg_count;
    const std::vector<UpvalueCellObject*>* active_upvalues = captured_upvalues;

    auto register_index = [&](std::uint8_t reg, const Span& span) -> RuntimeResult<std::size_t> {
        if (static_cast<std::size_t>(reg) >= active_reg_count) {
            return make_loc_error<std::size_t>(*active_module_name, span, "Invalid register access.");
        }
        return static_cast<std::size_t>(reg);
    };

    auto global_base_env = [&]() -> Environment* {
        if (active_call_env == nullptr) {
            return active_module_env;
        }
        return active_chunk->global_slots_use_module_root_base ? module_or_root_environment(active_call_env) : active_call_env;
    };

    auto resolve_global_binding = [&](int slot, const Span& span) -> RuntimeResult<std::pair<Environment*, Binding*>> {
        if (slot < 0 || static_cast<std::size_t>(slot) >= active_chunk->global_names.size()) {
            return make_loc_error<std::pair<Environment*, Binding*>>(*active_module_name, span, "Invalid global slot access.");
        }
        Environment* env = global_base_env();
        while (env != nullptr) {
            auto it = env->values.find(active_chunk->global_names[static_cast<std::size_t>(slot)]);
            if (it != env->values.end()) {
                return std::pair<Environment*, Binding*>{env, &it->second};
            }
            env = env->parent;
        }
        return make_loc_error<std::pair<Environment*, Binding*>>(*active_module_name,
                                                                 span,
                                                                 "Unknown identifier '" + active_chunk->global_names[static_cast<std::size_t>(slot)] + "'.");
    };

    // Global binding cache — only used for the root frame (iterative frames bypass via resolve_global_binding)
    const std::size_t r_global_count = chunk.global_names.size();
    std::vector<Environment*>    reg_global_binding_owners(r_global_count, nullptr);
    std::vector<Binding*>        reg_global_bindings(r_global_count, nullptr);
    std::vector<std::uint64_t>   reg_global_binding_versions(r_global_count, 0);

    auto ensure_r_global_binding = [&](int slot) -> bool {
        const std::size_t active_global_count = active_chunk->global_names.size();
        if (slot < 0 || static_cast<std::size_t>(slot) >= active_global_count) return false;
        // For iterative sub-frames (different chunk), bypass root-frame cache
        if (active_chunk != &chunk || static_cast<std::size_t>(slot) >= r_global_count) {
            // Direct env lookup for sub-frames — cache the result in the binding pair returned by caller
            ++global_binding_cache_misses_;
            return false;
        }
        const std::size_t s = static_cast<std::size_t>(slot);
        if (reg_global_bindings[s] != nullptr && reg_global_binding_owners[s] != nullptr &&
            reg_global_binding_versions[s] == reg_global_binding_owners[s]->version) {
            ++global_binding_cache_hits_;
            return true;
        }
        ++global_binding_cache_misses_;
        reg_global_binding_owners[s] = nullptr;
        reg_global_bindings[s] = nullptr;
        reg_global_binding_versions[s] = 0;
        Environment* env = global_base_env();
        while (env != nullptr) {
            const auto it = env->values.find(active_chunk->global_names[s]);
            if (it != env->values.end()) {
                reg_global_binding_owners[s] = env;
                reg_global_bindings[s] = &it->second;
                reg_global_binding_versions[s] = env->version;
                return true;
            }
            env = env->parent;
        }
        return false;
    };

    auto binary_fast_or_fallback = [&](BytecodeOp op, Value left, Value right, const Span& span) -> RuntimeResult<Value> {
        if (left.is_int() && right.is_int()) {
            const std::int64_t a = left.as_int();
            const std::int64_t b = right.as_int();
            std::int64_t int_result = 0;
            switch (op) {
                case BytecodeOp::R_ADD:
                    if (try_add_int48(a, b, int_result)) return Value::integer(int_result);
                    break;
                case BytecodeOp::R_SUB:
                    if (try_sub_int48(a, b, int_result)) return Value::integer(int_result);
                    break;
                case BytecodeOp::R_MUL:
                    if (try_mul_int48(a, b, int_result)) return Value::integer(int_result);
                    break;
                case BytecodeOp::R_MOD:
                    return Value::integer(a % b);
                case BytecodeOp::R_LT:
                    return Value::boolean(a < b);
                case BytecodeOp::R_LE:
                    return Value::boolean(a <= b);
                case BytecodeOp::R_GT:
                    return Value::boolean(a > b);
                case BytecodeOp::R_GE:
                    return Value::boolean(a >= b);
                case BytecodeOp::R_EQ:
                    return Value::boolean(a == b);
                case BytecodeOp::R_NE:
                    return Value::boolean(a != b);
                default:
                    break;
            }
        }

        TokenType token = TokenType::Plus;
        switch (op) {
            case BytecodeOp::R_ADD: token = TokenType::Plus; break;
            case BytecodeOp::R_SUB: token = TokenType::Minus; break;
            case BytecodeOp::R_MUL: token = TokenType::Star; break;
            case BytecodeOp::R_DIV: token = TokenType::Slash; break;
            case BytecodeOp::R_MOD: token = TokenType::Percent; break;
            case BytecodeOp::R_LT: token = TokenType::Less; break;
            case BytecodeOp::R_LE: token = TokenType::LessEqual; break;
            case BytecodeOp::R_GT: token = TokenType::Greater; break;
            case BytecodeOp::R_GE: token = TokenType::GreaterEqual; break;
            case BytecodeOp::R_EQ: token = TokenType::EqualEqual; break;
            case BytecodeOp::R_NE: token = TokenType::BangEqual; break;
            default: break;
        }
        return apply_binary_op(token, left, right, span, *active_module_name);
    };

    struct OpcodeCountCommit {
        std::size_t& total;
        std::size_t delta = 0;
        ~OpcodeCountCommit() { total += delta; }
    } opcode_count_commit{opcode_execution_count_};

    std::size_t ip = 0;
    Value* __restrict regs_ptr = regs_data;
    const CompactInstruction* __restrict instructions_ptr = chunk.instructions.data();
    const InstructionMetadata* metadata_ptr = chunk.metadata.data();
    const BytecodeConstant* __restrict constants_ptr = chunk.constants.data();
    std::size_t instructions_size = chunk.instructions.size();

    // ── C Dispatch Fast Path (computed goto) ────────────────────────
    // Try C dispatch for pure register-mode functions. Falls back to C++ for cold opcodes.
    // Skip for functions with upvalues (C dispatch doesn't support R_LOAD_UPVALUE yet).
    static_assert(sizeof(CompactInstruction) == sizeof(ZInstruction),
                  "CompactInstruction and ZInstruction must have identical layout.");
    // Skip C dispatch for functions with upvalues OR R_MAKE_FUNCTION (not yet in C dispatch table).
    // These fall back to call_value which misses the iterative frame stack.
    if (captured_upvalues == nullptr || captured_upvalues->empty()) {
        // Extract int constants
        std::vector<int64_t> c_int_constants(chunk.constants.size());
        std::vector<int> c_int_valid(chunk.constants.size(), 0);
        for (size_t ci = 0; ci < chunk.constants.size(); ++ci) {
            if (const auto* iv = std::get_if<std::int64_t>(&chunk.constants[ci])) {
                c_int_constants[ci] = *iv;
                c_int_valid[ci] = 1;
            }
        }

        // Resolve globals if not yet done
        if (!active_chunk->globals_resolved && !active_chunk->global_names.empty()) {
            active_chunk->resolved_global_bindings.resize(active_chunk->global_names.size(), nullptr);
            active_chunk->resolved_global_owners.resize(active_chunk->global_names.size(), nullptr);
            Environment* base = global_base_env();
            for (size_t gi = 0; gi < active_chunk->global_names.size(); ++gi) {
                Environment* env = base;
                while (env != nullptr) {
                    auto it = env->values.find(active_chunk->global_names[gi]);
                    if (it != env->values.end()) {
                        active_chunk->resolved_global_bindings[gi] = &it->second;
                        active_chunk->resolved_global_owners[gi] = env;
                        break;
                    }
                    env = env->parent;
                }
            }
            active_chunk->globals_resolved = true;
        }

        // Build dispatch state
        ZDispatchState dstate = {};
        dstate.regs = reinterpret_cast<ZephyrVal*>(regs_data);
        dstate.reg_pool = reinterpret_cast<ZephyrVal*>(register_pool_.data());
        dstate.reg_pool_capacity = register_pool_.size();
        dstate.register_sp = register_sp_;
        dstate.ip = 0;
        dstate.instructions = reinterpret_cast<const ZInstruction*>(chunk.instructions.data());
        dstate.instructions_size = chunk.instructions.size();
        dstate.int_constants = c_int_constants.data();
        dstate.int_constant_valid = c_int_valid.data();
        dstate.constants_count = chunk.constants.size();
        dstate.globals.bindings = reinterpret_cast<void**>(
            active_chunk->resolved_global_bindings.data());
        dstate.globals.resolved = active_chunk->globals_resolved ? 1 : 0;
        dstate.globals.count = active_chunk->global_names.size();
        dstate.call_stack = nullptr;
        dstate.call_stack_sp = 0;
        dstate.call_stack_capacity = 0;
        dstate.active_chunk = active_chunk;
        dstate.active_reg_count = active_reg_count;

        // Allocate call stack for C dispatch
        std::vector<ZCallFrame> c_call_stack(64);
        dstate.call_stack = c_call_stack.data();
        dstate.call_stack_capacity = c_call_stack.size();

        // Set up callbacks — must be non-capturing for C function pointer compatibility
        // read_global: read Value bits from Binding* via resolved_global_bindings
        struct CDispatchHelpers {
            static ZephyrVal read_global(void* /*runtime*/, ZDispatchState* s, int slot) {
                auto* bindings = reinterpret_cast<Binding**>(s->globals.bindings);
                if (bindings && bindings[slot]) {
                    // Binding::value is the first field (type Value, 8 bytes = uint64_t bits_)
                    ZephyrVal val;
                    std::memcpy(&val, &bindings[slot]->value, sizeof(ZephyrVal));
                    return val;
                }
                return ZV_NIL_TAG;
            }

            static int slow_opcode(ZDispatchState* /*s*/, void* /*runtime*/, size_t /*opcode_ip*/) {
                return ZVM_SLOW_OPCODE;
            }
        };

        ZCallbacks cbs = {};
        cbs.slow_opcode = CDispatchHelpers::slow_opcode;
        cbs.read_global = CDispatchHelpers::read_global;
        cbs.call_handler = nullptr;
        cbs.runtime = this;

        int c_result = zephyr_vm_dispatch(&dstate, &cbs);

        // Sync state back
        register_sp_ = dstate.register_sp;

        if (c_result == ZVM_RETURN) {
            // Convert the raw bits back to Value
            Value ret_val;
            std::memcpy(&ret_val, &dstate.return_value, sizeof(Value));
            return ret_val;
        }

        // ZVM_SLOW_OPCODE or ZVM_ERROR: fall through to C++ loop
        ip = dstate.ip;
        regs_ptr = reinterpret_cast<Value*>(dstate.regs);
        // Continue with existing C++ dispatch loop below
    }

    for (;;) {
        const CompactInstruction& instruction = instructions_ptr[ip];

        switch (instruction.op) {
            case BytecodeOp::R_LOAD_CONST: {
                const BytecodeConstant& bc = constants_ptr[static_cast<std::size_t>(unpack_r_index_operand(instruction.operand))];
                if (const auto* iv = std::get_if<std::int64_t>(&bc)) {
                    regs_ptr[unpack_r_dst_operand(instruction.operand)] = Value::integer(*iv);
                } else {
                    ZEPHYR_TRY_ASSIGN(cv, load_bytecode_constant(chunk, unpack_r_index_operand(instruction.operand)));
                    regs_ptr[unpack_r_dst_operand(instruction.operand)] = cv;
                }
                ++ip;
                break;
            }
            case BytecodeOp::R_LOAD_INT: {
                regs_ptr[unpack_r_load_int_dst(instruction.operand)] =
                    Value::integer(unpack_r_load_int_value(instruction.operand));
                ++ip;
                break;
            }
            case BytecodeOp::R_ADDI: {
                const Value& addi_src = regs_ptr[unpack_r_addi_src(instruction.operand)];
                const std::int64_t addi_imm = unpack_r_addi_imm(instruction.operand);
                if (addi_src.is_int()) {
                    const std::int64_t sum = addi_src.as_int() + addi_imm;
                    if (sum >= Value::kIntMin && sum <= Value::kIntMax) {
                        regs_ptr[unpack_r_addi_dst(instruction.operand)] = Value::integer(sum);
                        ++ip;
                        break;
                    }
                }
                { const Span span = instruction_span(instruction);
                const Value imm_val = Value::integer(addi_imm);
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_ADD, addi_src, imm_val, span));
                regs_ptr[unpack_r_addi_dst(instruction.operand)] = result; }
                ++ip;
                break;
            }
            case BytecodeOp::R_MODI: {
                const Value& modi_src = regs_ptr[unpack_r_modi_src(instruction.operand)];
                const std::int64_t modi_imm = unpack_r_modi_imm(instruction.operand);
                if (modi_src.is_int() && modi_imm > 0) {
                    regs_ptr[unpack_r_modi_dst(instruction.operand)] = Value::integer(modi_src.as_int() % modi_imm);
                    ++ip;
                    break;
                }
                { const Span span = instruction_span(instruction);
                const Value imm_val = Value::integer(modi_imm);
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_MOD, modi_src, imm_val, span));
                regs_ptr[unpack_r_modi_dst(instruction.operand)] = result; }
                ++ip;
                break;
            }
            case BytecodeOp::R_ADDI_JUMP: {
                const Value& aj_src = regs_ptr[unpack_r_addi_src(instruction.operand)];
                const std::int64_t aj_imm = unpack_r_addi_imm(instruction.operand);
                if (aj_src.is_int()) {
                    const std::int64_t aj_sum = aj_src.as_int() + aj_imm;
                    if (aj_sum >= Value::kIntMin && aj_sum <= Value::kIntMax) {
                        regs_ptr[unpack_r_addi_dst(instruction.operand)] = Value::integer(aj_sum);
                        ip = static_cast<std::size_t>(instruction.ic_slot);
                        break;
                    }
                }
                { const Span span = instruction_span(instruction);
                const Value imm_val = Value::integer(aj_imm);
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_ADD, aj_src, imm_val, span));
                regs_ptr[unpack_r_addi_dst(instruction.operand)] = result; }
                ip = static_cast<std::size_t>(instruction.ic_slot);
                break;
            }
            case BytecodeOp::R_SI_ADDI_CMPI_LT_JUMP: {
                const std::uint8_t acj_reg = unpack_r_si_acj_reg(instruction.operand);
                const Value& acj_val = regs_ptr[acj_reg];
                const std::int64_t acj_addi = unpack_r_si_acj_addi(instruction.operand);
                const std::int64_t acj_limit = unpack_r_si_acj_limit(instruction.operand);
                if (acj_val.is_int()) {
                    const std::int64_t acj_new = acj_val.as_int() + acj_addi;
                    if (acj_new >= Value::kIntMin && acj_new <= Value::kIntMax) {
                        regs_ptr[acj_reg] = Value::integer(acj_new);
                        if (acj_new < acj_limit) {
                            ip = static_cast<std::size_t>(instruction.ic_slot);
                        } else {
                            ++ip;
                        }
                        break;
                    }
                }
                { const Span span = instruction_span(instruction);
                const Value acj_addi_val = Value::integer(acj_addi);
                ZEPHYR_TRY_ASSIGN(acj_new_val, binary_fast_or_fallback(BytecodeOp::R_ADD, acj_val, acj_addi_val, span));
                regs_ptr[acj_reg] = acj_new_val;
                const Value acj_limit_val = Value::integer(acj_limit);
                ZEPHYR_TRY_ASSIGN(acj_cmp, binary_fast_or_fallback(BytecodeOp::R_LT, acj_new_val, acj_limit_val, span));
                if (is_truthy(acj_cmp)) { ip = static_cast<std::size_t>(instruction.ic_slot); } else { ++ip; } }
                break;
            }
            case BytecodeOp::R_SI_LOOP_STEP: {
                const std::int64_t ls_div   = static_cast<std::int64_t>(instruction.operand_a);
                const std::int64_t ls_step  = static_cast<std::int64_t>(static_cast<std::int8_t>(instruction.src1));
                const std::int64_t ls_limit = unpack_r_si_ls_limit(instruction.ic_slot);
                const std::size_t  ls_body  = unpack_r_si_ls_body(instruction.ic_slot);
                Value& ls_acc  = regs_ptr[instruction.dst];
                Value& ls_iter = regs_ptr[instruction.src2];
                if (ls_acc.is_int() && ls_iter.is_int()) {
                    const std::int64_t ls_a = ls_acc.as_int();
                    const std::int64_t ls_i = ls_iter.as_int();
                    const std::int64_t ls_new_acc  = ls_a + (ls_i % ls_div);
                    const std::int64_t ls_new_iter = ls_i + ls_step;
                    if (ls_new_acc >= Value::kIntMin && ls_new_acc <= Value::kIntMax &&
                        ls_new_iter >= Value::kIntMin && ls_new_iter <= Value::kIntMax) {
                        ls_acc  = Value::integer(ls_new_acc);
                        ls_iter = Value::integer(ls_new_iter);
                        ip = (ls_new_iter < ls_limit) ? ls_body : ip + 1;
                        break;
                    }
                }
                { const Span ls_span = instruction_span(instruction);
                const Value ls_div_val  = Value::integer(ls_div);
                ZEPHYR_TRY_ASSIGN(ls_mod, binary_fast_or_fallback(BytecodeOp::R_MOD, ls_iter, ls_div_val, ls_span));
                ZEPHYR_TRY_ASSIGN(ls_new_acc, binary_fast_or_fallback(BytecodeOp::R_ADD, ls_acc, ls_mod, ls_span));
                ls_acc = ls_new_acc;
                const Value ls_step_val = Value::integer(ls_step);
                ZEPHYR_TRY_ASSIGN(ls_new_iter, binary_fast_or_fallback(BytecodeOp::R_ADD, ls_iter, ls_step_val, ls_span));
                ls_iter = ls_new_iter;
                const Value ls_limit_val = Value::integer(ls_limit);
                ZEPHYR_TRY_ASSIGN(ls_cmp, binary_fast_or_fallback(BytecodeOp::R_LT, ls_new_iter, ls_limit_val, ls_span));
                ip = is_truthy(ls_cmp) ? ls_body : ip + 1; }
                break;
            }
            case BytecodeOp::R_LOAD_GLOBAL: {
                const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_dst_operand(instruction.operand), span));
                const int r_gslot = unpack_r_index_operand(instruction.operand);
                // Fast path: use per-chunk flat cache (resolved once, reused across recursive calls)
                if (active_chunk->globals_resolved) {
                    regs_ptr[dst] = read_binding_value(*active_chunk->resolved_global_bindings[static_cast<std::size_t>(r_gslot)]);
                    ++ip;
                    break;
                }
                // First execution: resolve all globals for this chunk and cache
                {
                    const std::size_t gc = active_chunk->global_names.size();
                    active_chunk->resolved_global_bindings.resize(gc, nullptr);
                    active_chunk->resolved_global_owners.resize(gc, nullptr);
                    Environment* base = global_base_env();
                    for (std::size_t gi = 0; gi < gc; ++gi) {
                        Environment* env = base;
                        while (env != nullptr) {
                            auto it = env->values.find(active_chunk->global_names[gi]);
                            if (it != env->values.end()) {
                                active_chunk->resolved_global_bindings[gi] = &it->second;
                                active_chunk->resolved_global_owners[gi] = env;
                                break;
                            }
                            env = env->parent;
                        }
                    }
                    active_chunk->globals_resolved = true;
                    if (active_chunk->resolved_global_bindings[static_cast<std::size_t>(r_gslot)] == nullptr) {
                        return make_loc_error<Value>(*active_module_name, span,
                            "Unknown identifier '" + active_chunk->global_names[static_cast<std::size_t>(r_gslot)] + "'.");
                    }
                    regs_ptr[dst] = read_binding_value(*active_chunk->resolved_global_bindings[static_cast<std::size_t>(r_gslot)]);
                }
                ++ip;
                break;
            }
            case BytecodeOp::R_STORE_GLOBAL: {
                const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(src, register_index(unpack_r_src_operand(instruction.operand), span));
                const int r_gslot = unpack_r_index_operand(instruction.operand);
                if (!ensure_r_global_binding(r_gslot)) {
                    // Fallback: direct resolve for iterative sub-frames
                    ZEPHYR_TRY_ASSIGN(binding_pair_sg, resolve_global_binding(r_gslot, span));
                    // Populate cache slots so code below can use them
                    const std::size_t sg_s = static_cast<std::size_t>(r_gslot);
                    if (active_chunk == &chunk && sg_s < r_global_count) {
                        reg_global_binding_owners[sg_s] = binding_pair_sg.first;
                        reg_global_bindings[sg_s] = binding_pair_sg.second;
                        reg_global_binding_versions[sg_s] = binding_pair_sg.first->version;
                    }
                }
                const std::size_t rgs = static_cast<std::size_t>(r_gslot);
                Binding* binding = reg_global_bindings[rgs];
                Environment* owner = reg_global_binding_owners[rgs];
                if (!binding->mutable_value) {
                    return make_loc_error<Value>(module.name, span,
                        "Cannot assign to immutable binding '" + chunk.global_names[rgs] + "'.");
                }
                ZEPHYR_TRY(enforce_type(regs_ptr[src], binding->type_name, span, module.name, "assignment"));
                ZEPHYR_TRY(validate_handle_store(regs_ptr[src], HandleContainerKind::Global, span, module.name, "global assignment"));
                if (binding->cell != nullptr) {
                    ZEPHYR_TRY(validate_handle_store(regs_ptr[src], binding->cell->container_kind, span, module.name, "closure capture assignment"));
                }
                write_binding_value(*binding, regs_ptr[src]);
                note_write(owner, regs_ptr[src]);
                if (binding->cell != nullptr) {
                    note_write(static_cast<GcObject*>(binding->cell), regs_ptr[src]);
                }
                ++ip;
                break;
            }
            case BytecodeOp::R_MOVE: {
                regs_ptr[instruction.dst] = regs_ptr[instruction.src1];
                ++ip;
                break;
            }
            // Individual binary op cases — each has its own int fast path, eliminating nested switch overhead.
            case BytecodeOp::R_ADD: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    std::int64_t ir = 0;
                    if (try_add_int48(lhs.as_int(), rhs.as_int(), ir)) {
                        regs_ptr[instruction.dst] = Value::integer(ir);
                        ++ip; break;
                    }
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_ADD, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_SUB: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    std::int64_t ir = 0;
                    if (try_sub_int48(lhs.as_int(), rhs.as_int(), ir)) {
                        regs_ptr[instruction.dst] = Value::integer(ir);
                        ++ip; break;
                    }
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_SUB, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_MUL: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    std::int64_t ir = 0;
                    if (try_mul_int48(lhs.as_int(), rhs.as_int(), ir)) {
                        regs_ptr[instruction.dst] = Value::integer(ir);
                        ++ip; break;
                    }
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_MUL, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_DIV: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int() && rhs.as_int() != 0) {
                    const std::int64_t a = lhs.as_int(), b = rhs.as_int();
                    regs_ptr[instruction.dst] = Value::integer(a / b);
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_DIV, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_MOD: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int() && rhs.as_int() != 0) {
                    regs_ptr[instruction.dst] = Value::integer(lhs.as_int() % rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_MOD, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_LT: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    regs_ptr[instruction.dst] = Value::boolean(lhs.as_int() < rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_LT, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_LE: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    regs_ptr[instruction.dst] = Value::boolean(lhs.as_int() <= rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_LE, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_GT: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    regs_ptr[instruction.dst] = Value::boolean(lhs.as_int() > rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_GT, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_GE: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    regs_ptr[instruction.dst] = Value::boolean(lhs.as_int() >= rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_GE, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_EQ: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    regs_ptr[instruction.dst] = Value::boolean(lhs.as_int() == rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_EQ, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_NE: {
                const Value& lhs = regs_ptr[instruction.src1];
                const Value& rhs = regs_ptr[instruction.src2];
                if (lhs.is_int() && rhs.is_int()) {
                    regs_ptr[instruction.dst] = Value::boolean(lhs.as_int() != rhs.as_int());
                    ++ip; break;
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_NE, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_NOT: {
                regs_ptr[instruction.dst] = Value::boolean(!is_truthy(regs_ptr[instruction.src1]));
                ++ip;
                break;
            }
            case BytecodeOp::R_NEG: {
                const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(result, apply_unary_op(TokenType::Minus, regs_ptr[instruction.src1], span, module.name));
                regs_ptr[instruction.dst] = result;
                ++ip;
                break;
            }
            case BytecodeOp::R_YIELD: {
                const Span span = instruction_span(instruction);
                return make_loc_error<Value>(module.name, span, "yield outside coroutine should be rejected at runtime.");
            }
            case BytecodeOp::R_CALL: {
                const std::size_t dst = static_cast<std::size_t>(instruction.dst);
                const std::size_t callee = static_cast<std::size_t>(instruction.src1);
                const std::uint8_t argc = instruction.operand_a;
                const std::uint8_t args_start = instruction.src2;

                // Fast path: ScriptFunction with register-mode bytecode — iterative frame push
                const Value& callee_val = regs_ptr[callee];
                if (callee_val.is_object() && callee_val.as_object()->kind == ObjectKind::ScriptFunction) {
                    auto* function = static_cast<ScriptFunctionObject*>(callee_val.as_object());
                    if (function->bytecode != nullptr && function->bytecode->uses_register_mode) {
                        if (function->bytecode.get() == active_chunk) {
                            // ── Ultra-fast same-function recursion ──
                            // Skip: chunk save/restore, instructions_ptr update, reg_count recompute,
                            //       call_env/module_env save (all identical for same function)
                            iterative_call_stack_.push_back({
                                ip + 1, active_reg_base, active_reg_count, dst,
                                nullptr, nullptr, nullptr, nullptr, nullptr
                            });
                            active_reg_base = register_sp_;
                            register_sp_ += active_reg_count;  // same reg count
                            if (register_sp_ > register_pool_.size()) {
                                register_pool_.resize(register_sp_ * 2, Value::nil());
                            }
                            Value* old_regs = regs_ptr;
                            regs_ptr = register_pool_.data() + active_reg_base;
                            for (std::uint8_t i = 0; i < argc; ++i) {
                                regs_ptr[i] = old_regs[static_cast<std::uint8_t>(args_start + i)];
                            }
                            ip = 0;
                            continue;
                        }
                        // ── Different-function call ──
                        iterative_call_stack_.push_back({
                            ip + 1, active_reg_base, active_reg_count, dst,
                            active_chunk, active_call_env, active_module_name, active_module_env,
                            active_upvalues
                        });
                        active_chunk = function->bytecode.get();
                        active_call_env = function->closure;
                        active_upvalues = &function->captured_upvalues;
                        active_module_env = function->closure;
                        instructions_ptr = active_chunk->instructions.data();
                        metadata_ptr = active_chunk->metadata.data();
                        constants_ptr = active_chunk->constants.data();
                        instructions_size = active_chunk->instructions.size();

                        const int mr = active_chunk->max_regs;
                        const int lc = active_chunk->local_count;
                        const std::size_t new_reg_count = static_cast<std::size_t>(mr > lc ? mr : lc);
                        active_reg_base = register_sp_;
                        register_sp_ += new_reg_count;
                        if (register_sp_ > register_pool_.size()) {
                            register_pool_.resize(register_sp_ * 2, Value::nil());
                        }
                        Value* old_regs = regs_ptr;
                        regs_ptr = register_pool_.data() + active_reg_base;
                        for (std::uint8_t i = 0; i < argc && i < new_reg_count; ++i) {
                            regs_ptr[i] = old_regs[static_cast<std::uint8_t>(args_start + i)];
                        }
                        active_reg_count = new_reg_count;
                        ip = 0;
                        continue;
                    }
                }
                // Slow path: build args vector for call_value()
                const Span span = instruction_span(instruction);
                std::vector<Value> args;
                args.reserve(argc);
                for (std::uint8_t index = 0; index < argc; ++index) {
                    ZEPHYR_TRY_ASSIGN(arg_reg, register_index(static_cast<std::uint8_t>(args_start + index), span));
                    args.push_back(regs_ptr[arg_reg]);
                }
                ZEPHYR_TRY_ASSIGN(result, call_value(regs_ptr[callee], args, span, *active_module_name));
                regs_ptr[dst] = result;
                ++ip;
                break;
            }
            case BytecodeOp::R_LOAD_MEMBER: {
                const Value& lm_obj = regs_ptr[instruction.src1];
                const Span lm_span = instruction_span(instruction);
                if (lm_obj.is_host_handle()) {
                    ZEPHYR_TRY_ASSIGN(lm_res, resolve_host_handle(lm_obj, lm_span, module.name, "member access"));
                    const ZephyrHostClass* lm_class = lm_res.entry->host_class.get();
                    const ZephyrHostClass::Getter* lm_getter = nullptr;
                    if (instruction.ic_shape == reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(lm_class))) {
                        lm_getter = lm_class->get_getter_at(instruction.ic_slot);
                    } else {
                        std::uint32_t lm_idx = 0;
                        lm_getter = lm_class->find_getter_ic(metadata_ptr[ip].string_operand, lm_idx);
                        if (lm_getter != nullptr) {
                            instruction.ic_shape = reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(lm_class));
                            instruction.ic_slot = lm_idx;
                        }
                    }
                    if (lm_getter != nullptr) {
                        try {
                            regs_ptr[instruction.dst] = from_public_value((*lm_getter)(lm_res.instance));
                        } catch (const std::exception& e) {
                            return make_loc_error<Value>(module.name, lm_span, e.what());
                        }
                        ++ip; break;
                    }
                }
                ZEPHYR_TRY_ASSIGN(lm_result, load_member_value(lm_obj, instruction, metadata_ptr[ip], module.name));
                regs_ptr[instruction.dst] = lm_result;
                ++ip; break;
            }
            case BytecodeOp::R_STORE_MEMBER: {
                const Value& sm_obj = regs_ptr[instruction.src1];
                const Value& sm_val = regs_ptr[instruction.src2];
                ZEPHYR_TRY_ASSIGN(sm_result, store_member_value(sm_obj, sm_val, instruction, metadata_ptr[ip], module.name));
                (void)sm_result;
                ++ip; break;
            }
            case BytecodeOp::R_CALL_MEMBER: {
                const Value& cm_obj = regs_ptr[instruction.src1];
                const Span cm_span = instruction_span(instruction);
                if (cm_obj.is_host_handle()) {
                    ZEPHYR_TRY_ASSIGN(cm_res, resolve_host_handle(cm_obj, cm_span, module.name, "method call"));
                    const ZephyrHostClass* cm_class = cm_res.entry->host_class.get();
                    const ZephyrHostClass::Method* cm_method = nullptr;
                    if (instruction.ic_shape == reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(cm_class))) {
                        cm_method = cm_class->get_method_at(instruction.ic_slot);
                    } else {
                        std::uint32_t cm_idx = 0;
                        cm_method = cm_class->find_method_ic(metadata_ptr[ip].string_operand, cm_idx);
                        if (cm_method != nullptr) {
                            instruction.ic_shape = reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(cm_class));
                            instruction.ic_slot = cm_idx;
                        }
                    }
                    if (cm_method != nullptr) {
                        const std::uint8_t cm_argc = instruction.operand_a;
                        auto cm_lease = acquire_public_args_buffer(cm_argc);
                        auto& cm_public_args = cm_lease.args();
                        for (std::uint8_t i = 0; i < cm_argc; ++i) {
                            cm_public_args.push_back(to_public_value(regs_ptr[static_cast<std::size_t>(instruction.src2 + i)]));
                        }
                        try {
                            regs_ptr[instruction.dst] = from_public_value((*cm_method)(cm_res.instance, cm_public_args));
                        } catch (const std::exception& e) {
                            return make_loc_error<Value>(module.name, cm_span, e.what());
                        }
                        ++ip; break;
                    }
                }
                {
                    std::vector<Value> cm_args;
                    cm_args.reserve(instruction.operand_a);
                    for (std::uint8_t i = 0; i < instruction.operand_a; ++i)
                        cm_args.push_back(regs_ptr[static_cast<std::size_t>(instruction.src2 + i)]);
                    ZEPHYR_TRY_ASSIGN(cm_result, call_member_value(cm_obj, metadata_ptr[ip].string_operand, cm_args, cm_span, module.name));
                    regs_ptr[instruction.dst] = cm_result;
                }
                ++ip; break;
            }
            case BytecodeOp::R_BUILD_STRUCT: {
                const std::uint8_t bs_count = instruction.operand_a;
                // IC fast path: StructTypeObject* cached, fields are in-order
                if (instruction.ic_shape != nullptr && instruction.ic_slot == 1) {
                    auto* bs_type = reinterpret_cast<StructTypeObject*>(instruction.ic_shape);
                    auto* bs_inst = allocate<StructInstanceObject>(bs_type);
                    // Inline initialize_struct_instance using cached shape
                    bs_inst->shape = bs_type->cached_shape;
                    bs_inst->field_values.reserve(bs_count);
                    for (std::uint8_t i = 0; i < bs_count; ++i) {
                        const Value& bs_val = regs_ptr[static_cast<std::size_t>(instruction.src1 + i)];
                        bs_inst->field_values.push_back(bs_val);
                        note_struct_field_write(bs_inst, i, bs_val);
                    }
                    regs_ptr[instruction.dst] = Value::object(bs_inst);
                    ++ip; break;
                }
                // Cold path
                {
                    const Span bs_span = instruction_span(instruction);
                    std::vector<Value> bs_fields(bs_count);
                    for (std::uint8_t i = 0; i < bs_count; ++i)
                        bs_fields[i] = regs_ptr[static_cast<std::size_t>(instruction.src1 + i)];
                    ZEPHYR_TRY_ASSIGN(bs_result, build_struct_value(call_env, metadata_ptr[ip].string_operand,
                                                                    metadata_ptr[ip].names, bs_fields, bs_span, module.name));
                    regs_ptr[instruction.dst] = bs_result;
                    // Populate IC on first call if fields are in declared order
                    if (bs_result.is_object() && bs_result.as_object()->kind == ObjectKind::StructInstance) {
                        auto* bs_inst_ic = static_cast<StructInstanceObject*>(bs_result.as_object());
                        StructTypeObject* bs_type_ic = bs_inst_ic->type;
                        const auto& bs_names = metadata_ptr[ip].names;
                        bool bs_in_order = (bs_names.size() == bs_type_ic->fields.size());
                        if (bs_in_order) {
                            for (std::size_t k = 0; k < bs_names.size(); ++k) {
                                if (bs_names[k] != bs_type_ic->fields[k].name) { bs_in_order = false; break; }
                            }
                        }
                        if (bs_in_order) {
                            instruction.ic_shape = reinterpret_cast<Shape*>(bs_type_ic);
                            instruction.ic_slot = 1;
                        }
                    }
                }
                ++ip; break;
            }
            case BytecodeOp::R_BUILD_ARRAY: {
                const std::uint8_t ba_count = instruction.operand_a;
                auto* ba_array = allocate<ArrayObject>();
                ba_array->elements.resize(ba_count);
                for (std::uint8_t i = 0; i < ba_count; ++i) {
                    ba_array->elements[i] = regs_ptr[static_cast<std::size_t>(instruction.src1 + i)];
                    note_array_element_write(ba_array, i, ba_array->elements[i]);
                }
                regs_ptr[instruction.dst] = Value::object(ba_array);
                ++ip; break;
            }
            case BytecodeOp::R_LOAD_INDEX: {
                const Value& li_obj = regs_ptr[instruction.src1];
                const Value& li_idx = regs_ptr[instruction.src2];
                const Span li_span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(li_result, get_index_value(li_obj, li_idx, li_span, module.name));
                regs_ptr[instruction.dst] = li_result;
                ++ip; break;
            }
            case BytecodeOp::R_LOAD_UPVALUE: {
                const std::uint8_t uv_dst = unpack_r_dst_operand(instruction.operand);
                const int uv_slot = unpack_r_index_operand(instruction.operand);
                if (active_upvalues != nullptr &&
                    uv_slot >= 0 && static_cast<std::size_t>(uv_slot) < active_upvalues->size() &&
                    (*active_upvalues)[static_cast<std::size_t>(uv_slot)] != nullptr) {
                    regs_ptr[uv_dst] = (*active_upvalues)[static_cast<std::size_t>(uv_slot)]->value;
                } else {
                    regs_ptr[uv_dst] = Value::nil();
                }
                ++ip; break;
            }
            case BytecodeOp::R_STORE_UPVALUE: {
                const std::uint8_t uv_src = unpack_r_src_operand(instruction.operand);
                const int uv_slot = unpack_r_index_operand(instruction.operand);
                if (active_upvalues != nullptr &&
                    uv_slot >= 0 && static_cast<std::size_t>(uv_slot) < active_upvalues->size() &&
                    (*active_upvalues)[static_cast<std::size_t>(uv_slot)] != nullptr) {
                    (*active_upvalues)[static_cast<std::size_t>(uv_slot)]->value = regs_ptr[uv_src];
                }
                ++ip; break;
            }
            case BytecodeOp::R_MAKE_FUNCTION: {
                const Span span = instruction_span(instruction);
                const InstructionMetadata& meta = metadata_ptr[ip];

                // Cache params/return_type on first call — avoids 100K string/vector allocations
                if (!meta.bytecode->closure_params_cached) {
                    std::vector<Param> func_params;
                    for (std::size_t i = 0; i + 1 < meta.names.size(); i += 2) {
                        Param p;
                        p.name = meta.names[i];
                        p.span = span;
                        if (!meta.names[i + 1].empty()) {
                            p.type = TypeRef{{meta.names[i + 1]}, span};
                        }
                        func_params.push_back(std::move(p));
                    }
                    meta.bytecode->cached_closure_params = std::move(func_params);
                    if (meta.type_name.has_value() && !meta.type_name->empty()) {
                        meta.bytecode->cached_closure_return_type = TypeRef{{*meta.type_name}, span};
                    }
                    meta.bytecode->closure_params_cached = true;
                }

                Environment* closure_env = select_closure_environment(active_call_env, meta.bytecode);
                auto* func_obj = allocate<ScriptFunctionObject>(
                    ScriptFunctionObject::ClosureTag{},
                    closure_env, span, meta.bytecode);

                // Create upvalue cells directly from parent registers — no Environment walk
                if (!meta.jump_table.empty()) {
                    func_obj->captured_upvalues.reserve(meta.jump_table.size());
                    for (std::size_t i = 0; i < meta.jump_table.size(); ++i) {
                        func_obj->captured_upvalues.push_back(
                            allocate<UpvalueCellObject>(
                                regs_ptr[meta.jump_table[i]], true, std::nullopt,
                                HandleContainerKind::ClosureCapture));
                    }
                }

                regs_ptr[instruction.dst] = Value::object(func_obj);
                ++ip; break;
            }
            case BytecodeOp::R_RETURN: {
                const Value return_value = regs_ptr[instruction.src1];
                if (!iterative_call_stack_.empty()) {
                    register_sp_ = active_reg_base;
                    auto& parent = iterative_call_stack_.back();
                    if (parent.chunk == nullptr) {
                        // ── Ultra-fast same-function return ──
                        ip = parent.ip;
                        active_reg_base = parent.reg_base;
                        regs_ptr = register_pool_.data() + active_reg_base;
                        regs_ptr[parent.dst] = return_value;
                        iterative_call_stack_.pop_back();
                        continue;
                    }
                    // ── Different-function return ──
                    ip = parent.ip;
                    active_reg_base = parent.reg_base;
                    active_reg_count = parent.reg_count;
                    active_call_env = parent.call_env;
                    active_module_name = parent.module_name;
                    active_module_env = parent.module_env;
                    active_upvalues = parent.upvalues;
                    active_chunk = parent.chunk;
                    instructions_ptr = active_chunk->instructions.data();
                    metadata_ptr = active_chunk->metadata.data();
                    constants_ptr = active_chunk->constants.data();
                    instructions_size = active_chunk->instructions.size();
                    regs_ptr = register_pool_.data() + active_reg_base;
                    regs_ptr[parent.dst] = return_value;
                    iterative_call_stack_.pop_back();
                    continue;
                }
                return return_value;
            }
            case BytecodeOp::R_JUMP:
                ip = static_cast<std::size_t>(instruction.operand);
                break;
            case BytecodeOp::R_JUMP_IF_FALSE:
                if (!is_truthy(regs_ptr[unpack_r_src_operand(instruction.operand)])) {
                    ip = static_cast<std::size_t>(unpack_r_jump_target_operand(instruction.operand));
                } else {
                    ++ip;
                }
                break;
            case BytecodeOp::R_JUMP_IF_TRUE:
                if (is_truthy(regs_ptr[unpack_r_src_operand(instruction.operand)])) {
                    ip = static_cast<std::size_t>(unpack_r_jump_target_operand(instruction.operand));
                } else {
                    ++ip;
                }
                break;
            case BytecodeOp::R_SI_ADD_STORE:
            case BytecodeOp::R_SI_SUB_STORE:
            case BytecodeOp::R_SI_MUL_STORE: {
                const Value& si_lhs = regs_ptr[instruction.src1];
                const Value& si_rhs = regs_ptr[instruction.src2];
                if (si_lhs.is_int() && si_rhs.is_int()) {
                    const std::int64_t a = si_lhs.as_int();
                    const std::int64_t b = si_rhs.as_int();
                    // Direct arithmetic fast path — values from as_int() are 48-bit so the
                    // result fits in int64 without C++ UB. Out-of-range results fall through.
                    const std::int64_t ir = instruction.op == BytecodeOp::R_SI_ADD_STORE ? a + b
                                          : instruction.op == BytecodeOp::R_SI_SUB_STORE ? a - b
                                                                                          : a * b;
                    if (ir >= Value::kIntMin && ir <= Value::kIntMax) {
                        regs_ptr[instruction.dst] = Value::integer(ir); ++ip; break;
                    }
                }
                { const Span span = instruction_span(instruction);
                const BytecodeOp fused_op = instruction.op == BytecodeOp::R_SI_ADD_STORE ? BytecodeOp::R_ADD
                                         : instruction.op == BytecodeOp::R_SI_SUB_STORE ? BytecodeOp::R_SUB
                                                                                         : BytecodeOp::R_MUL;
                ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(fused_op, regs_ptr[src1], regs_ptr[src2], span));
                regs_ptr[dst] = result; }
                ++ip; break;
            }
            case BytecodeOp::R_SI_MODI_ADD_STORE: {
                const Value& sma_acc = regs_ptr[instruction.src1];
                const Value& sma_src = regs_ptr[instruction.src2];
                const std::int64_t sma_div = static_cast<std::int64_t>(instruction.operand_a);
                if (sma_acc.is_int() && sma_src.is_int()) {
                    const std::int64_t sma_r = sma_acc.as_int() + (sma_src.as_int() % sma_div);
                    if (sma_r >= Value::kIntMin && sma_r <= Value::kIntMax) {
                        regs_ptr[instruction.dst] = Value::integer(sma_r); ++ip; break;
                    }
                }
                { const Span sma_span = instruction_span(instruction);
                const Value sma_div_val = Value::integer(sma_div);
                ZEPHYR_TRY_ASSIGN(sma_mod, binary_fast_or_fallback(BytecodeOp::R_MOD, sma_src, sma_div_val, sma_span));
                ZEPHYR_TRY_ASSIGN(sma_result, binary_fast_or_fallback(BytecodeOp::R_ADD, sma_acc, sma_mod, sma_span));
                regs_ptr[instruction.dst] = sma_result; }
                ++ip; break;
            }
            case BytecodeOp::R_SI_CMP_JUMP_FALSE: {
                const std::uint8_t cmp_s1 = unpack_r_si_cmp_jump_false_src1(instruction.operand);
                const std::uint8_t cmp_s2 = unpack_r_si_cmp_jump_false_src2(instruction.operand);
                const BytecodeOp cmp_op = unpack_r_si_cmp_jump_false_compare_op(instruction.operand);
                const Value& lhs = regs_ptr[cmp_s1];
                const Value& rhs = regs_ptr[cmp_s2];
                if (lhs.is_int() && rhs.is_int()) {
                    const std::int64_t a = lhs.as_int();
                    const std::int64_t b = rhs.as_int();
                    bool cmp_val;
                    switch (cmp_op) {
                    case BytecodeOp::R_LT: cmp_val = a < b;  break;
                    case BytecodeOp::R_LE: cmp_val = a <= b; break;
                    case BytecodeOp::R_GT: cmp_val = a > b;  break;
                    case BytecodeOp::R_GE: cmp_val = a >= b; break;
                    case BytecodeOp::R_EQ: cmp_val = a == b; break;
                    case BytecodeOp::R_NE: cmp_val = a != b; break;
                    default:               cmp_val = false;   break;
                    }
                    if (cmp_val) {
                        ++ip;  // Common case: condition true — no metadata access needed.
                    } else {
                        // Condition false: load the jump target from metadata.
                        const InstructionMetadata& metadata = metadata_ptr[ip];
                        ip = metadata.jump_table.empty()
                           ? ip + 1
                           : static_cast<std::size_t>(metadata.jump_table.front());
                    }
                    break;
                }
                { const InstructionMetadata& metadata = metadata_ptr[ip];
                if (metadata.jump_table.empty()) {
                    const Span span = instruction_span(instruction);
                    return make_loc_error<Value>(module.name, span, "Register compare superinstruction is missing jump metadata.");
                }
                const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(src1, register_index(cmp_s1, span));
                ZEPHYR_TRY_ASSIGN(src2, register_index(cmp_s2, span));
                ZEPHYR_TRY_ASSIGN(result,
                                  binary_fast_or_fallback(cmp_op, regs_ptr[src1], regs_ptr[src2], span));
                if (!is_truthy(result)) {
                    ip = static_cast<std::size_t>(metadata.jump_table.front());
                } else {
                    ++ip;
                } }
                break;
            }
            case BytecodeOp::R_SI_CMPI_JUMP_FALSE: {
                const std::uint8_t cmpi_s1 = unpack_r_si_cmpi_jump_false_src1(instruction.operand);
                const std::int64_t cmpi_imm = unpack_r_si_cmpi_jump_false_imm(instruction.operand);
                const auto cmpi_kind = unpack_r_si_cmpi_jump_false_kind(instruction.operand);
                const Value& cmpi_lhs = regs_ptr[cmpi_s1];
                if (cmpi_lhs.is_int()) {
                    const std::int64_t a = cmpi_lhs.as_int();
                    bool cmp_val;
                    switch (cmpi_kind) {
                    case SuperinstructionCompareKind::Less:         cmp_val = a < cmpi_imm;  break;
                    case SuperinstructionCompareKind::LessEqual:    cmp_val = a <= cmpi_imm; break;
                    case SuperinstructionCompareKind::Greater:      cmp_val = a > cmpi_imm;  break;
                    case SuperinstructionCompareKind::GreaterEqual: cmp_val = a >= cmpi_imm; break;
                    case SuperinstructionCompareKind::Equal:        cmp_val = a == cmpi_imm; break;
                    case SuperinstructionCompareKind::NotEqual:     cmp_val = a != cmpi_imm; break;
                    default:                                         cmp_val = false; break;
                    }
                    if (cmp_val) {
                        ++ip;
                    } else {
                        const InstructionMetadata& cmpi_meta = metadata_ptr[ip];
                        ip = cmpi_meta.jump_table.empty() ? ip + 1 : static_cast<std::size_t>(cmpi_meta.jump_table.front());
                    }
                    break;
                }
                { const InstructionMetadata& cmpi_meta = metadata_ptr[ip];
                if (cmpi_meta.jump_table.empty()) {
                    const Span span = instruction_span(instruction);
                    return make_loc_error<Value>(module.name, span, "R_SI_CMPI_JUMP_FALSE missing jump metadata.");
                }
                const Span span = instruction_span(instruction);
                const BytecodeOp cmpi_op = register_bytecode_op_from_superinstruction_compare_kind(cmpi_kind);
                ZEPHYR_TRY_ASSIGN(cmpi_src, register_index(cmpi_s1, span));
                const Value cmpi_rhs_val = Value::integer(cmpi_imm);
                ZEPHYR_TRY_ASSIGN(cmpi_result, binary_fast_or_fallback(cmpi_op, regs_ptr[cmpi_src], cmpi_rhs_val, span));
                if (!is_truthy(cmpi_result)) {
                    ip = static_cast<std::size_t>(cmpi_meta.jump_table.front());
                } else { ++ip; } }
                break;
            }
            case BytecodeOp::R_SI_LOAD_ADD_STORE: {
                const std::size_t las_const = unpack_r_si_load_add_store_constant(instruction.operand);
                const std::uint8_t las_dst = unpack_r_si_load_add_store_dst(instruction.operand);
                const std::uint8_t las_src = unpack_r_si_load_add_store_local_src(instruction.operand);
                const BytecodeConstant& bc_const = constants_ptr[las_const];
                const Value& lhs = regs_ptr[las_src];
                if (lhs.is_int()) {
                    if (const auto* iv = std::get_if<std::int64_t>(&bc_const)) {
                        const std::int64_t sum = lhs.as_int() + *iv;
                        if (sum >= Value::kIntMin && sum <= Value::kIntMax) {
                            regs_ptr[las_dst] = Value::integer(sum);
                            ++ip;
                            break;
                        }
                    }
                }
                { const Span span = instruction_span(instruction);
                ZEPHYR_TRY_ASSIGN(cv_checked, load_bytecode_constant(chunk, static_cast<int>(las_const)));
                ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_ADD, lhs, cv_checked, span));
                regs_ptr[las_dst] = result; }
                ++ip;
                break;
            }
            case BytecodeOp::R_SPILL_LOAD: {
                const std::uint8_t dst = unpack_r_spill_reg(instruction.operand);
                const int sidx = unpack_r_spill_idx(instruction.operand);
                regs_ptr[dst] = spill_ptr[sidx];
                ++ip;
                break;
            }
            case BytecodeOp::R_SPILL_STORE: {
                const std::uint8_t src = unpack_r_spill_reg(instruction.operand);
                const int sidx = unpack_r_spill_idx(instruction.operand);
                spill_ptr[sidx] = regs_ptr[src];
                ++ip;
                break;
            }
            default: {
                const Span span = instruction_span(instruction);
                return make_loc_error<Value>(module.name, span, "Unsupported opcode in register bytecode executor.");
            }
        }
    }

    return Value::nil();
}

RuntimeResult<Value> Runtime::execute_bytecode_chunk(const BytecodeFunction& chunk, const std::vector<Param>& params, Environment* call_env,
                                                     ModuleRecord& module, const Span& call_span,
                                                     const std::vector<UpvalueCellObject*>* captured_upvalues,
                                                     const std::vector<Value>* lightweight_args) {
    ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(&chunk, call_span, module.name, "Bytecode chunk"));
    struct BytecodeScopeState {
        Runtime& runtime;
        Environment*& current_env;
        std::vector<Environment*> scopes;

        ~BytecodeScopeState() {
            while (!scopes.empty()) {
                Environment* scope = scopes.back();
                scopes.pop_back();
                if (!runtime.active_environments_.empty() && runtime.active_environments_.back() == scope) {
                    runtime.active_environments_.pop_back();
                }
                current_env = scope->parent;
            }
        }

        void enter(Environment* scope) {
            scopes.push_back(scope);
            runtime.active_environments_.push_back(scope);
            current_env = scope;
        }

        VoidResult exit_one() {
            if (scopes.empty()) {
                return make_error<std::monostate>("Bytecode scope underflow.");
            }
            Environment* scope = scopes.back();
            scopes.pop_back();
            if (!runtime.active_environments_.empty() && runtime.active_environments_.back() == scope) {
                runtime.active_environments_.pop_back();
            }
            current_env = scope->parent;
            return ok_result();
        }
    };

    // Phase 1.1: lightweight mode flag — true when called without Environment allocation.
    const bool lightweight = (lightweight_args != nullptr);

    std::vector<Value> stack;
    stack.reserve(static_cast<std::size_t>(std::max(chunk.local_count, 0)) * 2 + 32);
    std::vector<Value> locals(static_cast<std::size_t>(std::max(chunk.local_count, 0)), Value::nil());

    // Phase 1.1: In lightweight mode, pre-load params directly into locals[] instead of
    // going through Environment bindings.  Binding cache vectors are left empty.
    std::vector<Environment*> local_binding_owners;
    std::vector<Binding*> local_bindings;
    std::vector<std::uint64_t> local_binding_versions;
    std::vector<Environment*> global_binding_owners;
    std::vector<Binding*> global_bindings;
    std::vector<std::uint64_t> global_binding_versions;
    if (!lightweight) {
        local_binding_owners.resize(locals.size(), nullptr);
        local_bindings.resize(locals.size(), nullptr);
        local_binding_versions.resize(locals.size(), 0);
        global_binding_owners.resize(chunk.global_names.size(), nullptr);
        global_bindings.resize(chunk.global_names.size(), nullptr);
        global_binding_versions.resize(chunk.global_names.size(), 0);
    } else {
        for (std::size_t i = 0; i < lightweight_args->size() && i < locals.size(); ++i) {
            locals[i] = (*lightweight_args)[i];
        }
    }

    // In lightweight mode we skip adding call_env to active_environments_ since we
    // did not allocate a new one — the closure env is already reachable via the function.
    std::optional<ScopedVectorPush<Environment>> call_env_root;
    if (!lightweight) {
        call_env_root.emplace(active_environments_, call_env);
    }
    ScopedVectorItem<const std::vector<Value>*> stack_root(rooted_value_vectors_, &stack);
    ScopedVectorItem<const std::vector<Value>*> locals_root(rooted_value_vectors_, &locals);

    Environment* current_env = call_env;
    Environment* global_resolution_env = (!lightweight && chunk.global_slots_use_module_root_base) ? module_or_root_environment(call_env) : nullptr;
    BytecodeScopeState scopes{*this, current_env};

    auto local_slot_index = [&](int slot, const Span& span) -> RuntimeResult<std::size_t> {
        if (slot < 0 || static_cast<std::size_t>(slot) >= locals.size()) {
            return make_loc_error<std::size_t>(module.name, span, "Invalid local slot access.");
        }
        return static_cast<std::size_t>(slot);
    };

    auto global_slot_index = [&](int slot, const Span& span) -> RuntimeResult<std::size_t> {
        if (slot < 0 || static_cast<std::size_t>(slot) >= chunk.global_names.size()) {
            return make_loc_error<std::size_t>(module.name, span, "Invalid global slot access.");
        }
        return static_cast<std::size_t>(slot);
    };

    auto environment_on_chain = [&](Environment* owner, Environment* search_env) -> bool {
        for (Environment* current = search_env; current != nullptr; current = current->parent) {
            if (current == owner) {
                return true;
            }
        }
        return false;
    };

    auto resolve_local_binding = [&](std::size_t slot, Environment* search_env = nullptr) {
        if (slot >= local_bindings.size()) {
            return;
        }
        local_binding_owners[slot] = nullptr;
        local_bindings[slot] = nullptr;
        local_binding_versions[slot] = 0;
        if (slot >= chunk.local_names.size() || chunk.local_names[slot].empty()) {
            return;
        }
        Environment* env = search_env != nullptr ? search_env : current_env;
        for (Environment* current = env; current != nullptr; current = current->parent) {
            const auto it = current->values.find(chunk.local_names[slot]);
            if (it != current->values.end()) {
                local_binding_owners[slot] = current;
                local_bindings[slot] = &it->second;
                local_binding_versions[slot] = current->version;
                locals[slot] = read_binding_value(it->second);
                return;
            }
        }
    };

    auto local_binding_valid = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (slot >= local_bindings.size() || local_bindings[slot] == nullptr || local_binding_owners[slot] == nullptr) {
            return false;
        }
        Environment* env = search_env != nullptr ? search_env : current_env;
        return local_binding_versions[slot] == local_binding_owners[slot]->version &&
               environment_on_chain(local_binding_owners[slot], env);
    };

    auto ensure_local_binding = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (local_binding_valid(slot, search_env)) {
            ++local_binding_cache_hits_;
            return true;
        }
        ++local_binding_cache_misses_;
        resolve_local_binding(slot, search_env);
        return local_binding_valid(slot, search_env);
    };

    auto global_lookup_env = [&](Environment* search_env = nullptr) -> Environment* {
        Environment* env = search_env != nullptr ? search_env : current_env;
        if (chunk.global_slots_use_module_root_base) {
            return search_env != nullptr ? module_or_root_environment(search_env) : global_resolution_env;
        }
        return env;
    };

    auto resolve_global_binding = [&](std::size_t slot, Environment* search_env = nullptr) {
        if (slot >= global_bindings.size()) {
            return;
        }
        global_binding_owners[slot] = nullptr;
        global_bindings[slot] = nullptr;
        global_binding_versions[slot] = 0;
        Environment* env = global_lookup_env(search_env);
        for (Environment* current = env; current != nullptr; current = current->parent) {
            const auto it = current->values.find(chunk.global_names[slot]);
            if (it != current->values.end()) {
                global_binding_owners[slot] = current;
                global_bindings[slot] = &it->second;
                global_binding_versions[slot] = current->version;
                return;
            }
        }
    };

    auto global_binding_valid = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (slot >= global_bindings.size() || global_bindings[slot] == nullptr || global_binding_owners[slot] == nullptr) {
            return false;
        }
        Environment* env = global_lookup_env(search_env);
        return global_binding_versions[slot] == global_binding_owners[slot]->version &&
               environment_on_chain(global_binding_owners[slot], env);
    };

    auto ensure_global_binding = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (global_binding_valid(slot, search_env)) {
            ++global_binding_cache_hits_;
            return true;
        }
        ++global_binding_cache_misses_;
        resolve_global_binding(slot, search_env);
        return global_binding_valid(slot, search_env);
    };

    if (!lightweight) {
        for (std::size_t i = 0; i < params.size() && i < locals.size(); ++i) {
            resolve_local_binding(i, call_env);
        }
    }

    auto read_local_value = [&](std::size_t slot) -> Value {
        if (lightweight) return locals[slot];
        ensure_local_binding(slot, current_env);
        if (slot < local_bindings.size() && local_bindings[slot] != nullptr) {
            locals[slot] = read_binding_value(*local_bindings[slot]);
        }
        return locals[slot];
    };

    auto read_global_value = [&](std::size_t slot, const Span& span) -> RuntimeResult<Value> {
        ensure_global_binding(slot, current_env);
        if (slot < global_bindings.size() && global_bindings[slot] != nullptr) {
            return read_binding_value(*global_bindings[slot]);
        }
        const std::string& missing = chunk.global_names[slot];
        std::vector<std::string> initialized_names;
        for (std::size_t i = 0; i < chunk.global_names.size(); ++i) {
            if (i < global_bindings.size() && global_bindings[i] != nullptr) {
                initialized_names.push_back(chunk.global_names[i]);
            }
        }
        if (current_env != nullptr) {
            current_env->collect_names(initialized_names);
        }
        std::string source_text;
        auto mod_it = modules_.find(module.name);
        if (mod_it != modules_.end()) {
            source_text = mod_it->second.source_text;
        }
        std::string context = format_source_context(source_text, span, missing.size());
        std::string message = context + "Unknown identifier '" + missing + "'.";
        const auto suggestion = suggest_similar_name(missing, initialized_names);
        if (suggestion) {
            message += "\nhint: did you mean '" + *suggestion + "'?";
        }
        return make_loc_error<Value>(module.name, span, message);
    };

    auto assign_cached_binding =
        [&](Binding* binding, Environment* owner, Value value, const Span& span, const std::string& name) -> VoidResult {
        if (binding == nullptr || owner == nullptr) {
            return make_error<std::monostate>("Missing binding cache entry.");
        }
        if (!binding->mutable_value) {
            return make_loc_error<std::monostate>(module.name, span, "Cannot assign to immutable binding '" + name + "'.");
        }
        ZEPHYR_TRY(enforce_type(value, binding->type_name, span, module.name, "assignment"));
        if (owner->kind == EnvironmentKind::Root || owner->kind == EnvironmentKind::Module) {
            ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::Global, span, module.name, "global assignment"));
        }
        if (binding->cell != nullptr) {
            const std::string capture_context = binding->cell->container_kind == HandleContainerKind::CoroutineFrame
                                                   ? "coroutine capture assignment"
                                                   : "closure capture assignment";
            ZEPHYR_TRY(validate_handle_store(value, binding->cell->container_kind, span, module.name, capture_context));
        }
        write_binding_value(*binding, value);
        note_write(owner, value);
        if (binding->cell != nullptr) {
            note_write(static_cast<GcObject*>(binding->cell), value);
        }
        return ok_result();
    };

    auto upvalue_slot_index = [&](int slot, const Span& span) -> RuntimeResult<std::size_t> {
        if (slot < 0 || captured_upvalues == nullptr || static_cast<std::size_t>(slot) >= captured_upvalues->size()) {
            return make_loc_error<std::size_t>(module.name, span, "Invalid upvalue slot access.");
        }
        return static_cast<std::size_t>(slot);
    };

    auto read_upvalue_value = [&](std::size_t slot) -> RuntimeResult<Value> {
        if (captured_upvalues == nullptr || slot >= captured_upvalues->size() || (*captured_upvalues)[slot] == nullptr) {
            return make_loc_error<Value>(module.name, Span{}, "Missing captured upvalue cell.");
        }
        return (*captured_upvalues)[slot]->value;
    };

    auto pop_value = [&](const Span& span) -> RuntimeResult<Value> {
        if (stack.empty()) {
            return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
        }
        Value value = stack.back();
        stack.pop_back();
        return value;
    };

    struct OpcodeCountCommit {
        std::size_t& total;
        std::size_t delta = 0;

        ~OpcodeCountCommit() {
            total += delta;
        }
    } opcode_count_commit{opcode_execution_count_};

    std::size_t ip = 0;
    while (ip < chunk.instructions.size()) {
        if (gc_stress_enabled_) {
            maybe_run_gc_stress_safe_point();
        }
        const CompactInstruction& instruction = chunk.instructions[ip];
        ++opcode_count_commit.delta;
        if (lightweight) {
            switch (instruction.op) {
                case BytecodeOp::LoadConst: {
                    ZEPHYR_TRY_ASSIGN(value, load_bytecode_constant(chunk, instruction.operand));
                    stack.push_back(value);
                    ++ip;
                    continue;
                }
                case BytecodeOp::LoadLocal: {
                    if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    stack.push_back(locals[static_cast<std::size_t>(instruction.operand)]);
                    ++ip;
                    continue;
                }
                case BytecodeOp::SILoadLocalLoadLocal: {
                    const int first_slot = unpack_si_local_pair_first(instruction.operand);
                    const int second_slot = unpack_si_local_pair_second(instruction.operand);
                    if (first_slot < 0 || second_slot < 0 ||
                        static_cast<std::size_t>(first_slot) >= locals.size() ||
                        static_cast<std::size_t>(second_slot) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    stack.push_back(locals[static_cast<std::size_t>(first_slot)]);
                    stack.push_back(locals[static_cast<std::size_t>(second_slot)]);
                    ++ip;
                    continue;
                }
                case BytecodeOp::StoreLocal: {
                    if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    if (stack.empty()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    Value value = stack.back();
                    stack.pop_back();
                    const std::size_t slot_index = static_cast<std::size_t>(instruction.operand);
                    locals[slot_index] = value;
                    stack.push_back(value);
                    ++ip;
                    continue;
                }
                case BytecodeOp::SIAddStoreLocal: {
                    if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    if (stack.size() < 2) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    Value right = stack.back();
                    stack.pop_back();
                    Value left = stack.back();
                    stack.pop_back();
                    Value result;
                    if (left.is_int() && right.is_int()) {
                        std::int64_t int_result = 0;
                        if (try_add_int48(left.as_int(), right.as_int(), int_result)) {
                            result = Value::integer(int_result);
                        } else {
                            ZEPHYR_TRY_ASSIGN(binary_result,
                                              apply_binary_op(TokenType::Plus, left, right, instruction_span(instruction), module.name));
                            result = binary_result;
                        }
                    } else {
                        ZEPHYR_TRY_ASSIGN(binary_result,
                                          apply_binary_op(TokenType::Plus, left, right, instruction_span(instruction), module.name));
                        result = binary_result;
                    }
                    locals[static_cast<std::size_t>(instruction.operand)] = result;
                    stack.push_back(result);
                    ++ip;
                    continue;
                }
                case BytecodeOp::SILoadLocalAdd: {
                    if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    if (stack.empty()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    Value right = stack.back();
                    stack.pop_back();
                    const Value left = locals[static_cast<std::size_t>(instruction.operand)];
                    if (left.is_int() && right.is_int()) {
                        std::int64_t int_result = 0;
                        if (try_add_int48(left.as_int(), right.as_int(), int_result)) {
                            stack.push_back(Value::integer(int_result));
                            ++ip;
                            continue;
                        }
                    }
                    ZEPHYR_TRY_ASSIGN(binary_result,
                                      apply_binary_op(TokenType::Plus, left, right, instruction_span(instruction), module.name));
                    stack.push_back(binary_result);
                    ++ip;
                    continue;
                }
                case BytecodeOp::SICmpJumpIfFalse: {
                    if (stack.size() < 2) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    Value right = stack.back();
                    stack.pop_back();
                    Value left = stack.back();
                    stack.pop_back();
                    const BytecodeOp comparison_op = unpack_si_cmp_jump_compare_op(instruction.operand);
                    Value compare_value = Value::boolean(false);
                    if (left.is_int() && right.is_int()) {
                        compare_value = Value::boolean(evaluate_int_comparison_opcode(comparison_op, left.as_int(), right.as_int()));
                    } else {
                        ZEPHYR_TRY_ASSIGN(compare_result,
                                          apply_binary_op(token_type_for_bytecode_binary_op(comparison_op),
                                                          left,
                                                          right,
                                                          instruction_span(instruction),
                                                          module.name));
                        compare_value = compare_result;
                    }
                    stack.push_back(compare_value);
                    if (!is_truthy(compare_value)) {
                        ip = static_cast<std::size_t>(unpack_si_cmp_jump_target(instruction.operand));
                    } else {
                        ++ip;
                    }
                    continue;
                }
                case BytecodeOp::SILoadLocalAddStoreLocal: {
                    const int first_slot = unpack_si_local_triple_first(instruction.operand);
                    const int second_slot = unpack_si_local_triple_second(instruction.operand);
                    const int destination_slot = unpack_si_local_triple_destination(instruction.operand);
                    if (first_slot < 0 || second_slot < 0 || destination_slot < 0 ||
                        static_cast<std::size_t>(first_slot) >= locals.size() ||
                        static_cast<std::size_t>(second_slot) >= locals.size() ||
                        static_cast<std::size_t>(destination_slot) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    const Value left = locals[static_cast<std::size_t>(first_slot)];
                    const Value right = locals[static_cast<std::size_t>(second_slot)];
                    Value result;
                    if (left.is_int() && right.is_int()) {
                        std::int64_t int_result = 0;
                        if (try_add_int48(left.as_int(), right.as_int(), int_result)) {
                            result = Value::integer(int_result);
                        } else {
                            ZEPHYR_TRY_ASSIGN(binary_result,
                                              apply_binary_op(TokenType::Plus, left, right, instruction_span(instruction), module.name));
                            result = binary_result;
                        }
                    } else {
                        ZEPHYR_TRY_ASSIGN(binary_result,
                                          apply_binary_op(TokenType::Plus, left, right, instruction_span(instruction), module.name));
                        result = binary_result;
                    }
                    locals[static_cast<std::size_t>(destination_slot)] = result;
                    stack.push_back(result);
                    ++ip;
                    continue;
                }
                case BytecodeOp::SILoadLocalConstAddStoreLocal: {
                    const int source_slot = unpack_si_local_const_local_source(instruction.operand);
                    const int destination_slot = unpack_si_local_const_local_destination(instruction.operand);
                    const int constant_index = unpack_si_local_const_local_constant(instruction.operand);
                    if (source_slot < 0 || destination_slot < 0 ||
                        static_cast<std::size_t>(source_slot) >= locals.size() ||
                        static_cast<std::size_t>(destination_slot) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    ZEPHYR_TRY_ASSIGN(constant_value, load_bytecode_constant(chunk, constant_index));
                    const Value left = locals[static_cast<std::size_t>(source_slot)];
                    Value result;
                    if (left.is_int() && constant_value.is_int()) {
                        std::int64_t int_result = 0;
                        if (try_add_int48(left.as_int(), constant_value.as_int(), int_result)) {
                            result = Value::integer(int_result);
                        } else {
                            ZEPHYR_TRY_ASSIGN(binary_result,
                                              apply_binary_op(TokenType::Plus, left, constant_value, instruction_span(instruction), module.name));
                            result = binary_result;
                        }
                    } else {
                        ZEPHYR_TRY_ASSIGN(binary_result,
                                          apply_binary_op(TokenType::Plus, left, constant_value, instruction_span(instruction), module.name));
                        result = binary_result;
                    }
                    locals[static_cast<std::size_t>(destination_slot)] = result;
                    stack.push_back(result);
                    ++ip;
                    continue;
                }
                case BytecodeOp::SILoadLocalLocalConstModulo: {
                    const int first_slot = unpack_si_local_local_const_first(instruction.operand);
                    const int second_slot = unpack_si_local_local_const_second(instruction.operand);
                    const int constant_index = unpack_si_local_local_const_constant(instruction.operand);
                    if (first_slot < 0 || second_slot < 0 ||
                        static_cast<std::size_t>(first_slot) >= locals.size() ||
                        static_cast<std::size_t>(second_slot) >= locals.size()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Invalid local slot access.");
                    }
                    ZEPHYR_TRY_ASSIGN(constant_value, load_bytecode_constant(chunk, constant_index));
                    const Value first_value = locals[static_cast<std::size_t>(first_slot)];
                    const Value second_value = locals[static_cast<std::size_t>(second_slot)];
                    Value modulo_value;
                    if (second_value.is_int() && constant_value.is_int()) {
                        modulo_value = Value::integer(second_value.as_int() % constant_value.as_int());
                    } else {
                        ZEPHYR_TRY_ASSIGN(binary_result,
                                          apply_binary_op(TokenType::Percent, second_value, constant_value, instruction_span(instruction), module.name));
                        modulo_value = binary_result;
                    }
                    stack.push_back(first_value);
                    stack.push_back(modulo_value);
                    ++ip;
                    continue;
                }
                case BytecodeOp::Add:
                case BytecodeOp::Subtract:
                case BytecodeOp::Multiply:
                case BytecodeOp::Divide:
                case BytecodeOp::Modulo:
                case BytecodeOp::Equal:
                case BytecodeOp::NotEqual:
                case BytecodeOp::Less:
                case BytecodeOp::LessEqual:
                case BytecodeOp::Greater:
                case BytecodeOp::GreaterEqual: {
                    if (stack.size() < 2) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    Value right = stack.back();
                    stack.pop_back();
                    Value left = stack.back();
                    stack.pop_back();
                    if (left.is_int() && right.is_int()) {
                        const std::int64_t a = left.as_int();
                        const std::int64_t b = right.as_int();
                        std::int64_t int_result = 0;
                        bool handled_fast_path = true;
                        switch (instruction.op) {
                            case BytecodeOp::Add:
                                if (try_add_int48(a, b, int_result)) { stack.push_back(Value::integer(int_result)); ++ip; }
                                else { handled_fast_path = false; }
                                break;
                            case BytecodeOp::Subtract:
                                if (try_sub_int48(a, b, int_result)) { stack.push_back(Value::integer(int_result)); ++ip; }
                                else { handled_fast_path = false; }
                                break;
                            case BytecodeOp::Multiply:
                                if (try_mul_int48(a, b, int_result)) { stack.push_back(Value::integer(int_result)); ++ip; }
                                else { handled_fast_path = false; }
                                break;
                            case BytecodeOp::Modulo:
                                stack.push_back(Value::integer(a % b));
                                ++ip;
                                break;
                            case BytecodeOp::Less:         stack.push_back(Value::boolean(a < b));  ++ip; break;
                            case BytecodeOp::LessEqual:    stack.push_back(Value::boolean(a <= b)); ++ip; break;
                            case BytecodeOp::Greater:      stack.push_back(Value::boolean(a > b));  ++ip; break;
                            case BytecodeOp::GreaterEqual: stack.push_back(Value::boolean(a >= b)); ++ip; break;
                            case BytecodeOp::Equal:        stack.push_back(Value::boolean(a == b)); ++ip; break;
                            case BytecodeOp::NotEqual:     stack.push_back(Value::boolean(a != b)); ++ip; break;
                            default:
                                handled_fast_path = false;
                                break;
                        }
                        if (handled_fast_path) {
                            continue;
                        }
                    }
                    TokenType op = TokenType::Plus;
                    switch (instruction.op) {
                        case BytecodeOp::Add:          op = TokenType::Plus; break;
                        case BytecodeOp::Subtract:     op = TokenType::Minus; break;
                        case BytecodeOp::Multiply:     op = TokenType::Star; break;
                        case BytecodeOp::Divide:       op = TokenType::Slash; break;
                        case BytecodeOp::Modulo:       op = TokenType::Percent; break;
                        case BytecodeOp::Equal:        op = TokenType::EqualEqual; break;
                        case BytecodeOp::NotEqual:     op = TokenType::BangEqual; break;
                        case BytecodeOp::Less:         op = TokenType::Less; break;
                        case BytecodeOp::LessEqual:    op = TokenType::LessEqual; break;
                        case BytecodeOp::Greater:      op = TokenType::Greater; break;
                        case BytecodeOp::GreaterEqual: op = TokenType::GreaterEqual; break;
                        default: break;
                    }
                    const Span span = instruction_span(instruction);
                    ZEPHYR_TRY_ASSIGN(binary_result, apply_binary_op(op, left, right, span, module.name));
                    stack.push_back(binary_result);
                    ++ip;
                    continue;
                }
                case BytecodeOp::Jump:
                    ip = static_cast<std::size_t>(instruction.operand);
                    continue;
                case BytecodeOp::JumpIfFalse: {
                    if (stack.empty()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    if (!is_truthy(stack.back())) {
                        ip = static_cast<std::size_t>(instruction.operand);
                    } else {
                        ++ip;
                    }
                    continue;
                }
                case BytecodeOp::JumpIfFalsePop: {
                    if (stack.empty()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    if (!is_truthy(stack.back())) {
                        stack.pop_back();
                        ip = static_cast<std::size_t>(instruction.operand);
                    } else {
                        stack.pop_back();
                        ++ip;
                    }
                    continue;
                }
                case BytecodeOp::JumpIfTrue: {
                    if (stack.empty()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    if (is_truthy(stack.back())) {
                        ip = static_cast<std::size_t>(instruction.operand);
                    } else {
                        ++ip;
                    }
                    continue;
                }
                case BytecodeOp::Return: {
                    if (stack.empty()) {
                        return make_loc_error<Value>(module.name, instruction_span(instruction), "Bytecode stack underflow.");
                    }
                    return stack.back();
                }
                default:
                    break;
            }
        }
        if (dap_active_) {
            check_breakpoint(ip, instruction.span_line, module.name);
        }
        const LazyInstructionMetadata lazy_metadata{chunk.metadata, ip};
        const LazyInstructionSpan lazy_span{instruction};
#define metadata (*lazy_metadata)
#define span lazy_span
        switch (instruction.op) {
            case BytecodeOp::LoadConst: {
                ZEPHYR_TRY_ASSIGN(value, load_bytecode_constant(chunk, instruction.operand));
                stack.push_back(value);
                ++ip;
                break;
            }
            case BytecodeOp::LoadLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                stack.push_back(read_local_value(slot));
                ++ip;
                break;
            }
            case BytecodeOp::SILoadLocalLoadLocal: {
                ZEPHYR_TRY_ASSIGN(first_slot, local_slot_index(unpack_si_local_pair_first(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(second_slot, local_slot_index(unpack_si_local_pair_second(instruction.operand), span));
                stack.push_back(read_local_value(first_slot));
                stack.push_back(read_local_value(second_slot));
                ++ip;
                break;
            }
            case BytecodeOp::LoadUpvalue: {
                ZEPHYR_TRY_ASSIGN(slot, upvalue_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, read_upvalue_value(slot));
                stack.push_back(value);
                ++ip;
                break;
            }
            case BytecodeOp::LoadName: {
                if (instruction.operand >= 0 && static_cast<std::size_t>(instruction.operand) < chunk.global_names.size()) {
                    ZEPHYR_TRY_ASSIGN(slot, global_slot_index(instruction.operand, span));
                    ZEPHYR_TRY_ASSIGN(value, read_global_value(slot, span));
                    stack.push_back(value);
                    ++ip;
                    break;
                }
                ZEPHYR_TRY_ASSIGN(value, lookup_value(current_env, metadata.string_operand, span, module.name, chunk.global_names));
                stack.push_back(value);
                ++ip;
                break;
            }
            case BytecodeOp::DefineLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY(enforce_type(value, metadata.type_name, span, module.name, "let binding"));
                locals[slot] = value;
                if (!lightweight) {
                    define_value(current_env, metadata.string_operand, value, metadata.flag, metadata.type_name);
                    resolve_local_binding(slot, current_env);
                }
                ++ip;
                break;
            }
            case BytecodeOp::DefineName: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY(enforce_type(value, metadata.type_name, span, module.name, "let binding"));
                define_value(current_env, metadata.string_operand, value, metadata.flag, metadata.type_name);
                if (instruction.operand >= 0 && static_cast<std::size_t>(instruction.operand) < chunk.global_names.size()) {
                    ZEPHYR_TRY_ASSIGN(slot, global_slot_index(instruction.operand, span));
                    resolve_global_binding(slot, current_env);
                }
                ++ip;
                break;
            }
            case BytecodeOp::BindPattern: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(matched, bind_pattern(current_env, value, metadata.pattern, module.name));
                if (matched && !lightweight) {
                    const std::size_t count = std::min(metadata.names.size(), metadata.jump_table.size());
                    for (std::size_t binding_index = 0; binding_index < count; ++binding_index) {
                        ZEPHYR_TRY_ASSIGN(slot, local_slot_index(metadata.jump_table[binding_index], span));
                        const auto it = current_env->values.find(metadata.names[binding_index]);
                        if (it == current_env->values.end()) {
                            return make_loc_error<Value>(module.name, span, "Missing bound pattern variable '" + metadata.names[binding_index] + "'.");
                        }
                        locals[slot] = read_binding_value(it->second);
                        resolve_local_binding(slot, current_env);
                    }
                }
                stack.push_back(Value::boolean(matched));
                ++ip;
                break;
            }
            case BytecodeOp::StoreLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                if (lightweight) {
                    // Direct store — no Environment binding to sync
                } else if (!ensure_local_binding(slot, current_env)) {
                    ZEPHYR_TRY(assign_value(current_env, metadata.string_operand, value, span, module.name));
                    resolve_local_binding(slot, current_env);
                } else {
                    ZEPHYR_TRY(assign_cached_binding(local_bindings[slot], local_binding_owners[slot], value, span,
                                                    metadata.string_operand));
                }
                locals[slot] = value;
                stack.push_back(value);
                ++ip;
                break;
            }
            case BytecodeOp::SIAddStoreLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(right, pop_value(span));
                ZEPHYR_TRY_ASSIGN(left, pop_value(span));
                Value stored_value;
                if (left.is_int() && right.is_int()) {
                    std::int64_t int_result = 0;
                    if (try_add_int48(left.as_int(), right.as_int(), int_result)) {
                        stored_value = Value::integer(int_result);
                    } else {
                        ZEPHYR_TRY_ASSIGN(computed_value, apply_binary_op(TokenType::Plus, left, right, span, module.name));
                        stored_value = computed_value;
                    }
                } else {
                    ZEPHYR_TRY_ASSIGN(computed_value, apply_binary_op(TokenType::Plus, left, right, span, module.name));
                    stored_value = computed_value;
                }
                if (lightweight) {
                    // Direct store — no Environment binding to sync
                } else if (!ensure_local_binding(slot, current_env)) {
                    ZEPHYR_TRY(assign_value(current_env, metadata.string_operand, stored_value, span, module.name));
                    resolve_local_binding(slot, current_env);
                } else {
                    ZEPHYR_TRY(assign_cached_binding(local_bindings[slot], local_binding_owners[slot], stored_value, span,
                                                    metadata.string_operand));
                }
                locals[slot] = stored_value;
                stack.push_back(stored_value);
                ++ip;
                break;
            }
            case BytecodeOp::SILoadLocalAdd: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(right, pop_value(span));
                const Value left_value = read_local_value(slot);
                if (left_value.is_int() && right.is_int()) {
                    std::int64_t int_result = 0;
                    if (try_add_int48(left_value.as_int(), right.as_int(), int_result)) {
                        stack.push_back(Value::integer(int_result));
                        ++ip;
                        break;
                    }
                }
                ZEPHYR_TRY_ASSIGN(result, apply_binary_op(TokenType::Plus, left_value, right, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::SILoadLocalAddStoreLocal: {
                ZEPHYR_TRY_ASSIGN(first_slot, local_slot_index(unpack_si_local_triple_first(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(second_slot, local_slot_index(unpack_si_local_triple_second(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(unpack_si_local_triple_destination(instruction.operand), span));
                const Value left_value = read_local_value(first_slot);
                const Value right_value = read_local_value(second_slot);
                Value stored_value;
                if (left_value.is_int() && right_value.is_int()) {
                    std::int64_t int_result = 0;
                    if (try_add_int48(left_value.as_int(), right_value.as_int(), int_result)) {
                        stored_value = Value::integer(int_result);
                    } else {
                        ZEPHYR_TRY_ASSIGN(computed_value, apply_binary_op(TokenType::Plus, left_value, right_value, span, module.name));
                        stored_value = computed_value;
                    }
                } else {
                    ZEPHYR_TRY_ASSIGN(computed_value, apply_binary_op(TokenType::Plus, left_value, right_value, span, module.name));
                    stored_value = computed_value;
                }
                if (lightweight) {
                    // Direct store — no Environment binding to sync
                } else if (!ensure_local_binding(slot, current_env)) {
                    ZEPHYR_TRY(assign_value(current_env, metadata.string_operand, stored_value, span, module.name));
                    resolve_local_binding(slot, current_env);
                } else {
                    ZEPHYR_TRY(assign_cached_binding(local_bindings[slot], local_binding_owners[slot], stored_value, span,
                                                    metadata.string_operand));
                }
                locals[slot] = stored_value;
                stack.push_back(stored_value);
                ++ip;
                break;
            }
            case BytecodeOp::SILoadLocalConstAddStoreLocal: {
                ZEPHYR_TRY_ASSIGN(source_slot, local_slot_index(unpack_si_local_const_local_source(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(unpack_si_local_const_local_destination(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(constant_value, load_bytecode_constant(chunk, unpack_si_local_const_local_constant(instruction.operand)));
                const Value left_value = read_local_value(source_slot);
                Value stored_value;
                if (left_value.is_int() && constant_value.is_int()) {
                    std::int64_t int_result = 0;
                    if (try_add_int48(left_value.as_int(), constant_value.as_int(), int_result)) {
                        stored_value = Value::integer(int_result);
                    } else {
                        ZEPHYR_TRY_ASSIGN(computed_value,
                                          apply_binary_op(TokenType::Plus, left_value, constant_value, span, module.name));
                        stored_value = computed_value;
                    }
                } else {
                    ZEPHYR_TRY_ASSIGN(computed_value,
                                      apply_binary_op(TokenType::Plus, left_value, constant_value, span, module.name));
                    stored_value = computed_value;
                }
                if (lightweight) {
                    // Direct store — no Environment binding to sync
                } else if (!ensure_local_binding(slot, current_env)) {
                    ZEPHYR_TRY(assign_value(current_env, metadata.string_operand, stored_value, span, module.name));
                    resolve_local_binding(slot, current_env);
                } else {
                    ZEPHYR_TRY(assign_cached_binding(local_bindings[slot], local_binding_owners[slot], stored_value, span,
                                                    metadata.string_operand));
                }
                locals[slot] = stored_value;
                stack.push_back(stored_value);
                ++ip;
                break;
            }
            case BytecodeOp::SILoadLocalLocalConstModulo: {
                ZEPHYR_TRY_ASSIGN(first_slot, local_slot_index(unpack_si_local_local_const_first(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(second_slot, local_slot_index(unpack_si_local_local_const_second(instruction.operand), span));
                ZEPHYR_TRY_ASSIGN(constant_value, load_bytecode_constant(chunk, unpack_si_local_local_const_constant(instruction.operand)));
                const Value first_value = read_local_value(first_slot);
                const Value second_value = read_local_value(second_slot);
                if (second_value.is_int() && constant_value.is_int()) {
                    stack.push_back(first_value);
                    stack.push_back(Value::integer(second_value.as_int() % constant_value.as_int()));
                    ++ip;
                    break;
                }
                ZEPHYR_TRY_ASSIGN(modulo_value, apply_binary_op(TokenType::Percent, second_value, constant_value, span, module.name));
                stack.push_back(first_value);
                stack.push_back(modulo_value);
                ++ip;
                break;
            }
            case BytecodeOp::StoreUpvalue: {
                ZEPHYR_TRY_ASSIGN(slot, upvalue_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                auto* cell = (*captured_upvalues)[slot];
                if (cell == nullptr) {
                    return make_loc_error<Value>(module.name, span, "Missing captured upvalue cell.");
                }
                if (!cell->mutable_value) {
                    return make_loc_error<Value>(module.name, span,
                                                 "Cannot assign to immutable captured binding '" + metadata.string_operand + "'.");
                }
                ZEPHYR_TRY(enforce_type(value, cell->type_name, span, module.name, "assignment"));
                const std::string capture_context = cell->container_kind == HandleContainerKind::CoroutineFrame
                                                       ? "coroutine capture assignment"
                                                       : "closure capture assignment";
                ZEPHYR_TRY(validate_handle_store(value, cell->container_kind, span, module.name, capture_context));
                cell->value = value;
                note_write(static_cast<GcObject*>(cell), value);
                stack.push_back(value);
                ++ip;
                break;
            }
            case BytecodeOp::StoreName: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                if (instruction.operand >= 0 && static_cast<std::size_t>(instruction.operand) < chunk.global_names.size()) {
                    ZEPHYR_TRY_ASSIGN(slot, global_slot_index(instruction.operand, span));
                    if (!ensure_global_binding(slot, current_env)) {
                        ZEPHYR_TRY(assign_value(current_env, metadata.string_operand, value, span, module.name));
                        resolve_global_binding(slot, current_env);
                    } else {
                        ZEPHYR_TRY(assign_cached_binding(global_bindings[slot], global_binding_owners[slot], value,
                                                        span, metadata.string_operand));
                    }
                } else {
                    ZEPHYR_TRY(assign_value(current_env, metadata.string_operand, value, span, module.name));
                }
                stack.push_back(value);
                ++ip;
                break;
            }
            case BytecodeOp::Pop: {
                if (stack.empty()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                stack.pop_back();
                ++ip;
                break;
            }
            case BytecodeOp::Not: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, apply_unary_op(TokenType::Bang, value, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::Negate: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, apply_unary_op(TokenType::Minus, value, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::ToBool: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                stack.push_back(Value::boolean(is_truthy(value)));
                ++ip;
                break;
            }
            case BytecodeOp::Stringify: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                stack.push_back(make_string(value_to_string(value)));
                ++ip;
                break;
            }
            case BytecodeOp::Add:
            case BytecodeOp::Subtract:
            case BytecodeOp::Multiply:
            case BytecodeOp::Divide:
            case BytecodeOp::Modulo:
            case BytecodeOp::Equal:
            case BytecodeOp::NotEqual:
            case BytecodeOp::Less:
            case BytecodeOp::LessEqual:
            case BytecodeOp::Greater:
            case BytecodeOp::GreaterEqual: {
                ZEPHYR_TRY_ASSIGN(right, pop_value(span));
                ZEPHYR_TRY_ASSIGN(left, pop_value(span));
                // ── Integer fast paths (avoids apply_binary_op dispatch overhead) ──
                if (left.is_int() && right.is_int()) {
                    const int64_t a = left.as_int(), b = right.as_int();
                    std::int64_t int_result = 0;
                    bool handled_fast_path = true;
                    switch (instruction.op) {
                        case BytecodeOp::Add:
                            if (try_add_int48(a, b, int_result)) { stack.push_back(Value::integer(int_result)); ++ip; }
                            else { handled_fast_path = false; }
                            break;
                        case BytecodeOp::Subtract:
                            if (try_sub_int48(a, b, int_result)) { stack.push_back(Value::integer(int_result)); ++ip; }
                            else { handled_fast_path = false; }
                            break;
                        case BytecodeOp::Multiply:
                            if (try_mul_int48(a, b, int_result)) { stack.push_back(Value::integer(int_result)); ++ip; }
                            else { handled_fast_path = false; }
                            break;
                        case BytecodeOp::Modulo:
                            stack.push_back(Value::integer(a % b));
                            ++ip;
                            break;
                        case BytecodeOp::Less:         stack.push_back(Value::boolean(a < b));  ++ip; break;
                        case BytecodeOp::LessEqual:    stack.push_back(Value::boolean(a <= b)); ++ip; break;
                        case BytecodeOp::Greater:      stack.push_back(Value::boolean(a > b));  ++ip; break;
                        case BytecodeOp::GreaterEqual: stack.push_back(Value::boolean(a >= b)); ++ip; break;
                        case BytecodeOp::Equal:        stack.push_back(Value::boolean(a == b)); ++ip; break;
                        case BytecodeOp::NotEqual:     stack.push_back(Value::boolean(a != b)); ++ip; break;
                        default:
                            handled_fast_path = false;
                            break;
                    }
                    if (handled_fast_path) {
                        break;
                    }
                }
                // ── General path ──
                TokenType op = TokenType::Plus;
                switch (instruction.op) {
                    case BytecodeOp::Add:          op = TokenType::Plus; break;
                    case BytecodeOp::Subtract:     op = TokenType::Minus; break;
                    case BytecodeOp::Multiply:     op = TokenType::Star; break;
                    case BytecodeOp::Divide:       op = TokenType::Slash; break;
                    case BytecodeOp::Modulo:       op = TokenType::Percent; break;
                    case BytecodeOp::Equal:        op = TokenType::EqualEqual; break;
                    case BytecodeOp::NotEqual:     op = TokenType::BangEqual; break;
                    case BytecodeOp::Less:         op = TokenType::Less; break;
                    case BytecodeOp::LessEqual:    op = TokenType::LessEqual; break;
                    case BytecodeOp::Greater:      op = TokenType::Greater; break;
                    case BytecodeOp::GreaterEqual: op = TokenType::GreaterEqual; break;
                    default: break;
                }
                ZEPHYR_TRY_ASSIGN(result, apply_binary_op(op, left, right, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::BuildArray: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > stack.size()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                auto* array = allocate<ArrayObject>();
                array->elements.resize(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    ZEPHYR_TRY(validate_handle_store(stack.back(), HandleContainerKind::ArrayElement, span, module.name,
                                                     "array literal"));
                    array->elements[static_cast<std::size_t>(index)] = stack.back();
                    note_array_element_write(array, static_cast<std::size_t>(index), array->elements[static_cast<std::size_t>(index)]);
                    stack.pop_back();
                }
                stack.push_back(Value::object(array));
                ++ip;
                break;
            }
            case BytecodeOp::ArrayLength: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                if (!value.is_object() || value.as_object()->kind != ObjectKind::Array) {
                    return make_loc_error<Value>(module.name, span, "for-in expects Array.");
                }
                auto* array = static_cast<ArrayObject*>(value.as_object());
                stack.push_back(Value::integer(static_cast<std::int64_t>(array->elements.size())));
                ++ip;
                break;
            }
            case BytecodeOp::IterHasNext: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(iterable, pop_value(span));
                if (iterable.is_object() && iterable.as_object()->kind == ObjectKind::Array) {
                    auto* arr = static_cast<ArrayObject*>(iterable.as_object());
                    const bool has = index.as_int() < static_cast<std::int64_t>(arr->elements.size());
                    stack.push_back(iterable);
                    stack.push_back(index);
                    stack.push_back(Value::boolean(has));
                } else {
                    stack.push_back(iterable);
                    stack.push_back(index);
                    ZEPHYR_TRY_ASSIGN(result, call_member_value(iterable, "has_next", {}, span, module.name));
                    stack.push_back(result);
                }
                ++ip;
                break;
            }
            case BytecodeOp::IterNext: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(iterable, pop_value(span));
                if (iterable.is_object() && iterable.as_object()->kind == ObjectKind::Array) {
                    auto* arr = static_cast<ArrayObject*>(iterable.as_object());
                    const Value elem = arr->elements[static_cast<std::size_t>(index.as_int())];
                    stack.push_back(iterable);
                    stack.push_back(Value::integer(index.as_int() + 1));
                    stack.push_back(elem);
                } else {
                    ZEPHYR_TRY_ASSIGN(elem, call_member_value(iterable, "next", {}, span, module.name));
                    stack.push_back(iterable);
                    stack.push_back(index);
                    stack.push_back(elem);
                }
                ++ip;
                break;
            }
            case BytecodeOp::BuildStruct: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > stack.size() ||
                    static_cast<std::size_t>(instruction.operand) != metadata.names.size()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> field_values(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    field_values[static_cast<std::size_t>(index)] = stack.back();
                    stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(result, build_struct_value(current_env, metadata.string_operand, metadata.names, field_values, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::BuildEnum: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > stack.size()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> payload(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    payload[static_cast<std::size_t>(index)] = stack.back();
                    stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(result,
                                  build_enum_value(current_env, metadata.string_operand, metadata.type_name.value_or(std::string{}), payload,
                                                   span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::IsEnumVariant: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result,
                                  is_enum_variant_value(value, metadata.string_operand, metadata.type_name.value_or(std::string{}),
                                                        instruction.operand, span, module.name));
                stack.push_back(Value::boolean(result));
                ++ip;
                break;
            }
            case BytecodeOp::LoadEnumPayload: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, get_enum_payload_value(value, instruction.operand, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::LoadMember: {
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, load_member_value(object, instruction, metadata, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::StoreMember: {
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, store_member_value(object, value, instruction, metadata, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::LoadIndex: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, get_index_value(object, index, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::StoreIndex: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, set_index_value(object, index, value, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::Call: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand + 1) > stack.size()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> args(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    args[static_cast<std::size_t>(index)] = stack.back();
                    stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(callee, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, call_value(callee, args, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::CallMember: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand + 1) > stack.size()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> args(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    args[static_cast<std::size_t>(index)] = stack.back();
                    stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, call_member_value(object, metadata.string_operand, args, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::Jump:
                ip = static_cast<std::size_t>(instruction.operand);
                break;
            case BytecodeOp::JumpIfFalse:
                if (stack.empty()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                if (!is_truthy(stack.back())) {
                    ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    ++ip;
                }
                break;
            case BytecodeOp::SICmpJumpIfFalse: {
                ZEPHYR_TRY_ASSIGN(right, pop_value(span));
                ZEPHYR_TRY_ASSIGN(left, pop_value(span));
                if (left.is_int() && right.is_int()) {
                    const Value compare_value = Value::boolean(
                        evaluate_int_comparison_opcode(unpack_si_cmp_jump_compare_op(instruction.operand), left.as_int(), right.as_int()));
                    stack.push_back(compare_value);
                    if (!is_truthy(compare_value)) {
                        ip = static_cast<std::size_t>(unpack_si_cmp_jump_target(instruction.operand));
                    } else {
                        ++ip;
                    }
                    break;
                }
                ZEPHYR_TRY_ASSIGN(compare_result,
                                  apply_binary_op(token_type_for_bytecode_binary_op(unpack_si_cmp_jump_compare_op(instruction.operand)),
                                                  left,
                                                  right,
                                                  span,
                                                  module.name));
                stack.push_back(compare_result);
                if (!is_truthy(compare_result)) {
                    ip = static_cast<std::size_t>(unpack_si_cmp_jump_target(instruction.operand));
                } else {
                    ++ip;
                }
                break;
            }
            case BytecodeOp::JumpIfFalsePop:
                if (stack.empty()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                if (!is_truthy(stack.back())) {
                    stack.pop_back();
                    ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    stack.pop_back();
                    ++ip;
                }
                break;
            case BytecodeOp::JumpIfTrue:
                if (stack.empty()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                if (is_truthy(stack.back())) {
                    ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    ++ip;
                }
                break;
            case BytecodeOp::JumpIfNilKeep:
                if (stack.empty()) {
                    return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
                }
                if (stack.back().is_nil()) {
                    ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    ++ip;
                }
                break;
            case BytecodeOp::EnterScope: {
                if (!lightweight) {
                    auto* scope = allocate<Environment>(current_env);
                    scopes.enter(scope);
                }
                ++ip;
                break;
            }
            case BytecodeOp::ExitScope:
                if (!lightweight) {
                    ZEPHYR_TRY(scopes.exit_one());
                }
                ++ip;
                break;
            case BytecodeOp::ImportModule: {
                ZEPHYR_TRY_ASSIGN(imported,
                                  import_module(module.path.empty() ? std::filesystem::current_path() : module.path.parent_path(),
                                                metadata.string_operand));
                ZEPHYR_TRY(import_exports(current_env, *imported, metadata.type_name, metadata.names, module.name, span));
                ++ip;
                break;
            }
            case BytecodeOp::DeclareFunction: {
                const auto cached_params = metadata.stmt != nullptr ? static_cast<FunctionDecl*>(metadata.stmt)->params
                                                                    : cached_params_from_metadata(metadata, instruction.span_line);
                const auto return_type = metadata.stmt != nullptr
                                             ? static_cast<FunctionDecl*>(metadata.stmt)->return_type
                                             : cached_return_type_from_metadata(metadata, instruction.span_line);
                auto* function_decl = static_cast<FunctionDecl*>(metadata.stmt);
                const std::string function_name =
                    function_decl != nullptr ? function_decl->name : metadata.string_operand;
                const auto generic_params = function_decl != nullptr ? function_decl->generic_params : std::vector<std::string>{};
                const auto where_clauses = function_decl != nullptr ? function_decl->where_clauses : std::vector<TraitBound>{};
                if (metadata.bytecode == nullptr && function_decl == nullptr) {
                    return make_loc_error<Value>(module.name, span, "Cached function metadata is incomplete.");
                }
                ZEPHYR_TRY_ASSIGN(function,
                                  create_script_function(function_name,
                                                         module.name,
                                                         cached_params,
                                                         return_type,
                                                         function_decl != nullptr ? function_decl->body.get() : nullptr,
                                                         current_env,
                                                          metadata.bytecode != nullptr
                                                              ? metadata.bytecode
                                                              : compile_bytecode_function(function_name, cached_params,
                                                                                          function_decl->body.get(), generic_params),
                                                          span,
                                                          generic_params,
                                                          where_clauses));
                Value function_value = Value::object(function);
                define_value(current_env, function_name, function_value, false, std::string("Function"));
                ++ip;
                break;
            }
            case BytecodeOp::DeclareStruct: {
                auto* struct_decl = static_cast<StructDecl*>(metadata.stmt);
                auto* type = allocate<StructTypeObject>(struct_decl->name);
                type->generic_params = struct_decl->generic_params;
                for (const auto& field : struct_decl->fields) {
                    type->fields.push_back(StructFieldSpec{field.name, field.type.display_name()});
                }
                define_value(current_env, struct_decl->name, Value::object(type), false);
                ++ip;
                break;
            }
            case BytecodeOp::DeclareEnum: {
                auto* enum_decl = static_cast<EnumDecl*>(metadata.stmt);
                auto* type = allocate<EnumTypeObject>(enum_decl->name);
                for (const auto& variant : enum_decl->variants) {
                    EnumVariantSpec spec;
                    spec.name = variant.name;
                    for (const auto& payload_type : variant.payload_types) {
                        spec.payload_types.push_back(payload_type.display_name());
                    }
                    type->variants.push_back(std::move(spec));
                }
                define_value(current_env, enum_decl->name, Value::object(type), false);
                ++ip;
                break;
            }
            case BytecodeOp::DeclareTrait: {
                auto* trait_decl = static_cast<TraitDecl*>(metadata.stmt);
                ZEPHYR_TRY(register_trait_decl(current_env, trait_decl, module));
                ++ip;
                break;
            }
            case BytecodeOp::DeclareImpl: {
                auto* impl_decl = static_cast<ImplDecl*>(metadata.stmt);
                ZEPHYR_TRY(register_impl_decl(current_env, impl_decl, module));
                ++ip;
                break;
            }
            case BytecodeOp::ExportName:
                if (module.namespace_object != nullptr &&
                    std::find(module.namespace_object->exports.begin(), module.namespace_object->exports.end(), metadata.string_operand) ==
                        module.namespace_object->exports.end()) {
                    module.namespace_object->exports.push_back(metadata.string_operand);
                }
                ++ip;
                break;
            case BytecodeOp::EvalAstExpr: {
                if constexpr (!kBytecodeAstFallbackEnabled) {
                    return make_loc_error<Value>(module.name, span, ast_fallback_disabled_message("Bytecode expression"));
                }
                ++ast_fallback_executions_;
                ZEPHYR_TRY_ASSIGN(result, evaluate(current_env, metadata.expr, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::MakeFunction: {
                const auto cached_params = metadata.expr != nullptr ? static_cast<FunctionExpr*>(metadata.expr)->params
                                                                    : cached_params_from_metadata(metadata, instruction.span_line);
                const auto return_type = metadata.expr != nullptr
                                             ? static_cast<FunctionExpr*>(metadata.expr)->return_type
                                             : cached_return_type_from_metadata(metadata, instruction.span_line);
                auto* function_expr = static_cast<FunctionExpr*>(metadata.expr);
                if (metadata.bytecode == nullptr && function_expr == nullptr) {
                    return make_loc_error<Value>(module.name, span, "Cached function literal metadata is incomplete.");
                }
                ZEPHYR_TRY_ASSIGN(function,
                                  create_script_function(metadata.string_operand.empty() ? "<anonymous>" : metadata.string_operand,
                                                         module.name,
                                                         cached_params,
                                                         return_type,
                                                         function_expr != nullptr ? function_expr->body.get() : nullptr,
                                                         current_env,
                                                          metadata.bytecode != nullptr
                                                              ? metadata.bytecode
                                                              : compile_bytecode_function("<anonymous>", cached_params,
                                                                                          function_expr->body.get(), std::vector<std::string>{}),
                                                          span,
                                                          std::vector<std::string>{}));
                stack.push_back(Value::object(function));
                ++ip;
                break;
            }
            case BytecodeOp::MakeCoroutine: {
                const auto cached_params = metadata.expr != nullptr ? static_cast<CoroutineExpr*>(metadata.expr)->params
                                                                    : cached_params_from_metadata(metadata, instruction.span_line);
                const auto return_type = metadata.expr != nullptr
                                             ? static_cast<CoroutineExpr*>(metadata.expr)->return_type
                                             : cached_return_type_from_metadata(metadata, instruction.span_line);
                auto* coroutine_expr = static_cast<CoroutineExpr*>(metadata.expr);
                if (!cached_params.empty()) {
                    return make_loc_error<Value>(module.name, span,
                                                 "coroutine fn expressions do not support parameters yet.");
                }
                ZEPHYR_TRY(ensure_capture_cells(current_env, HandleContainerKind::CoroutineFrame, span, module.name));
                if (metadata.bytecode == nullptr && coroutine_expr == nullptr) {
                    return make_loc_error<Value>(module.name, span, "Cached coroutine metadata is incomplete.");
                }
                auto bytecode = metadata.bytecode != nullptr ? metadata.bytecode
                                                              : compile_bytecode_function("<coroutine>", coroutine_expr->params,
                                                                                          coroutine_expr->body.get());
                ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(bytecode.get(), span, module.name, "Coroutine expression"));
                auto* coroutine = allocate<CoroutineObject>(
                    module.name,
                    select_closure_environment(current_env, bytecode),
                    bytecode,
                    return_type.has_value() ? std::optional<std::string>(return_type->display_name()) : std::nullopt);
                ensure_coroutine_trace_id(coroutine);
                record_coroutine_trace_event(CoroutineTraceEvent::Type::Created, coroutine);
                coroutine->frames.front().global_resolution_env = module_or_root_environment(coroutine->frames.front().closure);
                if (coroutine->frames.front().bytecode != nullptr) {
                    ZEPHYR_TRY_ASSIGN(captured_cells,
                                      capture_upvalue_cells(current_env,
                                                            coroutine->frames.front().bytecode->upvalue_names,
                                                            HandleContainerKind::CoroutineFrame,
                                                            span,
                                                            module.name));
                    coroutine->frames.front().captured_upvalues = std::move(captured_cells);
                }
                stack.push_back(Value::object(coroutine));
                ++ip;
                break;
            }
            case BytecodeOp::Resume: {
                ZEPHYR_TRY_ASSIGN(target, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, resume_coroutine_value(target, span, module.name));
                stack.push_back(result);
                ++ip;
                break;
            }
            case BytecodeOp::ExecAstStmt: {
                if constexpr (!kBytecodeAstFallbackEnabled) {
                    return make_loc_error<Value>(module.name, span, ast_fallback_disabled_message("Bytecode statement"));
                }
                ++ast_fallback_executions_;
                ZEPHYR_TRY_ASSIGN(flow, execute(current_env, metadata.stmt, module));
                if (flow.kind == FlowSignal::Kind::Return) {
                    return flow.value;
                }
                if (flow.kind == FlowSignal::Kind::Break) {
                    return make_loc_error<Value>(module.name, span, "break escaped native bytecode loop.");
                }
                if (flow.kind == FlowSignal::Kind::Continue) {
                    return make_loc_error<Value>(module.name, span, "continue escaped native bytecode loop.");
                }
                ++ip;
                break;
            }
            case BytecodeOp::Yield:
                if (active_coroutines_.empty()) {
                    return make_loc_error<Value>(module.name, span, "yield used outside coroutine.");
                }
                return make_loc_error<Value>(module.name, span,
                                             "yield inside nested script function is not supported yet.");
            case BytecodeOp::Fail:
                return make_loc_error<Value>(module.name, span, metadata.string_operand.empty() ? "Bytecode execution failed." : metadata.string_operand);
            case BytecodeOp::MatchFail:
                return make_loc_error<Value>(
                    module.name,
                    span,
                    metadata.string_operand.empty() ? "Match expression is not exhaustive. hint: match may not cover all cases."
                                                    : metadata.string_operand);
            case BytecodeOp::Return: {
                ZEPHYR_TRY_ASSIGN(result, pop_value(span));
                return result;
            }
            // Phase 1.3: hint to MSVC that all opcodes are covered — enables better
            // branch prediction and eliminates default-case bounds checking.
#if defined(_MSC_VER)
            default: __assume(0);
#else
            default: __builtin_unreachable();
#endif
        }
#undef span
#undef metadata
    }

    if (stack.empty()) {
        return Value::nil();
    }
    return stack.back();
}

VoidResult Runtime::push_coroutine_script_frame(CoroutineObject* coroutine, ScriptFunctionObject* function, const std::vector<Value>& args,
                                                const Span& call_span, const std::string& module_name) {
    if (function->params.size() != args.size()) {
        return make_loc_error<std::monostate>(module_name, call_span,
                                              "Function '" + function->name + "' received wrong argument count.");
    }
    if (function->bytecode == nullptr) {
        return make_loc_error<std::monostate>(module_name, call_span, "Nested AST-only script functions are not suspendable yet.");
    }

    const std::string execution_module = function->module_name.empty() ? module_name : function->module_name;
    auto* call_env = allocate<Environment>(function->closure);

    CoroutineFrameState frame;
    frame.module_name = execution_module;
    frame.closure = function->closure;
    frame.root_env = call_env;
    frame.current_env = call_env;
    frame.global_resolution_env = module_or_root_environment(call_env);
    frame.bytecode = function->bytecode;
    frame.return_type_name =
        function->return_type.has_value() ? std::optional<std::string>(function->return_type->display_name()) : std::nullopt;
    frame.captured_upvalues = function->captured_upvalues;
    frame.locals.assign(static_cast<std::size_t>(std::max(function->bytecode->local_count, 0)), Value::nil());
    frame.local_binding_owners.assign(frame.locals.size(), nullptr);
    frame.local_bindings.assign(frame.locals.size(), nullptr);
    frame.local_binding_versions.assign(frame.locals.size(), 0);
    frame.global_binding_owners.assign(function->bytecode->global_names.size(), nullptr);
    frame.global_bindings.assign(function->bytecode->global_names.size(), nullptr);
    frame.global_binding_versions.assign(function->bytecode->global_names.size(), 0);
    frame.uses_register_mode = function->bytecode->uses_register_mode;
    frame.ip_index = 0;
    if (frame.uses_register_mode) {
        frame.regs.assign(static_cast<std::size_t>(std::max(function->bytecode->max_regs, function->bytecode->local_count)), Value::nil());
    }
    install_upvalue_bindings(call_env, *function->bytecode, function->captured_upvalues);

    for (std::size_t i = 0; i < function->params.size(); ++i) {
        const auto& param = function->params[i];
        const bool is_generic = param.type.has_value() &&
            std::find(function->generic_params.begin(), function->generic_params.end(), param.type->display_name()) != function->generic_params.end();
        const std::optional<std::string> type_name =
            (param.type.has_value() && !is_generic) ? std::optional<std::string>(param.type->display_name()) : std::nullopt;
        ZEPHYR_TRY(enforce_type(args[i], type_name, param.span, execution_module, "parameter"));
        define_value(call_env, param.name, args[i], true, type_name);
        if (i < frame.locals.size()) {
            frame.locals[i] = args[i];
        }
        if (frame.uses_register_mode && i < frame.regs.size()) {
            frame.regs[i] = args[i];
        }
    }

    if (function->bytecode != nullptr && !function->bytecode->uses_only_locals_and_upvalues) {
        refresh_coroutine_locals_from_env(frame);
    }

    coroutine->frames.push_back(std::move(frame));
    return ok_result();
}

void Runtime::refresh_coroutine_locals_from_env(CoroutineFrameState& frame) {
    if (frame.bytecode == nullptr || frame.current_env == nullptr) {
        return;
    }
    if (frame.local_binding_owners.size() != frame.locals.size()) {
        frame.local_binding_owners.assign(frame.locals.size(), nullptr);
    } else {
        std::fill(frame.local_binding_owners.begin(), frame.local_binding_owners.end(), nullptr);
    }
    if (frame.local_bindings.size() != frame.locals.size()) {
        frame.local_bindings.assign(frame.locals.size(), nullptr);
    } else {
        std::fill(frame.local_bindings.begin(), frame.local_bindings.end(), nullptr);
    }
    if (frame.local_binding_versions.size() != frame.locals.size()) {
        frame.local_binding_versions.assign(frame.locals.size(), 0);
    } else {
        std::fill(frame.local_binding_versions.begin(), frame.local_binding_versions.end(), 0);
    }
    const std::size_t global_count = frame.bytecode->global_names.size();
    if (frame.global_binding_owners.size() != global_count) {
        frame.global_binding_owners.assign(global_count, nullptr);
    } else {
        std::fill(frame.global_binding_owners.begin(), frame.global_binding_owners.end(), nullptr);
    }
    if (frame.global_bindings.size() != global_count) {
        frame.global_bindings.assign(global_count, nullptr);
    } else {
        std::fill(frame.global_bindings.begin(), frame.global_bindings.end(), nullptr);
    }
    if (frame.global_binding_versions.size() != global_count) {
        frame.global_binding_versions.assign(global_count, 0);
    } else {
        std::fill(frame.global_binding_versions.begin(), frame.global_binding_versions.end(), 0);
    }
    const std::size_t count = std::min(frame.locals.size(), frame.bytecode->local_names.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (frame.bytecode->local_names[i].empty()) {
            continue;
        }
        for (Environment* current = frame.current_env; current != nullptr; current = current->parent) {
            auto it = current->values.find(frame.bytecode->local_names[i]);
            if (it != current->values.end()) {
                frame.local_binding_owners[i] = current;
                frame.local_bindings[i] = &it->second;
                frame.local_binding_versions[i] = current->version;
                frame.locals[i] = read_binding_value(it->second);
                break;
            }
        }
    }
}

void Runtime::register_suspended_coroutine(CoroutineObject* coroutine) {
    if (coroutine == nullptr || coroutine->handle_retained) {
        return;
    }
    suspended_coroutines_.insert(coroutine); // unordered_set ignores duplicates automatically
}

void Runtime::unregister_suspended_coroutine(CoroutineObject* coroutine) {
    if (coroutine == nullptr || coroutine->handle_retained) {
        return;
    }
    suspended_coroutines_.erase(coroutine);
}

void Runtime::compact_suspended_coroutine(CoroutineObject* coroutine) {
    if (coroutine == nullptr) {
        return;
    }

    // Skip shrink-to-fit for retained coroutines actively being resumed; they
    // will be resumed soon and the alloc/dealloc churn hurts more than it helps.
    if (!coroutine->handle_retained) {
        std::size_t compacted_frames = 0;
        std::size_t capacity_saved = 0;
        for (auto& frame : coroutine->frames) {
            capacity_saved += compact_vector_storage(frame.stack);
            capacity_saved += compact_vector_storage(frame.locals);
            capacity_saved += compact_vector_storage(frame.scope_stack);
            capacity_saved += compact_vector_storage(frame.local_binding_owners);
            capacity_saved += compact_vector_storage(frame.local_bindings);
            capacity_saved += compact_vector_storage(frame.local_binding_versions);
            capacity_saved += compact_vector_storage(frame.global_binding_owners);
            capacity_saved += compact_vector_storage(frame.global_bindings);
            capacity_saved += compact_vector_storage(frame.global_binding_versions);
            capacity_saved += compact_vector_storage(frame.stack_cards);
            capacity_saved += compact_vector_storage(frame.local_cards);
            if (frame.uses_register_mode) {
                capacity_saved += compact_vector_storage(frame.regs);
                capacity_saved += compact_vector_storage(frame.reg_cards);
            }
            ++compacted_frames;
        }
        ++coroutine_compactions_;
        coroutine_compacted_frames_ += compacted_frames;
        coroutine_compacted_capacity_ += capacity_saved;
    }

    rebuild_coroutine_cards(coroutine);
    if (has_direct_young_reference(coroutine)) {
        remember_minor_owner(coroutine);
    }
}

RuntimeResult<Runtime::CoroutineExecutionResult> Runtime::resume_nested_coroutine_frame(CoroutineObject* coroutine, ModuleRecord& module,
                                                                                        const Span& call_span) {
    if (coroutine->frames.size() <= 1) {
        return make_loc_error<CoroutineExecutionResult>(module.name, call_span, "No nested coroutine frame to resume.");
    }
    auto result = resume_coroutine_single_frame(coroutine, module, call_span);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->yielded) {
        return result;
    }

    coroutine->completed = false;
    coroutine->suspended = false;
    coroutine->frames.pop_back();
    return result;
}

RuntimeResult<Runtime::CoroutineExecutionResult>
Runtime::resume_register_coroutine_fast(CoroutineObject* coroutine, const Span& call_span) {
    if (coroutine->frames.empty()) {
        return make_loc_error<CoroutineExecutionResult>("", call_span, "Coroutine frame stack is empty.");
    }
    CoroutineFrameState* frame_ptr = &coroutine->frames.front();
    if (frame_ptr->bytecode == nullptr) {
        return make_loc_error<CoroutineExecutionResult>(frame_ptr->module_name, call_span, "Coroutine is missing bytecode.");
    }

    const BytecodeFunction& chunk = *frame_ptr->bytecode;
    const std::string& module_name = frame_ptr->module_name;

    Value* regs_ptr = (frame_ptr->reg_count > 0 && frame_ptr->regs.empty())
        ? frame_ptr->inline_regs
        : frame_ptr->regs.data();

    if (static_cast<int>(frame_ptr->spill_regs.size()) < chunk.spill_count) {
        frame_ptr->spill_regs.assign(static_cast<std::size_t>(chunk.spill_count), Value::nil());
    }
    Value* __restrict spill_ptr = frame_ptr->spill_regs.empty() ? nullptr : frame_ptr->spill_regs.data();

    const CompactInstruction* __restrict instrs_ptr = chunk.instructions.data();
    const InstructionMetadata* const metadata_ptr = chunk.metadata.data();
    const BytecodeConstant* __restrict constants_ptr = chunk.constants.data();
    const std::size_t instrs_count = chunk.instructions.size();
    std::size_t local_ip = frame_ptr->ip_index;
    std::size_t executed_steps = 0;

    auto resolve_global_env = [&]() -> Environment* {
        if (!chunk.global_slots_use_module_root_base) return frame_ptr->current_env;
        if (frame_ptr->global_resolution_env != nullptr) return frame_ptr->global_resolution_env;
        return module_or_root_environment(frame_ptr->current_env);
    };

    auto do_return = [&](const Value& ret_val) -> CoroutineExecutionResult {
        frame_ptr->ip_index = local_ip + 1;
        coroutine->suspended = false;
        unregister_suspended_coroutine(coroutine);
        coroutine->completed = true;
        coroutine->started = true;
        record_coroutine_completed(coroutine);
        frame_ptr->stack.clear();
        frame_ptr->locals.clear();
        frame_ptr->scope_stack.clear();
        frame_ptr->captured_upvalues.clear();
        frame_ptr->local_binding_owners.clear();
        frame_ptr->local_bindings.clear();
        frame_ptr->local_binding_versions.clear();
        frame_ptr->global_binding_owners.clear();
        frame_ptr->global_bindings.clear();
        frame_ptr->global_binding_versions.clear();
        frame_ptr->stack_cards.clear();
        frame_ptr->local_cards.clear();
        frame_ptr->regs.clear();
        frame_ptr->reg_cards.clear();
        frame_ptr->spill_regs.clear();
        frame_ptr->reg_count = 0;
        frame_ptr->current_env = nullptr;
        frame_ptr->root_env = nullptr;
        frame_ptr->ip = 0;
        frame_ptr->ip_index = 0;
        return CoroutineExecutionResult{false, ret_val, executed_steps};
    };

    while (local_ip < instrs_count) {
        if (gc_stress_enabled_) {
            maybe_run_gc_stress_safe_point();
            frame_ptr = &coroutine->frames.front();
            regs_ptr = (frame_ptr->reg_count > 0 && frame_ptr->regs.empty())
                ? frame_ptr->inline_regs : frame_ptr->regs.data();
        }
        const CompactInstruction& instr = instrs_ptr[local_ip];
        ++executed_steps;
        ++opcode_execution_count_;

        switch (instr.op) {
        case BytecodeOp::R_LOAD_CONST: {
            const int lc_idx = unpack_r_index_operand(instr.operand);
            const BytecodeConstant& bc = constants_ptr[static_cast<std::size_t>(lc_idx)];
            if (const auto* iv = std::get_if<std::int64_t>(&bc)) {
                regs_ptr[unpack_r_dst_operand(instr.operand)] = Value::integer(*iv);
            } else {
                ZEPHYR_TRY_ASSIGN(cv, load_bytecode_constant(chunk, lc_idx));
                regs_ptr[unpack_r_dst_operand(instr.operand)] = cv;
            }
            ++local_ip;
            break;
        }
        case BytecodeOp::R_LOAD_INT: {
            regs_ptr[unpack_r_load_int_dst(instr.operand)] =
                Value::integer(unpack_r_load_int_value(instr.operand));
            ++local_ip;
            break;
        }
        case BytecodeOp::R_ADDI: {
            const Value& addi_src = regs_ptr[unpack_r_addi_src(instr.operand)];
            const std::int64_t addi_imm = unpack_r_addi_imm(instr.operand);
            if (addi_src.is_int()) {
                const std::int64_t sum = addi_src.as_int() + addi_imm;
                if (sum >= Value::kIntMin && sum <= Value::kIntMax) {
                    regs_ptr[unpack_r_addi_dst(instr.operand)] = Value::integer(sum);
                    ++local_ip;
                    break;
                }
            }
            { const Span s = instruction_span(instr);
            const Value imm_val = Value::integer(addi_imm);
            ZEPHYR_TRY_ASSIGN(addi_r, apply_binary_op(TokenType::Plus, addi_src, imm_val, s, module_name));
            regs_ptr[unpack_r_addi_dst(instr.operand)] = addi_r; }
            ++local_ip;
            break;
        }
        case BytecodeOp::R_MODI: {
            const Value& modi_src = regs_ptr[unpack_r_modi_src(instr.operand)];
            const std::int64_t modi_imm = unpack_r_modi_imm(instr.operand);
            if (modi_src.is_int() && modi_imm > 0) {
                regs_ptr[unpack_r_modi_dst(instr.operand)] = Value::integer(modi_src.as_int() % modi_imm);
                ++local_ip;
                break;
            }
            { const Span s = instruction_span(instr);
            const Value imm_val = Value::integer(modi_imm);
            ZEPHYR_TRY_ASSIGN(modi_r, apply_binary_op(TokenType::Percent, modi_src, imm_val, s, module_name));
            regs_ptr[unpack_r_modi_dst(instr.operand)] = modi_r; }
            ++local_ip;
            break;
        }
        case BytecodeOp::R_ADDI_JUMP: {
            const Value& aj_src = regs_ptr[unpack_r_addi_src(instr.operand)];
            const std::int64_t aj_imm = unpack_r_addi_imm(instr.operand);
            if (aj_src.is_int()) {
                const std::int64_t aj_sum = aj_src.as_int() + aj_imm;
                if (aj_sum >= Value::kIntMin && aj_sum <= Value::kIntMax) {
                    regs_ptr[unpack_r_addi_dst(instr.operand)] = Value::integer(aj_sum);
                    local_ip = static_cast<std::size_t>(instr.ic_slot);
                    break;
                }
            }
            { const Span s = instruction_span(instr);
            const Value imm_val = Value::integer(aj_imm);
            ZEPHYR_TRY_ASSIGN(aj_r, apply_binary_op(TokenType::Plus, aj_src, imm_val, s, module_name));
            regs_ptr[unpack_r_addi_dst(instr.operand)] = aj_r; }
            local_ip = static_cast<std::size_t>(instr.ic_slot);
            break;
        }
        case BytecodeOp::R_SI_ADDI_CMPI_LT_JUMP: {
            const std::uint8_t acj_reg = unpack_r_si_acj_reg(instr.operand);
            const Value& acj_val = regs_ptr[acj_reg];
            const std::int64_t acj_addi = unpack_r_si_acj_addi(instr.operand);
            const std::int64_t acj_limit = unpack_r_si_acj_limit(instr.operand);
            if (acj_val.is_int()) {
                const std::int64_t acj_new = acj_val.as_int() + acj_addi;
                if (acj_new >= Value::kIntMin && acj_new <= Value::kIntMax) {
                    regs_ptr[acj_reg] = Value::integer(acj_new);
                    if (acj_new < acj_limit) { local_ip = static_cast<std::size_t>(instr.ic_slot); }
                    else { ++local_ip; }
                    break;
                }
            }
            { const Span s = instruction_span(instr);
            const Value acj_addi_val = Value::integer(acj_addi);
            ZEPHYR_TRY_ASSIGN(acj_new_val, apply_binary_op(TokenType::Plus, acj_val, acj_addi_val, s, module_name));
            regs_ptr[acj_reg] = acj_new_val;
            const Value acj_limit_val = Value::integer(acj_limit);
            ZEPHYR_TRY_ASSIGN(acj_cmp, apply_binary_op(TokenType::Less, acj_new_val, acj_limit_val, s, module_name));
            if (is_truthy(acj_cmp)) { local_ip = static_cast<std::size_t>(instr.ic_slot); } else { ++local_ip; } }
            break;
        }
        case BytecodeOp::R_SI_LOOP_STEP: {
            const std::int64_t ls_div   = static_cast<std::int64_t>(instr.operand_a);
            const std::int64_t ls_step  = static_cast<std::int64_t>(static_cast<std::int8_t>(instr.src1));
            const std::int64_t ls_limit = unpack_r_si_ls_limit(instr.ic_slot);
            const std::size_t  ls_body  = unpack_r_si_ls_body(instr.ic_slot);
            Value& ls_acc  = regs_ptr[instr.dst];
            Value& ls_iter = regs_ptr[instr.src2];
            if (ls_acc.is_int() && ls_iter.is_int()) {
                const std::int64_t ls_a = ls_acc.as_int();
                const std::int64_t ls_i = ls_iter.as_int();
                const std::int64_t ls_new_acc  = ls_a + (ls_i % ls_div);
                const std::int64_t ls_new_iter = ls_i + ls_step;
                if (ls_new_acc >= Value::kIntMin && ls_new_acc <= Value::kIntMax &&
                    ls_new_iter >= Value::kIntMin && ls_new_iter <= Value::kIntMax) {
                    ls_acc  = Value::integer(ls_new_acc);
                    ls_iter = Value::integer(ls_new_iter);
                    local_ip = (ls_new_iter < ls_limit) ? ls_body : local_ip + 1;
                    break;
                }
            }
            { const Span s = instruction_span(instr);
            const Value ls_div_val  = Value::integer(ls_div);
            ZEPHYR_TRY_ASSIGN(ls_mod, apply_binary_op(TokenType::Percent, ls_iter, ls_div_val, s, module_name));
            ZEPHYR_TRY_ASSIGN(ls_new_acc, apply_binary_op(TokenType::Plus, ls_acc, ls_mod, s, module_name));
            ls_acc = ls_new_acc;
            const Value ls_step_val = Value::integer(ls_step);
            ZEPHYR_TRY_ASSIGN(ls_new_iter, apply_binary_op(TokenType::Plus, ls_iter, ls_step_val, s, module_name));
            ls_iter = ls_new_iter;
            const Value ls_limit_val = Value::integer(ls_limit);
            ZEPHYR_TRY_ASSIGN(ls_cmp, apply_binary_op(TokenType::Less, ls_new_iter, ls_limit_val, s, module_name));
            local_ip = is_truthy(ls_cmp) ? ls_body : local_ip + 1; }
            break;
        }
        case BytecodeOp::R_LOAD_GLOBAL: {
            const int slot = unpack_r_index_operand(instr.operand);
            bool found = false;
            for (Environment* env = resolve_global_env(); env != nullptr; env = env->parent) {
                auto it = env->values.find(chunk.global_names[static_cast<std::size_t>(slot)]);
                if (it != env->values.end()) {
                    regs_ptr[unpack_r_dst_operand(instr.operand)] = read_binding_value(it->second);
                    found = true;
                    break;
                }
            }
            if (!found) {
                return make_loc_error<CoroutineExecutionResult>(
                    module_name, instruction_span(instr),
                    "Unknown identifier '" + chunk.global_names[static_cast<std::size_t>(slot)] + "'.");
            }
            ++local_ip;
            break;
        }
        case BytecodeOp::R_STORE_GLOBAL: {
            const Span span = instruction_span(instr);
            const int slot = unpack_r_index_operand(instr.operand);
            bool found = false;
            for (Environment* env = resolve_global_env(); env != nullptr; env = env->parent) {
                auto it = env->values.find(chunk.global_names[static_cast<std::size_t>(slot)]);
                if (it != env->values.end()) {
                    Binding* binding = &it->second;
                    if (!binding->mutable_value) {
                        return make_loc_error<CoroutineExecutionResult>(
                            module_name, span,
                            "Cannot assign to immutable binding '" + chunk.global_names[static_cast<std::size_t>(slot)] + "'.");
                    }
                    ZEPHYR_TRY(enforce_type(regs_ptr[instr.src1], binding->type_name, span, module_name, "assignment"));
                    ZEPHYR_TRY(validate_handle_store(regs_ptr[instr.src1], HandleContainerKind::Global, span, module_name, "global assignment"));
                    if (binding->cell != nullptr) {
                        ZEPHYR_TRY(validate_handle_store(regs_ptr[instr.src1], binding->cell->container_kind,
                                                          span, module_name, "coroutine capture assignment"));
                    }
                    write_binding_value(*binding, regs_ptr[instr.src1]);
                    note_write(env, regs_ptr[instr.src1]);
                    if (binding->cell != nullptr) {
                        note_write(static_cast<GcObject*>(binding->cell), regs_ptr[instr.src1]);
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                return make_loc_error<CoroutineExecutionResult>(
                    module_name, span,
                    "Unknown identifier '" + chunk.global_names[static_cast<std::size_t>(slot)] + "'.");
            }
            ++local_ip;
            break;
        }
        case BytecodeOp::R_MOVE: {
            regs_ptr[instr.dst] = regs_ptr[instr.src1];
            ++local_ip;
            break;
        }
        case BytecodeOp::R_ADD: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t ir = lv.as_int() + rv.as_int();
                if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(ir); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Plus, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_SUB: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t ir = lv.as_int() - rv.as_int();
                if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(ir); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Minus, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_MUL: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t ir = lv.as_int() * rv.as_int();
                if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(ir); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Star, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_DIV: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int() && rv.as_int() != 0) { regs_ptr[instr.dst] = Value::integer(lv.as_int() / rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Slash, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_MOD: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int() && rv.as_int() != 0) { regs_ptr[instr.dst] = Value::integer(lv.as_int() % rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Percent, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_LT: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) { regs_ptr[instr.dst] = Value::boolean(lv.as_int() < rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Less, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_LE: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) { regs_ptr[instr.dst] = Value::boolean(lv.as_int() <= rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::LessEqual, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_GT: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) { regs_ptr[instr.dst] = Value::boolean(lv.as_int() > rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Greater, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_GE: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) { regs_ptr[instr.dst] = Value::boolean(lv.as_int() >= rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::GreaterEqual, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_EQ: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) { regs_ptr[instr.dst] = Value::boolean(lv.as_int() == rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::EqualEqual, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_NE: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) { regs_ptr[instr.dst] = Value::boolean(lv.as_int() != rv.as_int()); ++local_ip; break; }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::BangEqual, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_NOT: {
            regs_ptr[instr.dst] = Value::boolean(!is_truthy(regs_ptr[instr.src1]));
            ++local_ip;
            break;
        }
        case BytecodeOp::R_NEG: {
            const Span s = instruction_span(instr);
            ZEPHYR_TRY_ASSIGN(r, apply_unary_op(TokenType::Minus, regs_ptr[instr.src1], s, module_name));
            regs_ptr[instr.dst] = r;
            ++local_ip;
            break;
        }
        case BytecodeOp::R_CALL: {
            const Span span = instruction_span(instr);
            const std::size_t dst = instr.dst;
            const Value callee_value = regs_ptr[instr.src1];
            std::vector<Value> args;
            args.reserve(instr.operand_a);
            for (std::uint8_t idx = 0; idx < instr.operand_a; ++idx) {
                args.push_back(regs_ptr[static_cast<std::size_t>(instr.src2 + idx)]);
            }
            if (callee_value.is_object() && callee_value.as_object()->kind == ObjectKind::ScriptFunction) {
                ++local_ip;
                frame_ptr->ip_index = local_ip;
                Environment* saved_root_env = frame_ptr->root_env;
                ModuleRecord temp_module;
                temp_module.name = module_name;
                temp_module.environment = saved_root_env;
                ZEPHYR_TRY(push_coroutine_script_frame(
                    coroutine, static_cast<ScriptFunctionObject*>(callee_value.as_object()), args, span, module_name));
                frame_ptr = &coroutine->frames.front();
                regs_ptr = (frame_ptr->reg_count > 0 && frame_ptr->regs.empty())
                    ? frame_ptr->inline_regs : frame_ptr->regs.data();
                ZEPHYR_TRY_ASSIGN(nested_result, resume_nested_coroutine_frame(coroutine, temp_module, span));
                executed_steps += nested_result.step_count;
                frame_ptr = &coroutine->frames.front();
                regs_ptr = (frame_ptr->reg_count > 0 && frame_ptr->regs.empty())
                    ? frame_ptr->inline_regs : frame_ptr->regs.data();
                if (nested_result.yielded) {
                    frame_ptr->pending_call_dst_reg = dst;
                    nested_result.step_count = executed_steps;
                    return nested_result;
                }
                regs_ptr[dst] = nested_result.value;
                break;
            }
            ZEPHYR_TRY_ASSIGN(result, call_value(callee_value, args, span, module_name));
            regs_ptr[dst] = result;
            ++local_ip;
            break;
        }
        case BytecodeOp::R_LOAD_MEMBER: {
            const Value& lm_obj = regs_ptr[instr.src1];
            const Span lm_span = instruction_span(instr);
            if (lm_obj.is_host_handle()) {
                ZEPHYR_TRY_ASSIGN(lm_res, resolve_host_handle(lm_obj, lm_span, module_name, "member access"));
                const ZephyrHostClass* lm_class = lm_res.entry->host_class.get();
                const ZephyrHostClass::Getter* lm_getter = nullptr;
                if (instr.ic_shape == reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(lm_class))) {
                    lm_getter = lm_class->get_getter_at(instr.ic_slot);
                } else {
                    std::uint32_t lm_idx = 0;
                    lm_getter = lm_class->find_getter_ic(metadata_ptr[local_ip].string_operand, lm_idx);
                    if (lm_getter != nullptr) {
                        instr.ic_shape = reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(lm_class));
                        instr.ic_slot = lm_idx;
                    }
                }
                if (lm_getter != nullptr) {
                    try {
                        regs_ptr[instr.dst] = from_public_value((*lm_getter)(lm_res.instance));
                    } catch (const std::exception& e) {
                        return make_loc_error<CoroutineExecutionResult>(module_name, lm_span, e.what());
                    }
                    ++local_ip; break;
                }
            }
            ZEPHYR_TRY_ASSIGN(lm_result, load_member_value(lm_obj, instr, metadata_ptr[local_ip], module_name));
            regs_ptr[instr.dst] = lm_result;
            ++local_ip; break;
        }
        case BytecodeOp::R_STORE_MEMBER: {
            const Value& sm_obj = regs_ptr[instr.src1];
            const Value& sm_val = regs_ptr[instr.src2];
            ZEPHYR_TRY_ASSIGN(sm_result, store_member_value(sm_obj, sm_val, instr, metadata_ptr[local_ip], module_name));
            (void)sm_result;
            ++local_ip; break;
        }
        case BytecodeOp::R_CALL_MEMBER: {
            const Value& cm_obj = regs_ptr[instr.src1];
            const Span cm_span = instruction_span(instr);
            if (cm_obj.is_host_handle()) {
                ZEPHYR_TRY_ASSIGN(cm_res, resolve_host_handle(cm_obj, cm_span, module_name, "method call"));
                const ZephyrHostClass* cm_class = cm_res.entry->host_class.get();
                const ZephyrHostClass::Method* cm_method = nullptr;
                if (instr.ic_shape == reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(cm_class))) {
                    cm_method = cm_class->get_method_at(instr.ic_slot);
                } else {
                    std::uint32_t cm_idx = 0;
                    cm_method = cm_class->find_method_ic(metadata_ptr[local_ip].string_operand, cm_idx);
                    if (cm_method != nullptr) {
                        instr.ic_shape = reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(cm_class));
                        instr.ic_slot = cm_idx;
                    }
                }
                if (cm_method != nullptr) {
                    const std::uint8_t cm_argc = instr.operand_a;
                    auto cm_lease = acquire_public_args_buffer(cm_argc);
                    auto& cm_public_args = cm_lease.args();
                    for (std::uint8_t i = 0; i < cm_argc; ++i) {
                        cm_public_args.push_back(to_public_value(regs_ptr[static_cast<std::size_t>(instr.src2 + i)]));
                    }
                    try {
                        regs_ptr[instr.dst] = from_public_value((*cm_method)(cm_res.instance, cm_public_args));
                    } catch (const std::exception& e) {
                        return make_loc_error<CoroutineExecutionResult>(module_name, cm_span, e.what());
                    }
                    ++local_ip; break;
                }
            }
            {
                std::vector<Value> cm_args;
                cm_args.reserve(instr.operand_a);
                for (std::uint8_t i = 0; i < instr.operand_a; ++i)
                    cm_args.push_back(regs_ptr[static_cast<std::size_t>(instr.src2 + i)]);
                ZEPHYR_TRY_ASSIGN(cm_result, call_member_value(cm_obj, metadata_ptr[local_ip].string_operand, cm_args, cm_span, module_name));
                regs_ptr[instr.dst] = cm_result;
            }
            ++local_ip; break;
        }
        case BytecodeOp::R_BUILD_STRUCT: {
            const std::uint8_t bs_count = instr.operand_a;
            // IC fast path
            if (instr.ic_shape != nullptr && instr.ic_slot == 1) {
                auto* bs_type = reinterpret_cast<StructTypeObject*>(instr.ic_shape);
                auto* bs_inst = allocate<StructInstanceObject>(bs_type);
                bs_inst->shape = bs_type->cached_shape;
                bs_inst->field_values.reserve(bs_count);
                for (std::uint8_t i = 0; i < bs_count; ++i) {
                    const Value& bs_val = regs_ptr[static_cast<std::size_t>(instr.src1 + i)];
                    bs_inst->field_values.push_back(bs_val);
                    note_struct_field_write(bs_inst, i, bs_val);
                }
                regs_ptr[instr.dst] = Value::object(bs_inst);
                ++local_ip; break;
            }
            {
                const Span bs_span = instruction_span(instr);
                std::vector<Value> bs_fields(bs_count);
                for (std::uint8_t i = 0; i < bs_count; ++i)
                    bs_fields[i] = regs_ptr[static_cast<std::size_t>(instr.src1 + i)];
                ZEPHYR_TRY_ASSIGN(bs_result, build_struct_value(frame_ptr->current_env, metadata_ptr[local_ip].string_operand,
                                                                metadata_ptr[local_ip].names, bs_fields, bs_span, module_name));
                regs_ptr[instr.dst] = bs_result;
                if (bs_result.is_object() && bs_result.as_object()->kind == ObjectKind::StructInstance) {
                    auto* bs_inst_ic = static_cast<StructInstanceObject*>(bs_result.as_object());
                    StructTypeObject* bs_type_ic = bs_inst_ic->type;
                    const auto& bs_names = metadata_ptr[local_ip].names;
                    bool bs_in_order = (bs_names.size() == bs_type_ic->fields.size());
                    if (bs_in_order) {
                        for (std::size_t k = 0; k < bs_names.size(); ++k) {
                            if (bs_names[k] != bs_type_ic->fields[k].name) { bs_in_order = false; break; }
                        }
                    }
                    if (bs_in_order) {
                        instr.ic_shape = reinterpret_cast<Shape*>(bs_type_ic);
                        instr.ic_slot = 1;
                    }
                }
            }
            ++local_ip; break;
        }
        case BytecodeOp::R_BUILD_ARRAY: {
            const std::uint8_t ba_count = instr.operand_a;
            auto* ba_array = allocate<ArrayObject>();
            ba_array->elements.resize(ba_count);
            for (std::uint8_t i = 0; i < ba_count; ++i) {
                ba_array->elements[i] = regs_ptr[static_cast<std::size_t>(instr.src1 + i)];
                note_array_element_write(ba_array, i, ba_array->elements[i]);
            }
            regs_ptr[instr.dst] = Value::object(ba_array);
            ++local_ip; break;
        }
        case BytecodeOp::R_LOAD_INDEX: {
            const Value& li_obj = regs_ptr[instr.src1];
            const Value& li_idx = regs_ptr[instr.src2];
            const Span li_span = instruction_span(instr);
            ZEPHYR_TRY_ASSIGN(li_result, get_index_value(li_obj, li_idx, li_span, module_name));
            regs_ptr[instr.dst] = li_result;
            ++local_ip; break;
        }
        case BytecodeOp::R_JUMP:
            local_ip = static_cast<std::size_t>(instr.operand);
            break;
        case BytecodeOp::R_JUMP_IF_FALSE: {
            const std::size_t src = static_cast<std::size_t>(unpack_r_src_operand(instr.operand));
            if (!is_truthy(regs_ptr[src])) {
                local_ip = static_cast<std::size_t>(unpack_r_jump_target_operand(instr.operand));
            } else {
                ++local_ip;
            }
            break;
        }
        case BytecodeOp::R_JUMP_IF_TRUE: {
            const std::size_t src = static_cast<std::size_t>(unpack_r_src_operand(instr.operand));
            if (is_truthy(regs_ptr[src])) {
                local_ip = static_cast<std::size_t>(unpack_r_jump_target_operand(instr.operand));
            } else {
                ++local_ip;
            }
            break;
        }
        case BytecodeOp::R_SI_ADD_STORE: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t ir = lv.as_int() + rv.as_int();
                if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(ir); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Plus, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_SI_SUB_STORE: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t ir = lv.as_int() - rv.as_int();
                if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(ir); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Minus, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_SI_MUL_STORE: {
            const Value& lv = regs_ptr[instr.src1]; const Value& rv = regs_ptr[instr.src2];
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t ir = lv.as_int() * rv.as_int();
                if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(ir); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr); ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Star, lv, rv, s, module_name)); regs_ptr[instr.dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_SI_MODI_ADD_STORE: {
            const Value& sma_acc = regs_ptr[instr.src1];
            const Value& sma_src = regs_ptr[instr.src2];
            const std::int64_t sma_div = static_cast<std::int64_t>(instr.operand_a);
            if (sma_acc.is_int() && sma_src.is_int()) {
                const std::int64_t sma_r = sma_acc.as_int() + (sma_src.as_int() % sma_div);
                if (sma_r >= Value::kIntMin && sma_r <= Value::kIntMax) { regs_ptr[instr.dst] = Value::integer(sma_r); ++local_ip; break; }
            }
            { const Span s = instruction_span(instr);
            const Value sma_div_val = Value::integer(sma_div);
            ZEPHYR_TRY_ASSIGN(sma_mod, apply_binary_op(TokenType::Percent, sma_src, sma_div_val, s, module_name));
            ZEPHYR_TRY_ASSIGN(sma_result, apply_binary_op(TokenType::Plus, sma_acc, sma_mod, s, module_name));
            regs_ptr[instr.dst] = sma_result; }
            ++local_ip; break;
        }
        case BytecodeOp::R_SI_CMP_JUMP_FALSE: {
            const BytecodeOp cmp_op = unpack_r_si_cmp_jump_false_compare_op(instr.operand);
            const std::uint8_t s1 = unpack_r_si_cmp_jump_false_src1(instr.operand);
            const std::uint8_t s2 = unpack_r_si_cmp_jump_false_src2(instr.operand);
            const Value& lv = regs_ptr[s1]; const Value& rv = regs_ptr[s2];
            bool int_handled = false;
            if (lv.is_int() && rv.is_int()) {
                const std::int64_t a = lv.as_int(), b = rv.as_int();
                bool cond = false;
                bool valid_op = true;
                switch (cmp_op) {
                case BytecodeOp::R_LT: cond = a < b; break;
                case BytecodeOp::R_LE: cond = a <= b; break;
                case BytecodeOp::R_GT: cond = a > b; break;
                case BytecodeOp::R_GE: cond = a >= b; break;
                case BytecodeOp::R_EQ: cond = a == b; break;
                case BytecodeOp::R_NE: cond = a != b; break;
                default: valid_op = false; break;
                }
                if (valid_op) {
                    if (cond) { ++local_ip; } else { local_ip = static_cast<std::size_t>(metadata_ptr[local_ip].jump_table.front()); }
                    int_handled = true;
                }
            }
            if (!int_handled) {
                const Span span = instruction_span(instr);
                if (metadata_ptr[local_ip].jump_table.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module_name, span,
                        "Register compare superinstruction is missing jump metadata.");
                }
                TokenType token = TokenType::Less;
                switch (cmp_op) {
                case BytecodeOp::R_LT: token = TokenType::Less; break;
                case BytecodeOp::R_LE: token = TokenType::LessEqual; break;
                case BytecodeOp::R_GT: token = TokenType::Greater; break;
                case BytecodeOp::R_GE: token = TokenType::GreaterEqual; break;
                case BytecodeOp::R_EQ: token = TokenType::EqualEqual; break;
                case BytecodeOp::R_NE: token = TokenType::BangEqual; break;
                default:
                    return make_loc_error<CoroutineExecutionResult>(module_name, span, "Invalid compare op in R_SI_CMP_JUMP_FALSE.");
                }
                ZEPHYR_TRY_ASSIGN(cmp_result, apply_binary_op(token, lv, rv, span, module_name));
                if (!is_truthy(cmp_result)) { local_ip = static_cast<std::size_t>(metadata_ptr[local_ip].jump_table.front()); }
                else { ++local_ip; }
            }
            break;
        }
        case BytecodeOp::R_SI_CMPI_JUMP_FALSE: {
            const std::uint8_t cmpi_s1 = unpack_r_si_cmpi_jump_false_src1(instr.operand);
            const std::int64_t cmpi_imm = unpack_r_si_cmpi_jump_false_imm(instr.operand);
            const auto cmpi_kind = unpack_r_si_cmpi_jump_false_kind(instr.operand);
            const Value& cmpi_lhs = regs_ptr[cmpi_s1];
            if (cmpi_lhs.is_int()) {
                const std::int64_t a = cmpi_lhs.as_int();
                bool cmp_val;
                switch (cmpi_kind) {
                case SuperinstructionCompareKind::Less:         cmp_val = a < cmpi_imm;  break;
                case SuperinstructionCompareKind::LessEqual:    cmp_val = a <= cmpi_imm; break;
                case SuperinstructionCompareKind::Greater:      cmp_val = a > cmpi_imm;  break;
                case SuperinstructionCompareKind::GreaterEqual: cmp_val = a >= cmpi_imm; break;
                case SuperinstructionCompareKind::Equal:        cmp_val = a == cmpi_imm; break;
                case SuperinstructionCompareKind::NotEqual:     cmp_val = a != cmpi_imm; break;
                default:                                         cmp_val = false; break;
                }
                if (cmp_val) { ++local_ip; }
                else {
                    const InstructionMetadata& cmpi_meta = metadata_ptr[local_ip];
                    local_ip = cmpi_meta.jump_table.empty() ? local_ip + 1 : static_cast<std::size_t>(cmpi_meta.jump_table.front());
                }
                break;
            }
            { const Span s = instruction_span(instr);
            if (metadata_ptr[local_ip].jump_table.empty()) {
                return make_loc_error<CoroutineExecutionResult>(module_name, s, "R_SI_CMPI_JUMP_FALSE missing jump metadata.");
            }
            const BytecodeOp cmpi_op = register_bytecode_op_from_superinstruction_compare_kind(cmpi_kind);
            const Value cmpi_rhs_val = Value::integer(cmpi_imm);
            TokenType cmpi_token = TokenType::Less;
            switch (cmpi_op) {
            case BytecodeOp::R_LT: cmpi_token = TokenType::Less; break;
            case BytecodeOp::R_LE: cmpi_token = TokenType::LessEqual; break;
            case BytecodeOp::R_GT: cmpi_token = TokenType::Greater; break;
            case BytecodeOp::R_GE: cmpi_token = TokenType::GreaterEqual; break;
            case BytecodeOp::R_EQ: cmpi_token = TokenType::EqualEqual; break;
            case BytecodeOp::R_NE: cmpi_token = TokenType::BangEqual; break;
            default: break;
            }
            ZEPHYR_TRY_ASSIGN(cmpi_result, apply_binary_op(cmpi_token, cmpi_lhs, cmpi_rhs_val, s, module_name));
            if (!is_truthy(cmpi_result)) { local_ip = static_cast<std::size_t>(metadata_ptr[local_ip].jump_table.front()); }
            else { ++local_ip; } }
            break;
        }
        case BytecodeOp::R_SI_LOAD_ADD_STORE: {
            const std::uint8_t las_dst = unpack_r_si_load_add_store_dst(instr.operand);
            const std::uint8_t las_src = unpack_r_si_load_add_store_local_src(instr.operand);
            const int las_const = unpack_r_si_load_add_store_constant(instr.operand);
            const Value& src_val = regs_ptr[las_src];
            const BytecodeConstant& bc_las = constants_ptr[static_cast<std::size_t>(las_const)];
            if (src_val.is_int()) {
                if (const auto* iv = std::get_if<std::int64_t>(&bc_las)) {
                    const std::int64_t ir = src_val.as_int() + *iv;
                    if (ir >= Value::kIntMin && ir <= Value::kIntMax) { regs_ptr[las_dst] = Value::integer(ir); ++local_ip; break; }
                }
            }
            { const Span s = instruction_span(instr);
              ZEPHYR_TRY_ASSIGN(cv, load_bytecode_constant(chunk, las_const));
              ZEPHYR_TRY_ASSIGN(r, apply_binary_op(TokenType::Plus, src_val, cv, s, module_name));
              regs_ptr[las_dst] = r; }
            ++local_ip; break;
        }
        case BytecodeOp::R_SPILL_LOAD: {
            const std::uint8_t dst = unpack_r_spill_reg(instr.operand);
            const int sidx = unpack_r_spill_idx(instr.operand);
            regs_ptr[dst] = spill_ptr[sidx];
            ++local_ip; break;
        }
        case BytecodeOp::R_SPILL_STORE: {
            const std::uint8_t src = unpack_r_spill_reg(instr.operand);
            const int sidx = unpack_r_spill_idx(instr.operand);
            spill_ptr[sidx] = regs_ptr[src];
            ++local_ip; break;
        }
        case BytecodeOp::R_YIELD: {
            const Value yield_value = regs_ptr[instr.src1];
            const Span span = instruction_span(instr);
            ZEPHYR_TRY(validate_handle_store(yield_value, HandleContainerKind::CoroutineFrame, span, module_name, "coroutine yield"));
            frame_ptr->ip_index = local_ip + 1;
            coroutine->suspended = true;
            compact_suspended_coroutine(coroutine);
            register_suspended_coroutine(coroutine);
            record_coroutine_trace_event(CoroutineTraceEvent::Type::Yielded, coroutine);
            return CoroutineExecutionResult{true, yield_value, executed_steps};
        }
        case BytecodeOp::R_RETURN: {
            const Value ret_value = regs_ptr[instr.src1];
            const Span span = instruction_span(instr);
            ZEPHYR_TRY(enforce_type(ret_value, frame_ptr->return_type_name, span, module_name, "coroutine return"));
            return do_return(ret_value);
        }
        default: {
            const Span span = instruction_span(instr);
            return make_loc_error<CoroutineExecutionResult>(module_name, span,
                "Unsupported opcode in register coroutine fast executor.");
        }
        }
    }

    return do_return(Value::nil());
}

RuntimeResult<Runtime::CoroutineExecutionResult> Runtime::resume_coroutine_bytecode(CoroutineObject* coroutine, ModuleRecord& module,
                                                                                    const Span& call_span) {
    if (coroutine->frames.empty()) {
        return make_loc_error<CoroutineExecutionResult>(module.name, call_span, "Coroutine frame stack is empty.");
    }

    // Fast path: single-frame register-mode coroutine — skip full single-frame setup
    if (coroutine->frames.size() == 1 && coroutine->frames[0].uses_register_mode) {
        return resume_register_coroutine_fast(coroutine, call_span);
    }

    // Fast path: single-frame coroutine (most common case — no nested calls)
    if (coroutine->frames.size() == 1) {
        return resume_coroutine_single_frame(coroutine, module, call_span);
    }

    std::size_t total_steps = 0;
    while (true) {
        if (coroutine->frames.size() > 1) {
            ZEPHYR_TRY_ASSIGN(child_result, resume_nested_coroutine_frame(coroutine, module, call_span));
            total_steps += child_result.step_count;
            if (child_result.yielded) {
                child_result.step_count = total_steps;
                return child_result;
            }
            auto& parent_frame = coroutine->frames.back();
            if (parent_frame.uses_register_mode && parent_frame.pending_call_dst_reg.has_value()) {
                const std::size_t dst_reg = *parent_frame.pending_call_dst_reg;
                if (dst_reg < parent_frame.regs.size()) {
                    parent_frame.regs[dst_reg] = child_result.value;
                }
                parent_frame.pending_call_dst_reg = std::nullopt;
            } else {
                parent_frame.stack.push_back(child_result.value);
            }
            continue;
        }

        ZEPHYR_TRY_ASSIGN(root_result, resume_coroutine_single_frame(coroutine, module, call_span));
        root_result.step_count += total_steps;
        return root_result;
    }
}

RuntimeResult<Runtime::CoroutineExecutionResult> Runtime::resume_coroutine_single_frame(CoroutineObject* coroutine, ModuleRecord& module,
                                                                                        const Span& call_span) {
    (void)call_span;
    if (coroutine->frames.empty()) {
        return make_loc_error<CoroutineExecutionResult>(module.name, Span{}, "Coroutine frame stack is empty.");
    }

    const std::size_t frame_index = coroutine->frames.size() - 1;
    CoroutineFrameState* frame_ptr = &coroutine->frames[frame_index];
    auto frame = [&]() -> CoroutineFrameState& { return *frame_ptr; };
    auto refresh_frame_ptr = [&]() { frame_ptr = &coroutine->frames[frame_index]; };

    if (frame().bytecode == nullptr) {
        return make_loc_error<CoroutineExecutionResult>(module.name, Span{}, "Coroutine is missing bytecode.");
    }

    const BytecodeFunction& chunk = *frame().bytecode;
    const bool lightweight = chunk.uses_only_locals_and_upvalues;
    std::size_t executed_steps = 0;
    if (frame().uses_register_mode && frame().regs.empty() && frame().reg_count == 0) {
        const int needed = std::max(chunk.max_regs, chunk.local_count);
        if (needed > 0 && needed <= CoroutineFrameState::kInlineRegs) {
            frame().reg_count = static_cast<std::uint8_t>(needed);
            std::fill_n(frame().inline_regs, needed, Value::nil());
        } else {
            frame().regs.resize(static_cast<std::size_t>(std::max(needed, 0)), Value::nil());
        }
    }
    if (frame().locals.empty()) {
        frame().locals.resize(static_cast<std::size_t>(std::max(chunk.local_count, 0)), Value::nil());
    }
    if (frame().local_binding_owners.size() != frame().locals.size()) {
        frame().local_binding_owners.assign(frame().locals.size(), nullptr);
    }
    if (frame().local_bindings.size() != frame().locals.size()) {
        frame().local_bindings.assign(frame().locals.size(), nullptr);
    }
    if (frame().local_binding_versions.size() != frame().locals.size()) {
        frame().local_binding_versions.assign(frame().locals.size(), 0);
    }
    if (frame().global_binding_owners.size() != chunk.global_names.size()) {
        frame().global_binding_owners.assign(chunk.global_names.size(), nullptr);
    }
    if (frame().global_bindings.size() != chunk.global_names.size()) {
        frame().global_bindings.assign(chunk.global_names.size(), nullptr);
    }
    if (frame().global_binding_versions.size() != chunk.global_names.size()) {
        frame().global_binding_versions.assign(chunk.global_names.size(), 0);
    }
    if (frame().root_env == nullptr) {
        frame().root_env = allocate<Environment>(frame().closure);
        install_upvalue_bindings(frame().root_env, chunk, frame().captured_upvalues);
    }
    if (frame().current_env == nullptr) {
        frame().current_env = frame().root_env;
    }
    if (!lightweight &&
        std::all_of(frame().local_bindings.begin(), frame().local_bindings.end(), [](Binding* binding) { return binding == nullptr; })) {
        refresh_coroutine_locals_from_env(frame());
    }

    const std::size_t active_scope_base = active_environments_.size();
    for (Environment* scope : frame().scope_stack) {
        active_environments_.push_back(scope);
    }
    struct ActiveScopeMirrorGuard {
        Runtime& runtime;
        std::size_t base = 0;

        ~ActiveScopeMirrorGuard() {
            while (runtime.active_environments_.size() > base) {
                runtime.active_environments_.pop_back();
            }
        }
    } active_scope_guard{*this, active_scope_base};
    ScopedVectorItem<const std::vector<Value>*> stack_root(rooted_value_vectors_, &frame().stack);
    ScopedVectorItem<const std::vector<Value>*> locals_root(rooted_value_vectors_, &frame().locals);

    auto pop_all_active_scope_mirrors = [&]() {
        for (std::size_t i = frame().scope_stack.size(); i > 0; --i) {
            if (!active_environments_.empty() && active_environments_.back() == frame().scope_stack[i - 1]) {
                active_environments_.pop_back();
            }
        }
    };

    auto local_slot_index = [&](int slot, const Span& span) -> RuntimeResult<std::size_t> {
        if (slot < 0 || static_cast<std::size_t>(slot) >= frame().locals.size()) {
            return make_loc_error<std::size_t>(module.name, span, "Invalid local slot access.");
        }
        return static_cast<std::size_t>(slot);
    };

    auto global_slot_index = [&](int slot, const Span& span) -> RuntimeResult<std::size_t> {
        if (slot < 0 || static_cast<std::size_t>(slot) >= chunk.global_names.size()) {
            return make_loc_error<std::size_t>(module.name, span, "Invalid global slot access.");
        }
        return static_cast<std::size_t>(slot);
    };

    auto environment_on_chain = [&](Environment* owner, Environment* search_env) -> bool {
        for (Environment* current = search_env; current != nullptr; current = current->parent) {
            if (current == owner) {
                return true;
            }
        }
        return false;
    };

    auto resolve_local_binding = [&](std::size_t slot, Environment* search_env = nullptr) {
        if (slot >= frame().local_bindings.size()) {
            return;
        }
        frame().local_binding_owners[slot] = nullptr;
        frame().local_bindings[slot] = nullptr;
        frame().local_binding_versions[slot] = 0;
        if (slot >= chunk.local_names.size() || chunk.local_names[slot].empty()) {
            return;
        }
        Environment* env = search_env != nullptr ? search_env : frame().current_env;
        for (Environment* current = env; current != nullptr; current = current->parent) {
            auto it = current->values.find(chunk.local_names[slot]);
            if (it != current->values.end()) {
                frame().local_binding_owners[slot] = current;
                frame().local_bindings[slot] = &it->second;
                frame().local_binding_versions[slot] = current->version;
                frame().locals[slot] = read_binding_value(it->second);
                return;
            }
        }
    };

    auto local_binding_valid = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (slot >= frame().local_bindings.size() || frame().local_bindings[slot] == nullptr ||
            frame().local_binding_owners[slot] == nullptr) {
            return false;
        }
        Environment* env = search_env != nullptr ? search_env : frame().current_env;
        return frame().local_binding_versions[slot] == frame().local_binding_owners[slot]->version &&
               environment_on_chain(frame().local_binding_owners[slot], env);
    };

    auto ensure_local_binding = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (local_binding_valid(slot, search_env)) {
            ++local_binding_cache_hits_;
            return true;
        }
        ++local_binding_cache_misses_;
        resolve_local_binding(slot, search_env);
        return local_binding_valid(slot, search_env);
    };

    auto current_global_resolution_env = [&](Environment* search_env = nullptr) -> Environment* {
        Environment* env = search_env != nullptr ? search_env : frame().current_env;
        if (frame().bytecode == nullptr || !frame().bytecode->global_slots_use_module_root_base) {
            return env;
        }
        if (search_env != nullptr) {
            return module_or_root_environment(search_env);
        }
        if (frame().global_resolution_env != nullptr) {
            return frame().global_resolution_env;
        }
        return module_or_root_environment(env);
    };

    auto resolve_global_binding = [&](std::size_t slot, Environment* search_env = nullptr) {
        if (slot >= frame().global_bindings.size()) {
            return;
        }
        frame().global_binding_owners[slot] = nullptr;
        frame().global_bindings[slot] = nullptr;
        frame().global_binding_versions[slot] = 0;
        Environment* env = current_global_resolution_env(search_env);
        for (Environment* current = env; current != nullptr; current = current->parent) {
            auto it = current->values.find(chunk.global_names[slot]);
            if (it != current->values.end()) {
                frame().global_binding_owners[slot] = current;
                frame().global_bindings[slot] = &it->second;
                frame().global_binding_versions[slot] = current->version;
                return;
            }
        }
    };

    auto global_binding_valid = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (slot >= frame().global_bindings.size() || frame().global_bindings[slot] == nullptr ||
            frame().global_binding_owners[slot] == nullptr) {
            return false;
        }
        Environment* env = current_global_resolution_env(search_env);
        return frame().global_binding_versions[slot] == frame().global_binding_owners[slot]->version &&
               environment_on_chain(frame().global_binding_owners[slot], env);
    };

    auto ensure_global_binding = [&](std::size_t slot, Environment* search_env = nullptr) -> bool {
        if (global_binding_valid(slot, search_env)) {
            ++global_binding_cache_hits_;
            return true;
        }
        ++global_binding_cache_misses_;
        resolve_global_binding(slot, search_env);
        return global_binding_valid(slot, search_env);
    };

    auto read_local_value = [&](std::size_t slot) -> Value {
        if (lightweight) {
            return frame().locals[slot];
        }
        ensure_local_binding(slot, frame().current_env);
        if (slot < frame().local_bindings.size() && frame().local_bindings[slot] != nullptr) {
            frame().locals[slot] = read_binding_value(*frame().local_bindings[slot]);
        }
        return frame().locals[slot];
    };

    auto read_global_value = [&](std::size_t slot, const Span& span) -> RuntimeResult<Value> {
        ensure_global_binding(slot, frame().current_env);
        if (slot < frame().global_bindings.size() && frame().global_bindings[slot] != nullptr) {
            return read_binding_value(*frame().global_bindings[slot]);
        }
        return make_loc_error<Value>(module.name, span, "Unknown identifier '" + chunk.global_names[slot] + "'.");
    };

    auto upvalue_slot_index = [&](int slot, const Span& span) -> RuntimeResult<std::size_t> {
        if (slot < 0 || static_cast<std::size_t>(slot) >= frame().captured_upvalues.size()) {
            return make_loc_error<std::size_t>(module.name, span, "Invalid upvalue slot access.");
        }
        return static_cast<std::size_t>(slot);
    };

    auto read_upvalue_value = [&](std::size_t slot) -> RuntimeResult<Value> {
        if (slot >= frame().captured_upvalues.size() || frame().captured_upvalues[slot] == nullptr) {
            return make_loc_error<Value>(module.name, Span{}, "Missing captured upvalue cell.");
        }
        return frame().captured_upvalues[slot]->value;
    };

    auto sync_locals_from_bindings = [&]() {
        if (lightweight) {
            return;
        }
        const std::size_t count = std::min(frame().locals.size(), frame().local_bindings.size());
        for (std::size_t i = 0; i < count; ++i) {
            if (frame().local_bindings[i] != nullptr) {
                frame().locals[i] = read_binding_value(*frame().local_bindings[i]);
            }
        }
    };

    auto assign_cached_binding =
        [&](Binding* binding, Environment* owner, Value value, const Span& span, const std::string& name) -> VoidResult {
        if (binding == nullptr || owner == nullptr) {
            return make_error<std::monostate>("Missing binding cache entry.");
        }
        if (!binding->mutable_value) {
            return make_loc_error<std::monostate>(module.name, span, "Cannot assign to immutable binding '" + name + "'.");
        }
        ZEPHYR_TRY(enforce_type(value, binding->type_name, span, module.name, "assignment"));
        if (owner->kind == EnvironmentKind::Root || owner->kind == EnvironmentKind::Module) {
            ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::Global, span, module.name, "global assignment"));
        }
        if (binding->cell != nullptr) {
            const std::string capture_context = binding->cell->container_kind == HandleContainerKind::CoroutineFrame
                                                   ? "coroutine capture assignment"
                                                   : "closure capture assignment";
            ZEPHYR_TRY(validate_handle_store(value, binding->cell->container_kind, span, module.name, capture_context));
        }
        write_binding_value(*binding, value);
        note_write(owner, value);
        if (binding->cell != nullptr) {
            note_write(static_cast<GcObject*>(binding->cell), value);
        }
        return ok_result();
    };

    auto pop_value = [&](const Span& span) -> RuntimeResult<Value> {
        if (frame().stack.empty()) {
            return make_loc_error<Value>(module.name, span, "Bytecode stack underflow.");
        }
        Value value = frame().stack.back();
        frame().stack.pop_back();
        return value;
    };

    auto enter_scope = [&](Environment* scope) {
        frame().scope_stack.push_back(scope);
        active_environments_.push_back(scope);
        frame().current_env = scope;
    };

    auto exit_scope = [&]() -> VoidResult {
        if (frame().scope_stack.empty()) {
            return make_error<std::monostate>("Bytecode scope underflow.");
        }
        Environment* scope = frame().scope_stack.back();
        frame().scope_stack.pop_back();
        if (!active_environments_.empty() && active_environments_.back() == scope) {
            active_environments_.pop_back();
        }
        frame().current_env = scope->parent != nullptr ? scope->parent : frame().root_env;
        return ok_result();
    };

    auto finalize = [&](const Value& value, bool yielded) -> CoroutineExecutionResult {
        pop_all_active_scope_mirrors();
        coroutine->suspended = yielded;
        if (yielded) {
            sync_locals_from_bindings();
            compact_suspended_coroutine(coroutine);
            register_suspended_coroutine(coroutine);
            record_coroutine_trace_event(CoroutineTraceEvent::Type::Yielded, coroutine);
        } else {
            unregister_suspended_coroutine(coroutine);
            coroutine->completed = (frame_index == 0);
            coroutine->started = true;
            if (frame_index == 0) {
                record_coroutine_completed(coroutine);
            }
            frame().stack.clear();
            frame().locals.clear();
            frame().scope_stack.clear();
            frame().captured_upvalues.clear();
            frame().local_binding_owners.clear();
            frame().local_bindings.clear();
            frame().local_binding_versions.clear();
            frame().global_binding_owners.clear();
            frame().global_bindings.clear();
            frame().global_binding_versions.clear();
            frame().stack_cards.clear();
            frame().local_cards.clear();
            frame().regs.clear();
            frame().reg_cards.clear();
            frame().reg_count = 0;
            frame().current_env = nullptr;
            frame().root_env = nullptr;
            frame().ip = 0;
            frame().ip_index = 0;
        }
        return CoroutineExecutionResult{yielded, value, executed_steps};
    };

    if (frame().uses_register_mode) {
        const std::size_t regs_size = !frame().regs.empty()
            ? frame().regs.size()
            : static_cast<std::size_t>(frame().reg_count);
        auto register_index = [&](std::uint8_t reg, const Span& span) -> RuntimeResult<std::size_t> {
            if (static_cast<std::size_t>(reg) >= regs_size) {
                return make_loc_error<std::size_t>(module.name, span, "Invalid register access.");
            }
            return static_cast<std::size_t>(reg);
        };

        auto register_mode_global_resolution_env = [&]() -> Environment* {
            if (frame().bytecode == nullptr || !frame().bytecode->global_slots_use_module_root_base) {
                return frame().current_env;
            }
            if (frame().global_resolution_env != nullptr) {
                return frame().global_resolution_env;
            }
            return module_or_root_environment(frame().current_env);
        };

        auto resolve_register_mode_global_binding = [&](int slot, const Span& span) -> RuntimeResult<std::pair<Environment*, Binding*>> {
            if (slot < 0 || static_cast<std::size_t>(slot) >= chunk.global_names.size()) {
                return make_loc_error<std::pair<Environment*, Binding*>>(module.name, span, "Invalid global slot access.");
            }
            for (Environment* env = register_mode_global_resolution_env(); env != nullptr; env = env->parent) {
                auto it = env->values.find(chunk.global_names[static_cast<std::size_t>(slot)]);
                if (it != env->values.end()) {
                    return std::pair<Environment*, Binding*>{env, &it->second};
                }
            }
            return make_loc_error<std::pair<Environment*, Binding*>>(module.name,
                                                                     span,
                                                                     "Unknown identifier '" + chunk.global_names[static_cast<std::size_t>(slot)] + "'.");
        };

        auto binary_fast_or_fallback = [&](BytecodeOp op, Value left, Value right, const Span& span) -> RuntimeResult<Value> {
            if (left.is_int() && right.is_int()) {
                const std::int64_t a = left.as_int();
                const std::int64_t b = right.as_int();
                std::int64_t int_result = 0;
                switch (op) {
                    case BytecodeOp::R_ADD:
                        if (try_add_int48(a, b, int_result)) return Value::integer(int_result);
                        break;
                    case BytecodeOp::R_SUB:
                        if (try_sub_int48(a, b, int_result)) return Value::integer(int_result);
                        break;
                    case BytecodeOp::R_MUL:
                        if (try_mul_int48(a, b, int_result)) return Value::integer(int_result);
                        break;
                    case BytecodeOp::R_MOD:
                        return Value::integer(a % b);
                    case BytecodeOp::R_LT:
                        return Value::boolean(a < b);
                    case BytecodeOp::R_LE:
                        return Value::boolean(a <= b);
                    case BytecodeOp::R_GT:
                        return Value::boolean(a > b);
                    case BytecodeOp::R_GE:
                        return Value::boolean(a >= b);
                    case BytecodeOp::R_EQ:
                        return Value::boolean(a == b);
                    case BytecodeOp::R_NE:
                        return Value::boolean(a != b);
                    default:
                        break;
                }
            }

            TokenType token = TokenType::Plus;
            switch (op) {
                case BytecodeOp::R_ADD: token = TokenType::Plus; break;
                case BytecodeOp::R_SUB: token = TokenType::Minus; break;
                case BytecodeOp::R_MUL: token = TokenType::Star; break;
                case BytecodeOp::R_DIV: token = TokenType::Slash; break;
                case BytecodeOp::R_MOD: token = TokenType::Percent; break;
                case BytecodeOp::R_LT: token = TokenType::Less; break;
                case BytecodeOp::R_LE: token = TokenType::LessEqual; break;
                case BytecodeOp::R_GT: token = TokenType::Greater; break;
                case BytecodeOp::R_GE: token = TokenType::GreaterEqual; break;
                case BytecodeOp::R_EQ: token = TokenType::EqualEqual; break;
                case BytecodeOp::R_NE: token = TokenType::BangEqual; break;
                default: break;
            }
            return apply_binary_op(token, left, right, span, module.name);
        };

        Value* __restrict regs_ptr = !frame().regs.empty() ? frame().regs.data() : frame().inline_regs;
        const CompactInstruction* __restrict instrs_ptr = chunk.instructions.data();
        std::size_t local_ip = frame().ip_index;
        const std::size_t instrs_count = chunk.instructions.size();

        while (local_ip < instrs_count) {
            if (gc_stress_enabled_) {
                maybe_run_gc_stress_safe_point();
            }
            const CompactInstruction& instruction = instrs_ptr[local_ip];
            const InstructionMetadata& metadata = chunk.metadata[local_ip];
            const Span span = instruction_span(instruction);
            ++executed_steps;
            ++opcode_execution_count_;

            switch (instruction.op) {
                case BytecodeOp::R_LOAD_CONST: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_dst_operand(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(value, load_bytecode_constant(chunk, unpack_r_index_operand(instruction.operand)));
                    regs_ptr[dst] = value;
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_LOAD_INT: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_load_int_dst(instruction.operand), span));
                    regs_ptr[dst] = Value::integer(unpack_r_load_int_value(instruction.operand));
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_ADDI: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_addi_dst(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(src_idx, register_index(unpack_r_addi_src(instruction.operand), span));
                    const Value& addi_src = regs_ptr[src_idx];
                    const std::int64_t addi_imm = unpack_r_addi_imm(instruction.operand);
                    if (addi_src.is_int()) {
                        const std::int64_t sum = addi_src.as_int() + addi_imm;
                        if (sum >= Value::kIntMin && sum <= Value::kIntMax) {
                            regs_ptr[dst] = Value::integer(sum);
                            ++local_ip;
                            break;
                        }
                    }
                    { const Value imm_val = Value::integer(addi_imm);
                    ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_ADD, addi_src, imm_val, span));
                    regs_ptr[dst] = result; }
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_MODI: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_modi_dst(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(src_idx, register_index(unpack_r_modi_src(instruction.operand), span));
                    const Value& modi_src = regs_ptr[src_idx];
                    const std::int64_t modi_imm = unpack_r_modi_imm(instruction.operand);
                    if (modi_src.is_int() && modi_imm > 0) {
                        regs_ptr[dst] = Value::integer(modi_src.as_int() % modi_imm);
                        ++local_ip;
                        break;
                    }
                    { const Value imm_val = Value::integer(modi_imm);
                    ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_MOD, modi_src, imm_val, span));
                    regs_ptr[dst] = result; }
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_ADDI_JUMP: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_addi_dst(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(src_idx, register_index(unpack_r_addi_src(instruction.operand), span));
                    const Value& aj_src = regs_ptr[src_idx];
                    const std::int64_t aj_imm = unpack_r_addi_imm(instruction.operand);
                    if (aj_src.is_int()) {
                        const std::int64_t aj_sum = aj_src.as_int() + aj_imm;
                        if (aj_sum >= Value::kIntMin && aj_sum <= Value::kIntMax) {
                            regs_ptr[dst] = Value::integer(aj_sum);
                            local_ip = static_cast<std::size_t>(instruction.ic_slot);
                            break;
                        }
                    }
                    { const Value imm_val = Value::integer(aj_imm);
                    ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(BytecodeOp::R_ADD, aj_src, imm_val, span));
                    regs_ptr[dst] = result; }
                    local_ip = static_cast<std::size_t>(instruction.ic_slot);
                    break;
                }
                case BytecodeOp::R_SI_ADDI_CMPI_LT_JUMP: {
                    ZEPHYR_TRY_ASSIGN(acj_idx, register_index(unpack_r_si_acj_reg(instruction.operand), span));
                    const Value& acj_val = regs_ptr[acj_idx];
                    const std::int64_t acj_addi = unpack_r_si_acj_addi(instruction.operand);
                    const std::int64_t acj_limit = unpack_r_si_acj_limit(instruction.operand);
                    if (acj_val.is_int()) {
                        const std::int64_t acj_new = acj_val.as_int() + acj_addi;
                        if (acj_new >= Value::kIntMin && acj_new <= Value::kIntMax) {
                            regs_ptr[acj_idx] = Value::integer(acj_new);
                            if (acj_new < acj_limit) { local_ip = static_cast<std::size_t>(instruction.ic_slot); }
                            else { ++local_ip; }
                            break;
                        }
                    }
                    { const Value acj_addi_val = Value::integer(acj_addi);
                    ZEPHYR_TRY_ASSIGN(acj_new_val, binary_fast_or_fallback(BytecodeOp::R_ADD, acj_val, acj_addi_val, span));
                    regs_ptr[acj_idx] = acj_new_val;
                    const Value acj_limit_val = Value::integer(acj_limit);
                    ZEPHYR_TRY_ASSIGN(acj_cmp, binary_fast_or_fallback(BytecodeOp::R_LT, acj_new_val, acj_limit_val, span));
                    if (is_truthy(acj_cmp)) { local_ip = static_cast<std::size_t>(instruction.ic_slot); } else { ++local_ip; } }
                    break;
                }
                case BytecodeOp::R_SI_LOOP_STEP: {
                    ZEPHYR_TRY_ASSIGN(ls_acc_idx,  register_index(instruction.dst,  span));
                    ZEPHYR_TRY_ASSIGN(ls_iter_idx, register_index(instruction.src2, span));
                    const std::int64_t ls_div   = static_cast<std::int64_t>(instruction.operand_a);
                    const std::int64_t ls_step  = static_cast<std::int64_t>(static_cast<std::int8_t>(instruction.src1));
                    const std::int64_t ls_limit = unpack_r_si_ls_limit(instruction.ic_slot);
                    const std::size_t  ls_body  = unpack_r_si_ls_body(instruction.ic_slot);
                    Value& ls_acc  = regs_ptr[ls_acc_idx];
                    Value& ls_iter = regs_ptr[ls_iter_idx];
                    if (ls_acc.is_int() && ls_iter.is_int()) {
                        const std::int64_t ls_a = ls_acc.as_int();
                        const std::int64_t ls_i = ls_iter.as_int();
                        const std::int64_t ls_new_acc  = ls_a + (ls_i % ls_div);
                        const std::int64_t ls_new_iter = ls_i + ls_step;
                        if (ls_new_acc >= Value::kIntMin && ls_new_acc <= Value::kIntMax &&
                            ls_new_iter >= Value::kIntMin && ls_new_iter <= Value::kIntMax) {
                            ls_acc  = Value::integer(ls_new_acc);
                            ls_iter = Value::integer(ls_new_iter);
                            local_ip = (ls_new_iter < ls_limit) ? ls_body : local_ip + 1;
                            break;
                        }
                    }
                    { const Value ls_div_val = Value::integer(ls_div);
                    ZEPHYR_TRY_ASSIGN(ls_mod, binary_fast_or_fallback(BytecodeOp::R_MOD, ls_iter, ls_div_val, span));
                    ZEPHYR_TRY_ASSIGN(ls_new_acc, binary_fast_or_fallback(BytecodeOp::R_ADD, ls_acc, ls_mod, span));
                    ls_acc = ls_new_acc;
                    const Value ls_step_val = Value::integer(ls_step);
                    ZEPHYR_TRY_ASSIGN(ls_new_iter, binary_fast_or_fallback(BytecodeOp::R_ADD, ls_iter, ls_step_val, span));
                    ls_iter = ls_new_iter;
                    const Value ls_limit_val = Value::integer(ls_limit);
                    ZEPHYR_TRY_ASSIGN(ls_cmp, binary_fast_or_fallback(BytecodeOp::R_LT, ls_new_iter, ls_limit_val, span));
                    local_ip = is_truthy(ls_cmp) ? ls_body : local_ip + 1; }
                    break;
                }
                case BytecodeOp::R_LOAD_GLOBAL: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_dst_operand(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(binding_pair, resolve_register_mode_global_binding(unpack_r_index_operand(instruction.operand), span));
                    regs_ptr[dst] = read_binding_value(*binding_pair.second);
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_STORE_GLOBAL: {
                    ZEPHYR_TRY_ASSIGN(src, register_index(unpack_r_src_operand(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(binding_pair, resolve_register_mode_global_binding(unpack_r_index_operand(instruction.operand), span));
                    Binding* binding = binding_pair.second;
                    Environment* owner = binding_pair.first;
                    if (!binding->mutable_value) {
                        return make_loc_error<CoroutineExecutionResult>(
                            module.name,
                            span,
                            "Cannot assign to immutable binding '" +
                                chunk.global_names[static_cast<std::size_t>(unpack_r_index_operand(instruction.operand))] + "'.");
                    }
                    ZEPHYR_TRY(enforce_type(regs_ptr[src], binding->type_name, span, module.name, "assignment"));
                    ZEPHYR_TRY(validate_handle_store(regs_ptr[src], HandleContainerKind::Global, span, module.name, "global assignment"));
                    if (binding->cell != nullptr) {
                        ZEPHYR_TRY(validate_handle_store(regs_ptr[src],
                                                         binding->cell->container_kind,
                                                         span,
                                                         module.name,
                                                         "coroutine capture assignment"));
                    }
                    write_binding_value(*binding, regs_ptr[src]);
                    note_write(owner, regs_ptr[src]);
                    if (binding->cell != nullptr) {
                        note_write(static_cast<GcObject*>(binding->cell), regs_ptr[src]);
                    }
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_MOVE: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(src, register_index(instruction.src1, span));
                    regs_ptr[dst] = regs_ptr[src];
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_ADD:
                case BytecodeOp::R_SUB:
                case BytecodeOp::R_MUL:
                case BytecodeOp::R_DIV:
                case BytecodeOp::R_MOD:
                case BytecodeOp::R_LT:
                case BytecodeOp::R_LE:
                case BytecodeOp::R_GT:
                case BytecodeOp::R_GE:
                case BytecodeOp::R_EQ:
                case BytecodeOp::R_NE: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                    ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                    ZEPHYR_TRY_ASSIGN(result, binary_fast_or_fallback(instruction.op, regs_ptr[src1], regs_ptr[src2], span));
                    regs_ptr[dst] = result;
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_NOT: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(src, register_index(instruction.src1, span));
                    regs_ptr[dst] = Value::boolean(!is_truthy(regs_ptr[src]));
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_NEG: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(src, register_index(instruction.src1, span));
                    ZEPHYR_TRY_ASSIGN(result, apply_unary_op(TokenType::Minus, regs_ptr[src], span, module.name));
                    regs_ptr[dst] = result;
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_CALL: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(callee, register_index(instruction.src1, span));
                    std::vector<Value> args;
                    args.reserve(instruction.operand_a);
                    for (std::uint8_t index = 0; index < instruction.operand_a; ++index) {
                        ZEPHYR_TRY_ASSIGN(arg_reg,
                                          register_index(static_cast<std::uint8_t>(instruction.src2 + index), span));
                        args.push_back(regs_ptr[arg_reg]);
                    }
                    const Value callee_value = regs_ptr[callee];
                    if (callee_value.is_object() && callee_value.as_object()->kind == ObjectKind::ScriptFunction) {
                        ++local_ip;
                        frame().ip_index = local_ip;  // Sync before frame push
                        ZEPHYR_TRY(push_coroutine_script_frame(coroutine,
                                                              static_cast<ScriptFunctionObject*>(callee_value.as_object()),
                                                              args,
                                                              span,
                                                              module.name));
                        refresh_frame_ptr();  // coroutine->frames may have reallocated
                        ZEPHYR_TRY_ASSIGN(nested_result, resume_nested_coroutine_frame(coroutine, module, span));
                        executed_steps += nested_result.step_count;
                        refresh_frame_ptr();  // nested execution may have reallocated frames
                        regs_ptr = !frame().regs.empty() ? frame().regs.data() : frame().inline_regs;
                        if (nested_result.yielded) {
                            frame().pending_call_dst_reg = dst;
                            nested_result.step_count = executed_steps;
                            return nested_result;
                        }
                        regs_ptr[dst] = nested_result.value;
                        break;
                    }
                    ZEPHYR_TRY_ASSIGN(result, call_value(callee_value, args, span, module.name));
                    regs_ptr[dst] = result;
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_LOAD_MEMBER: {
                    ZEPHYR_TRY_ASSIGN(lm_dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(lm_src, register_index(instruction.src1, span));
                    const Value& lm_obj = regs_ptr[lm_src];
                    if (lm_obj.is_host_handle()) {
                        ZEPHYR_TRY_ASSIGN(lm_res, resolve_host_handle(lm_obj, span, module.name, "member access"));
                        const ZephyrHostClass* lm_class = lm_res.entry->host_class.get();
                        const ZephyrHostClass::Getter* lm_getter = nullptr;
                        if (instruction.ic_shape == reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(lm_class))) {
                            lm_getter = lm_class->get_getter_at(instruction.ic_slot);
                        } else {
                            std::uint32_t lm_idx = 0;
                            lm_getter = lm_class->find_getter_ic(metadata.string_operand, lm_idx);
                            if (lm_getter != nullptr) {
                                instruction.ic_shape = reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(lm_class));
                                instruction.ic_slot = lm_idx;
                            }
                        }
                        if (lm_getter != nullptr) {
                            try {
                                regs_ptr[lm_dst] = from_public_value((*lm_getter)(lm_res.instance));
                            } catch (const std::exception& e) {
                                return make_loc_error<CoroutineExecutionResult>(module.name, span, e.what());
                            }
                            ++local_ip; break;
                        }
                    }
                    ZEPHYR_TRY_ASSIGN(lm_result, load_member_value(lm_obj, instruction, metadata, module.name));
                    regs_ptr[lm_dst] = lm_result;
                    ++local_ip; break;
                }
                case BytecodeOp::R_STORE_MEMBER: {
                    ZEPHYR_TRY_ASSIGN(sm_obj_idx, register_index(instruction.src1, span));
                    ZEPHYR_TRY_ASSIGN(sm_val_idx, register_index(instruction.src2, span));
                    const Value& sm_obj = regs_ptr[sm_obj_idx];
                    const Value& sm_val = regs_ptr[sm_val_idx];
                    ZEPHYR_TRY_ASSIGN(sm_result, store_member_value(sm_obj, sm_val, instruction, metadata, module.name));
                    (void)sm_result;
                    ++local_ip; break;
                }
                case BytecodeOp::R_CALL_MEMBER: {
                    ZEPHYR_TRY_ASSIGN(cm_dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(cm_src, register_index(instruction.src1, span));
                    const Value& cm_obj = regs_ptr[cm_src];
                    if (cm_obj.is_host_handle()) {
                        ZEPHYR_TRY_ASSIGN(cm_res, resolve_host_handle(cm_obj, span, module.name, "method call"));
                        const ZephyrHostClass* cm_class = cm_res.entry->host_class.get();
                        const ZephyrHostClass::Method* cm_method = nullptr;
                        if (instruction.ic_shape == reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(cm_class))) {
                            cm_method = cm_class->get_method_at(instruction.ic_slot);
                        } else {
                            std::uint32_t cm_idx = 0;
                            cm_method = cm_class->find_method_ic(metadata.string_operand, cm_idx);
                            if (cm_method != nullptr) {
                                instruction.ic_shape = reinterpret_cast<Shape*>(const_cast<ZephyrHostClass*>(cm_class));
                                instruction.ic_slot = cm_idx;
                            }
                        }
                        if (cm_method != nullptr) {
                            const std::uint8_t cm_argc = instruction.operand_a;
                            auto cm_lease = acquire_public_args_buffer(cm_argc);
                            auto& cm_public_args = cm_lease.args();
                            for (std::uint8_t i = 0; i < cm_argc; ++i) {
                                ZEPHYR_TRY_ASSIGN(cm_arg_reg, register_index(static_cast<std::uint8_t>(instruction.src2 + i), span));
                                cm_public_args.push_back(to_public_value(regs_ptr[cm_arg_reg]));
                            }
                            try {
                                regs_ptr[cm_dst] = from_public_value((*cm_method)(cm_res.instance, cm_public_args));
                            } catch (const std::exception& e) {
                                return make_loc_error<CoroutineExecutionResult>(module.name, span, e.what());
                            }
                            ++local_ip; break;
                        }
                    }
                    {
                        std::vector<Value> cm_args;
                        cm_args.reserve(instruction.operand_a);
                        for (std::uint8_t i = 0; i < instruction.operand_a; ++i) {
                            ZEPHYR_TRY_ASSIGN(cm_arg_reg2, register_index(static_cast<std::uint8_t>(instruction.src2 + i), span));
                            cm_args.push_back(regs_ptr[cm_arg_reg2]);
                        }
                        ZEPHYR_TRY_ASSIGN(cm_result, call_member_value(cm_obj, metadata.string_operand, cm_args, span, module.name));
                        regs_ptr[cm_dst] = cm_result;
                    }
                    ++local_ip; break;
                }
                case BytecodeOp::R_BUILD_STRUCT: {
                    ZEPHYR_TRY_ASSIGN(bs_dst, register_index(instruction.dst, span));
                    const std::uint8_t bs_count = instruction.operand_a;
                    std::vector<Value> bs_fields(bs_count);
                    for (std::uint8_t i = 0; i < bs_count; ++i) {
                        ZEPHYR_TRY_ASSIGN(bs_src, register_index(static_cast<std::uint8_t>(instruction.src1 + i), span));
                        bs_fields[i] = regs_ptr[bs_src];
                    }
                    ZEPHYR_TRY_ASSIGN(bs_result, build_struct_value(frame().current_env, metadata.string_operand,
                                                                    metadata.names, bs_fields, span, module.name));
                    regs_ptr[bs_dst] = bs_result;
                    ++local_ip; break;
                }
                case BytecodeOp::R_BUILD_ARRAY: {
                    ZEPHYR_TRY_ASSIGN(ba_dst, register_index(instruction.dst, span));
                    const std::uint8_t ba_count = instruction.operand_a;
                    auto* ba_array = allocate<ArrayObject>();
                    ba_array->elements.resize(ba_count);
                    for (std::uint8_t i = 0; i < ba_count; ++i) {
                        ZEPHYR_TRY_ASSIGN(ba_src, register_index(static_cast<std::uint8_t>(instruction.src1 + i), span));
                        ba_array->elements[i] = regs_ptr[ba_src];
                        note_array_element_write(ba_array, i, ba_array->elements[i]);
                    }
                    regs_ptr[ba_dst] = Value::object(ba_array);
                    ++local_ip; break;
                }
                case BytecodeOp::R_LOAD_INDEX: {
                    ZEPHYR_TRY_ASSIGN(li_dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(li_src, register_index(instruction.src1, span));
                    ZEPHYR_TRY_ASSIGN(li_idx, register_index(instruction.src2, span));
                    ZEPHYR_TRY_ASSIGN(li_result, get_index_value(regs_ptr[li_src], regs_ptr[li_idx], span, module.name));
                    regs_ptr[li_dst] = li_result;
                    ++local_ip; break;
                }
                case BytecodeOp::R_JUMP:
                    local_ip = static_cast<std::size_t>(instruction.operand);
                    break;
                case BytecodeOp::R_JUMP_IF_FALSE: {
                    ZEPHYR_TRY_ASSIGN(src, register_index(unpack_r_src_operand(instruction.operand), span));
                    if (!is_truthy(regs_ptr[src])) {
                        local_ip = static_cast<std::size_t>(unpack_r_jump_target_operand(instruction.operand));
                    } else {
                        ++local_ip;
                    }
                    break;
                }
                case BytecodeOp::R_JUMP_IF_TRUE: {
                    ZEPHYR_TRY_ASSIGN(src, register_index(unpack_r_src_operand(instruction.operand), span));
                    if (is_truthy(regs_ptr[src])) {
                        local_ip = static_cast<std::size_t>(unpack_r_jump_target_operand(instruction.operand));
                    } else {
                        ++local_ip;
                    }
                    break;
                }
                case BytecodeOp::R_SI_ADD_STORE:
                case BytecodeOp::R_SI_SUB_STORE:
                case BytecodeOp::R_SI_MUL_STORE: {
                    const BytecodeOp fused_op = instruction.op == BytecodeOp::R_SI_ADD_STORE ? BytecodeOp::R_ADD
                                             : instruction.op == BytecodeOp::R_SI_SUB_STORE ? BytecodeOp::R_SUB
                                                                                             : BytecodeOp::R_MUL;
                    ZEPHYR_TRY_ASSIGN(dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(src1, register_index(instruction.src1, span));
                    ZEPHYR_TRY_ASSIGN(src2, register_index(instruction.src2, span));
                    ZEPHYR_TRY_ASSIGN(result,
                                      binary_fast_or_fallback(fused_op, regs_ptr[src1], regs_ptr[src2], span));
                    regs_ptr[dst] = result;
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_SI_CMP_JUMP_FALSE: {
                    if (metadata.jump_table.empty()) {
                        return make_loc_error<CoroutineExecutionResult>(module.name,
                                                                        span,
                                                                        "Register compare superinstruction is missing jump metadata.");
                    }
                    ZEPHYR_TRY_ASSIGN(src1, register_index(unpack_r_si_cmp_jump_false_src1(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(src2, register_index(unpack_r_si_cmp_jump_false_src2(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(result,
                                      binary_fast_or_fallback(unpack_r_si_cmp_jump_false_compare_op(instruction.operand),
                                                              regs_ptr[src1],
                                                              regs_ptr[src2],
                                                              span));
                    if (!is_truthy(result)) {
                        local_ip = static_cast<std::size_t>(metadata.jump_table.front());
                    } else {
                        ++local_ip;
                    }
                    break;
                }
                case BytecodeOp::R_SI_CMPI_JUMP_FALSE: {
                    if (metadata.jump_table.empty()) {
                        return make_loc_error<CoroutineExecutionResult>(module.name, span, "R_SI_CMPI_JUMP_FALSE missing jump metadata.");
                    }
                    ZEPHYR_TRY_ASSIGN(cmpi_src_idx, register_index(unpack_r_si_cmpi_jump_false_src1(instruction.operand), span));
                    const Value& cmpi_lhs = regs_ptr[cmpi_src_idx];
                    const std::int64_t cmpi_imm = unpack_r_si_cmpi_jump_false_imm(instruction.operand);
                    const Value cmpi_rhs_val = Value::integer(cmpi_imm);
                    const BytecodeOp cmpi_op = register_bytecode_op_from_superinstruction_compare_kind(
                        unpack_r_si_cmpi_jump_false_kind(instruction.operand));
                    ZEPHYR_TRY_ASSIGN(cmpi_result, binary_fast_or_fallback(cmpi_op, cmpi_lhs, cmpi_rhs_val, span));
                    if (!is_truthy(cmpi_result)) {
                        local_ip = static_cast<std::size_t>(metadata.jump_table.front());
                    } else { ++local_ip; }
                    break;
                }
                case BytecodeOp::R_SI_LOAD_ADD_STORE: {
                    ZEPHYR_TRY_ASSIGN(dst, register_index(unpack_r_si_load_add_store_dst(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(local_src, register_index(unpack_r_si_load_add_store_local_src(instruction.operand), span));
                    ZEPHYR_TRY_ASSIGN(constant_value, load_bytecode_constant(chunk, unpack_r_si_load_add_store_constant(instruction.operand)));
                    ZEPHYR_TRY_ASSIGN(result,
                                      binary_fast_or_fallback(BytecodeOp::R_ADD, regs_ptr[local_src], constant_value, span));
                    regs_ptr[dst] = result;
                    ++local_ip;
                    break;
                }
                case BytecodeOp::R_SI_MODI_ADD_STORE: {
                    ZEPHYR_TRY_ASSIGN(sma_dst, register_index(instruction.dst, span));
                    ZEPHYR_TRY_ASSIGN(sma_acc_idx, register_index(instruction.src1, span));
                    ZEPHYR_TRY_ASSIGN(sma_src_idx, register_index(instruction.src2, span));
                    const Value& sma_acc = regs_ptr[sma_acc_idx];
                    const Value& sma_src = regs_ptr[sma_src_idx];
                    const std::int64_t sma_div = static_cast<std::int64_t>(instruction.operand_a);
                    if (sma_acc.is_int() && sma_src.is_int()) {
                        const std::int64_t sma_r = sma_acc.as_int() + (sma_src.as_int() % sma_div);
                        if (sma_r >= Value::kIntMin && sma_r <= Value::kIntMax) {
                            regs_ptr[sma_dst] = Value::integer(sma_r); ++local_ip; break;
                        }
                    }
                    { const Value sma_div_val = Value::integer(sma_div);
                    ZEPHYR_TRY_ASSIGN(sma_mod, binary_fast_or_fallback(BytecodeOp::R_MOD, sma_src, sma_div_val, span));
                    ZEPHYR_TRY_ASSIGN(sma_result, binary_fast_or_fallback(BytecodeOp::R_ADD, sma_acc, sma_mod, span));
                    regs_ptr[sma_dst] = sma_result; }
                    ++local_ip; break;
                }
                case BytecodeOp::R_YIELD: {
                    ZEPHYR_TRY_ASSIGN(src, register_index(instruction.src1, span));
                    const Value yield_value = regs_ptr[src];
                    ZEPHYR_TRY(validate_handle_store(yield_value, HandleContainerKind::CoroutineFrame, span, module.name, "coroutine yield"));
                    frame().ip_index = local_ip + 1;  // Advance past yield before suspending
                    return finalize(yield_value, true);
                }
                case BytecodeOp::R_RETURN: {
                    ZEPHYR_TRY_ASSIGN(src, register_index(instruction.src1, span));
                    const Value result = regs_ptr[src];
                    ZEPHYR_TRY(enforce_type(result, frame().return_type_name, span, module.name, "coroutine return"));
                    frame().ip_index = local_ip + 1;
                    return finalize(result, false);
                }
                default:
                    return make_loc_error<CoroutineExecutionResult>(module.name,
                                                                    span,
                                                                    "Unsupported opcode in register coroutine executor.");
            }
        }

        return finalize(Value::nil(), false);
    }

    while (frame().ip < chunk.instructions.size()) {
        if (gc_stress_enabled_) {
            maybe_run_gc_stress_safe_point();
        }
        CoroutineFrameState& current_frame = frame();
        const CompactInstruction& instruction = chunk.instructions[current_frame.ip];
        if (dap_active_) {
            check_breakpoint(current_frame.ip, instruction.span_line, module.name);
        }
        const LazyInstructionMetadata lazy_metadata{chunk.metadata, current_frame.ip};
        const LazyInstructionSpan lazy_span{instruction};
#define metadata (*lazy_metadata)
#define span lazy_span
        ++executed_steps;
        ++opcode_execution_count_;
        switch (instruction.op) {
            case BytecodeOp::LoadConst: {
                ZEPHYR_TRY_ASSIGN(value, load_bytecode_constant(chunk, instruction.operand));
                current_frame.stack.push_back(value);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::LoadLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                current_frame.stack.push_back(read_local_value(slot));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::LoadUpvalue: {
                ZEPHYR_TRY_ASSIGN(slot, upvalue_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, read_upvalue_value(slot));
                current_frame.stack.push_back(value);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::LoadName: {
                if (instruction.operand >= 0 && static_cast<std::size_t>(instruction.operand) < chunk.global_names.size()) {
                    ZEPHYR_TRY_ASSIGN(slot, global_slot_index(instruction.operand, span));
                    ZEPHYR_TRY_ASSIGN(value, read_global_value(slot, span));
                    current_frame.stack.push_back(value);
                    ++current_frame.ip;
                    break;
                }
                ZEPHYR_TRY_ASSIGN(value, lookup_value(current_frame.current_env, metadata.string_operand, span, module.name, chunk.global_names));
                current_frame.stack.push_back(value);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DefineLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY(enforce_type(value, metadata.type_name, span, module.name, "let binding"));
                current_frame.locals[slot] = value;
                if (!lightweight) {
                    define_value(current_frame.current_env, metadata.string_operand, value, metadata.flag, metadata.type_name);
                    resolve_local_binding(slot, current_frame.current_env);
                }
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DefineName: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY(enforce_type(value, metadata.type_name, span, module.name, "let binding"));
                define_value(current_frame.current_env, metadata.string_operand, value, metadata.flag, metadata.type_name);
                if (instruction.operand >= 0 && static_cast<std::size_t>(instruction.operand) < chunk.global_names.size()) {
                    ZEPHYR_TRY_ASSIGN(slot, global_slot_index(instruction.operand, span));
                    resolve_global_binding(slot, current_frame.current_env);
                }
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::BindPattern: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(matched, bind_pattern(current_frame.current_env, value, metadata.pattern, module.name));
                if (matched) {
                    const std::size_t count = std::min(metadata.names.size(), metadata.jump_table.size());
                    for (std::size_t binding_index = 0; binding_index < count; ++binding_index) {
                        ZEPHYR_TRY_ASSIGN(slot, local_slot_index(metadata.jump_table[binding_index], span));
                        const auto it = current_frame.current_env->values.find(metadata.names[binding_index]);
                        if (it == current_frame.current_env->values.end()) {
                            return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                            "Missing bound pattern variable '" + metadata.names[binding_index] + "'.");
                        }
                        current_frame.locals[slot] = read_binding_value(it->second);
                        resolve_local_binding(slot, current_frame.current_env);
                    }
                }
                current_frame.stack.push_back(Value::boolean(matched));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::StoreLocal: {
                ZEPHYR_TRY_ASSIGN(slot, local_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                if (lightweight) {
                    // Direct local-slot update; lightweight coroutine frames do not mirror locals into Environment bindings.
                } else if (!ensure_local_binding(slot, current_frame.current_env)) {
                    ZEPHYR_TRY(assign_value(current_frame.current_env, metadata.string_operand, value, span, module.name));
                    resolve_local_binding(slot, current_frame.current_env);
                } else {
                    ZEPHYR_TRY(assign_cached_binding(current_frame.local_bindings[slot], current_frame.local_binding_owners[slot],
                                                    value, span, metadata.string_operand));
                }
                current_frame.locals[slot] = value;
                current_frame.stack.push_back(value);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::StoreUpvalue: {
                ZEPHYR_TRY_ASSIGN(slot, upvalue_slot_index(instruction.operand, span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                auto* cell = current_frame.captured_upvalues[slot];
                if (cell == nullptr) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    "Missing captured upvalue cell.");
                }
                if (!cell->mutable_value) {
                    return make_loc_error<CoroutineExecutionResult>(
                        module.name,
                        span,
                        "Cannot assign to immutable captured binding '" + metadata.string_operand + "'.");
                }
                ZEPHYR_TRY(enforce_type(value, cell->type_name, span, module.name, "assignment"));
                const std::string capture_context = cell->container_kind == HandleContainerKind::CoroutineFrame
                                                       ? "coroutine capture assignment"
                                                       : "closure capture assignment";
                ZEPHYR_TRY(validate_handle_store(value, cell->container_kind, span, module.name, capture_context));
                cell->value = value;
                note_write(static_cast<GcObject*>(cell), value);
                current_frame.stack.push_back(value);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::StoreName: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                if (instruction.operand >= 0 && static_cast<std::size_t>(instruction.operand) < chunk.global_names.size()) {
                    ZEPHYR_TRY_ASSIGN(slot, global_slot_index(instruction.operand, span));
                    if (!ensure_global_binding(slot, current_frame.current_env)) {
                        ZEPHYR_TRY(assign_value(current_frame.current_env, metadata.string_operand, value, span, module.name));
                        resolve_global_binding(slot, current_frame.current_env);
                    } else {
                        ZEPHYR_TRY(assign_cached_binding(current_frame.global_bindings[slot], current_frame.global_binding_owners[slot],
                                                        value, span, metadata.string_operand));
                    }
                } else {
                    ZEPHYR_TRY(assign_value(current_frame.current_env, metadata.string_operand, value, span, module.name));
                }
                current_frame.stack.push_back(value);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Pop: {
                if (current_frame.stack.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                current_frame.stack.pop_back();
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Not: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, apply_unary_op(TokenType::Bang, value, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Negate: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, apply_unary_op(TokenType::Minus, value, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::ToBool: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                current_frame.stack.push_back(Value::boolean(is_truthy(value)));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Stringify: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                current_frame.stack.push_back(make_string(value_to_string(value)));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Add:
            case BytecodeOp::Subtract:
            case BytecodeOp::Multiply:
            case BytecodeOp::Divide:
            case BytecodeOp::Modulo:
            case BytecodeOp::Equal:
            case BytecodeOp::NotEqual:
            case BytecodeOp::Less:
            case BytecodeOp::LessEqual:
            case BytecodeOp::Greater:
            case BytecodeOp::GreaterEqual: {
                ZEPHYR_TRY_ASSIGN(right, pop_value(span));
                ZEPHYR_TRY_ASSIGN(left, pop_value(span));
                // ── Integer fast paths (avoids apply_binary_op dispatch overhead) ──
                if (left.is_int() && right.is_int()) {
                    const int64_t a = left.as_int(), b = right.as_int();
                    std::int64_t int_result = 0;
                    bool handled_fast_path = true;
                    switch (instruction.op) {
                        case BytecodeOp::Add:
                            if (try_add_int48(a, b, int_result)) { current_frame.stack.push_back(Value::integer(int_result)); ++current_frame.ip; }
                            else { handled_fast_path = false; }
                            break;
                        case BytecodeOp::Subtract:
                            if (try_sub_int48(a, b, int_result)) { current_frame.stack.push_back(Value::integer(int_result)); ++current_frame.ip; }
                            else { handled_fast_path = false; }
                            break;
                        case BytecodeOp::Multiply:
                            if (try_mul_int48(a, b, int_result)) { current_frame.stack.push_back(Value::integer(int_result)); ++current_frame.ip; }
                            else { handled_fast_path = false; }
                            break;
                        case BytecodeOp::Modulo:
                            current_frame.stack.push_back(Value::integer(a % b));
                            ++current_frame.ip;
                            break;
                        case BytecodeOp::Less:         current_frame.stack.push_back(Value::boolean(a < b));  ++current_frame.ip; break;
                        case BytecodeOp::LessEqual:    current_frame.stack.push_back(Value::boolean(a <= b)); ++current_frame.ip; break;
                        case BytecodeOp::Greater:      current_frame.stack.push_back(Value::boolean(a > b));  ++current_frame.ip; break;
                        case BytecodeOp::GreaterEqual: current_frame.stack.push_back(Value::boolean(a >= b)); ++current_frame.ip; break;
                        case BytecodeOp::Equal:        current_frame.stack.push_back(Value::boolean(a == b)); ++current_frame.ip; break;
                        case BytecodeOp::NotEqual:     current_frame.stack.push_back(Value::boolean(a != b)); ++current_frame.ip; break;
                        default:
                            handled_fast_path = false;
                            break;
                    }
                    if (handled_fast_path) {
                        break;
                    }
                }
                // ── General path ──
                TokenType op = TokenType::Plus;
                switch (instruction.op) {
                    case BytecodeOp::Add: op = TokenType::Plus; break;
                    case BytecodeOp::Subtract: op = TokenType::Minus; break;
                    case BytecodeOp::Multiply: op = TokenType::Star; break;
                    case BytecodeOp::Divide: op = TokenType::Slash; break;
                    case BytecodeOp::Modulo: op = TokenType::Percent; break;
                    case BytecodeOp::Equal: op = TokenType::EqualEqual; break;
                    case BytecodeOp::NotEqual: op = TokenType::BangEqual; break;
                    case BytecodeOp::Less: op = TokenType::Less; break;
                    case BytecodeOp::LessEqual: op = TokenType::LessEqual; break;
                    case BytecodeOp::Greater: op = TokenType::Greater; break;
                    case BytecodeOp::GreaterEqual: op = TokenType::GreaterEqual; break;
                    default: break;
                }
                ZEPHYR_TRY_ASSIGN(result, apply_binary_op(op, left, right, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::BuildArray: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > current_frame.stack.size()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                auto* array = allocate<ArrayObject>();
                array->elements.resize(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    ZEPHYR_TRY(validate_handle_store(current_frame.stack.back(), HandleContainerKind::ArrayElement, span,
                                                     module.name, "array literal"));
                    array->elements[static_cast<std::size_t>(index)] = current_frame.stack.back();
                    note_array_element_write(array, static_cast<std::size_t>(index), array->elements[static_cast<std::size_t>(index)]);
                    current_frame.stack.pop_back();
                }
                current_frame.stack.push_back(Value::object(array));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::ArrayLength: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                if (!value.is_object() || value.as_object()->kind != ObjectKind::Array) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "for-in expects Array.");
                }
                auto* array = static_cast<ArrayObject*>(value.as_object());
                current_frame.stack.push_back(Value::integer(static_cast<std::int64_t>(array->elements.size())));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::IterHasNext: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(iterable, pop_value(span));
                if (iterable.is_object() && iterable.as_object()->kind == ObjectKind::Array) {
                    auto* arr = static_cast<ArrayObject*>(iterable.as_object());
                    const bool has = index.as_int() < static_cast<std::int64_t>(arr->elements.size());
                    current_frame.stack.push_back(iterable);
                    current_frame.stack.push_back(index);
                    current_frame.stack.push_back(Value::boolean(has));
                } else {
                    current_frame.stack.push_back(iterable);
                    current_frame.stack.push_back(index);
                    ZEPHYR_TRY_ASSIGN(result, call_member_value(iterable, "has_next", {}, span, module.name));
                    current_frame.stack.push_back(result);
                }
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::IterNext: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(iterable, pop_value(span));
                if (iterable.is_object() && iterable.as_object()->kind == ObjectKind::Array) {
                    auto* arr = static_cast<ArrayObject*>(iterable.as_object());
                    const Value elem = arr->elements[static_cast<std::size_t>(index.as_int())];
                    current_frame.stack.push_back(iterable);
                    current_frame.stack.push_back(Value::integer(index.as_int() + 1));
                    current_frame.stack.push_back(elem);
                } else {
                    ZEPHYR_TRY_ASSIGN(elem, call_member_value(iterable, "next", {}, span, module.name));
                    current_frame.stack.push_back(iterable);
                    current_frame.stack.push_back(index);
                    current_frame.stack.push_back(elem);
                }
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::BuildStruct: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > current_frame.stack.size() ||
                    static_cast<std::size_t>(instruction.operand) != metadata.names.size()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> field_values(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    field_values[static_cast<std::size_t>(index)] = current_frame.stack.back();
                    current_frame.stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(result,
                                  build_struct_value(current_frame.current_env, metadata.string_operand, metadata.names, field_values,
                                                     span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::BuildEnum: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand) > current_frame.stack.size()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> payload(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    payload[static_cast<std::size_t>(index)] = current_frame.stack.back();
                    current_frame.stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(result,
                                  build_enum_value(current_frame.current_env,
                                                   metadata.string_operand,
                                                   metadata.type_name.value_or(std::string{}),
                                                   payload,
                                                   span,
                                                   module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::IsEnumVariant: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result,
                                  is_enum_variant_value(value, metadata.string_operand, metadata.type_name.value_or(std::string{}),
                                                        instruction.operand, span, module.name));
                current_frame.stack.push_back(Value::boolean(result));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::LoadEnumPayload: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, get_enum_payload_value(value, instruction.operand, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::LoadMember: {
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, load_member_value(object, instruction, metadata, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::StoreMember: {
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, store_member_value(object, value, instruction, metadata, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::LoadIndex: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, get_index_value(object, index, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::StoreIndex: {
                ZEPHYR_TRY_ASSIGN(index, pop_value(span));
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, set_index_value(object, index, value, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Call: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand + 1) > current_frame.stack.size()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> args(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    args[static_cast<std::size_t>(index)] = current_frame.stack.back();
                    current_frame.stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(callee, pop_value(span));
                if (callee.is_object() && callee.as_object()->kind == ObjectKind::ScriptFunction) {
                    ++current_frame.ip;
                    ZEPHYR_TRY(push_coroutine_script_frame(coroutine,
                                                          static_cast<ScriptFunctionObject*>(callee.as_object()),
                                                          args,
                                                          span,
                                                          module.name));
                    refresh_frame_ptr();
                    ZEPHYR_TRY_ASSIGN(nested_result, resume_nested_coroutine_frame(coroutine, module, span));
                    executed_steps += nested_result.step_count;
                    refresh_frame_ptr();
                    if (nested_result.yielded) {
                        nested_result.step_count = executed_steps;
                        return nested_result;
                    }
                    frame().stack.push_back(nested_result.value);
                    break;
                }
                ZEPHYR_TRY_ASSIGN(result, call_value(callee, args, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::CallMember: {
                if (instruction.operand < 0 || static_cast<std::size_t>(instruction.operand + 1) > current_frame.stack.size()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                std::vector<Value> args(static_cast<std::size_t>(instruction.operand));
                for (int index = instruction.operand - 1; index >= 0; --index) {
                    args[static_cast<std::size_t>(index)] = current_frame.stack.back();
                    current_frame.stack.pop_back();
                }
                ZEPHYR_TRY_ASSIGN(object, pop_value(span));
                if (!object.is_host_handle()) {
                    ZEPHYR_TRY_ASSIGN(callee, get_member_value(object, metadata.string_operand, span, module.name));
                    if (callee.is_object() && callee.as_object()->kind == ObjectKind::ScriptFunction) {
                        ++current_frame.ip;
                        ZEPHYR_TRY(push_coroutine_script_frame(coroutine,
                                                              static_cast<ScriptFunctionObject*>(callee.as_object()),
                                                              args,
                                                              span,
                                                              module.name));
                        refresh_frame_ptr();
                        ZEPHYR_TRY_ASSIGN(nested_result, resume_nested_coroutine_frame(coroutine, module, span));
                        executed_steps += nested_result.step_count;
                        refresh_frame_ptr();
                        if (nested_result.yielded) {
                            nested_result.step_count = executed_steps;
                            return nested_result;
                        }
                        frame().stack.push_back(nested_result.value);
                        break;
                    }
                }
                ZEPHYR_TRY_ASSIGN(result, call_member_value(object, metadata.string_operand, args, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Jump:
                current_frame.ip = static_cast<std::size_t>(instruction.operand);
                break;
            case BytecodeOp::JumpIfFalse:
                if (current_frame.stack.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                if (!is_truthy(current_frame.stack.back())) {
                    current_frame.ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    ++current_frame.ip;
                }
                break;
            case BytecodeOp::JumpIfFalsePop:
                if (current_frame.stack.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                if (!is_truthy(current_frame.stack.back())) {
                    current_frame.stack.pop_back();
                    current_frame.ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    current_frame.stack.pop_back();
                    ++current_frame.ip;
                }
                break;
            case BytecodeOp::JumpIfTrue:
                if (current_frame.stack.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                if (is_truthy(current_frame.stack.back())) {
                    current_frame.ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    ++current_frame.ip;
                }
                break;
            case BytecodeOp::JumpIfNilKeep:
                if (current_frame.stack.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Bytecode stack underflow.");
                }
                if (current_frame.stack.back().is_nil()) {
                    current_frame.ip = static_cast<std::size_t>(instruction.operand);
                } else {
                    ++current_frame.ip;
                }
                break;
            case BytecodeOp::EnterScope: {
                if (!lightweight) {
                    auto* scope = allocate<Environment>(current_frame.current_env);
                    enter_scope(scope);
                }
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::ExitScope:
                if (!lightweight) {
                    ZEPHYR_TRY(exit_scope());
                }
                ++current_frame.ip;
                break;
            case BytecodeOp::ImportModule: {
                ZEPHYR_TRY_ASSIGN(imported,
                                  import_module(module.path.empty() ? std::filesystem::current_path() : module.path.parent_path(),
                                                metadata.string_operand));
                ZEPHYR_TRY(import_exports(current_frame.current_env, *imported, metadata.type_name, metadata.names, module.name, span));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DeclareFunction: {
                const auto cached_params = metadata.stmt != nullptr ? static_cast<FunctionDecl*>(metadata.stmt)->params
                                                                    : cached_params_from_metadata(metadata, instruction.span_line);
                const auto return_type = metadata.stmt != nullptr
                                             ? static_cast<FunctionDecl*>(metadata.stmt)->return_type
                                             : cached_return_type_from_metadata(metadata, instruction.span_line);
                auto* function_decl = static_cast<FunctionDecl*>(metadata.stmt);
                const std::string function_name =
                    function_decl != nullptr ? function_decl->name : metadata.string_operand;
                const auto generic_params = function_decl != nullptr ? function_decl->generic_params : std::vector<std::string>{};
                const auto where_clauses = function_decl != nullptr ? function_decl->where_clauses : std::vector<TraitBound>{};
                if (metadata.bytecode == nullptr && function_decl == nullptr) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "Cached function metadata is incomplete.");
                }
                ZEPHYR_TRY_ASSIGN(function,
                                  create_script_function(function_name,
                                                         module.name,
                                                         cached_params,
                                                         return_type,
                                                         function_decl != nullptr ? function_decl->body.get() : nullptr,
                                                         current_frame.current_env,
                                                          metadata.bytecode != nullptr
                                                              ? metadata.bytecode
                                                              : compile_bytecode_function(function_name, cached_params,
                                                                                          function_decl->body.get()),
                                                          span,
                                                          generic_params,
                                                          where_clauses));
                Value function_value = Value::object(function);
                define_value(current_frame.current_env, function_name, function_value, false, std::string("Function"));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DeclareStruct: {
                auto* struct_decl = static_cast<StructDecl*>(metadata.stmt);
                auto* type = allocate<StructTypeObject>(struct_decl->name);
                type->generic_params = struct_decl->generic_params;
                for (const auto& field : struct_decl->fields) {
                    type->fields.push_back(StructFieldSpec{field.name, field.type.display_name()});
                }
                define_value(current_frame.current_env, struct_decl->name, Value::object(type), false);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DeclareEnum: {
                auto* enum_decl = static_cast<EnumDecl*>(metadata.stmt);
                auto* type = allocate<EnumTypeObject>(enum_decl->name);
                for (const auto& variant : enum_decl->variants) {
                    EnumVariantSpec spec;
                    spec.name = variant.name;
                    for (const auto& payload_type : variant.payload_types) {
                        spec.payload_types.push_back(payload_type.display_name());
                    }
                    type->variants.push_back(std::move(spec));
                }
                define_value(current_frame.current_env, enum_decl->name, Value::object(type), false);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DeclareTrait: {
                auto* trait_decl = static_cast<TraitDecl*>(metadata.stmt);
                ZEPHYR_TRY(register_trait_decl(current_frame.current_env, trait_decl, module));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::DeclareImpl: {
                auto* impl_decl = static_cast<ImplDecl*>(metadata.stmt);
                ZEPHYR_TRY(register_impl_decl(current_frame.current_env, impl_decl, module));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::ExportName:
                if (module.namespace_object != nullptr &&
                    std::find(module.namespace_object->exports.begin(), module.namespace_object->exports.end(), metadata.string_operand) ==
                        module.namespace_object->exports.end()) {
                    module.namespace_object->exports.push_back(metadata.string_operand);
                }
                ++current_frame.ip;
                break;
            case BytecodeOp::EvalAstExpr: {
                if constexpr (!kBytecodeAstFallbackEnabled) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    ast_fallback_disabled_message("Bytecode expression"));
                }
                ++ast_fallback_executions_;
                ZEPHYR_TRY_ASSIGN(result, evaluate(current_frame.current_env, metadata.expr, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::MakeFunction: {
                const auto cached_params = metadata.expr != nullptr ? static_cast<FunctionExpr*>(metadata.expr)->params
                                                                    : cached_params_from_metadata(metadata, instruction.span_line);
                const auto return_type = metadata.expr != nullptr
                                             ? static_cast<FunctionExpr*>(metadata.expr)->return_type
                                             : cached_return_type_from_metadata(metadata, instruction.span_line);
                auto* function_expr = static_cast<FunctionExpr*>(metadata.expr);
                if (metadata.bytecode == nullptr && function_expr == nullptr) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    "Cached function literal metadata is incomplete.");
                }
                ZEPHYR_TRY_ASSIGN(function,
                                  create_script_function(metadata.string_operand.empty() ? "<anonymous>" : metadata.string_operand,
                                                         module.name,
                                                         cached_params,
                                                         return_type,
                                                         function_expr != nullptr ? function_expr->body.get() : nullptr,
                                                         current_frame.current_env,
                                                          metadata.bytecode != nullptr
                                                              ? metadata.bytecode
                                                              : compile_bytecode_function("<anonymous>", cached_params,
                                                                                          function_expr->body.get()),
                                                          span,
                                                          std::vector<std::string>{}));
                current_frame.stack.push_back(Value::object(function));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::MakeCoroutine: {
                const auto cached_params = metadata.expr != nullptr ? static_cast<CoroutineExpr*>(metadata.expr)->params
                                                                    : cached_params_from_metadata(metadata, instruction.span_line);
                const auto return_type = metadata.expr != nullptr
                                             ? static_cast<CoroutineExpr*>(metadata.expr)->return_type
                                             : cached_return_type_from_metadata(metadata, instruction.span_line);
                auto* coroutine_expr = static_cast<CoroutineExpr*>(metadata.expr);
                if (!cached_params.empty()) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    "coroutine fn expressions do not support parameters yet.");
                }
                ZEPHYR_TRY(ensure_capture_cells(current_frame.current_env, HandleContainerKind::CoroutineFrame, span, module.name));
                if (metadata.bytecode == nullptr && coroutine_expr == nullptr) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    "Cached coroutine metadata is incomplete.");
                }
                auto bytecode = metadata.bytecode != nullptr ? metadata.bytecode
                                                              : compile_bytecode_function("<coroutine>", coroutine_expr->params,
                                                                                          coroutine_expr->body.get());
                ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(bytecode.get(), span, module.name, "Coroutine expression"));
                auto* nested_coroutine = allocate<CoroutineObject>(
                    module.name,
                    select_closure_environment(current_frame.current_env, bytecode),
                    bytecode,
                    return_type.has_value() ? std::optional<std::string>(return_type->display_name()) : std::nullopt);
                ensure_coroutine_trace_id(nested_coroutine);
                record_coroutine_trace_event(CoroutineTraceEvent::Type::Created, nested_coroutine);
                nested_coroutine->frames.front().global_resolution_env =
                    module_or_root_environment(nested_coroutine->frames.front().closure);
                if (nested_coroutine->frames.front().bytecode != nullptr) {
                    ZEPHYR_TRY_ASSIGN(captured_cells,
                                      capture_upvalue_cells(current_frame.current_env,
                                                            nested_coroutine->frames.front().bytecode->upvalue_names,
                                                            HandleContainerKind::CoroutineFrame,
                                                            span,
                                                            module.name));
                    nested_coroutine->frames.front().captured_upvalues = std::move(captured_cells);
                }
                current_frame.stack.push_back(Value::object(nested_coroutine));
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Resume: {
                ZEPHYR_TRY_ASSIGN(target, pop_value(span));
                ZEPHYR_TRY_ASSIGN(result, resume_coroutine_value(target, span, module.name));
                current_frame.stack.push_back(result);
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::ExecAstStmt: {
                if constexpr (!kBytecodeAstFallbackEnabled) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    ast_fallback_disabled_message("Bytecode statement"));
                }
                ++ast_fallback_executions_;
                ZEPHYR_TRY_ASSIGN(flow, execute(current_frame.current_env, metadata.stmt, module));
                if (flow.kind == FlowSignal::Kind::Return) {
                    return finalize(flow.value, false);
                }
                if (flow.kind == FlowSignal::Kind::Break) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span, "break escaped native bytecode loop.");
                }
                if (flow.kind == FlowSignal::Kind::Continue) {
                    return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                    "continue escaped native bytecode loop.");
                }
                ++current_frame.ip;
                break;
            }
            case BytecodeOp::Yield: {
                ZEPHYR_TRY_ASSIGN(value, pop_value(span));
                ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::CoroutineFrame, span, module.name, "coroutine yield"));
                ++current_frame.ip;
                return finalize(value, true);
            }
            case BytecodeOp::Fail:
                return make_loc_error<CoroutineExecutionResult>(module.name, span,
                                                                metadata.string_operand.empty() ? "Bytecode execution failed." : metadata.string_operand);
            case BytecodeOp::MatchFail:
                return make_loc_error<CoroutineExecutionResult>(
                    module.name,
                    span,
                    metadata.string_operand.empty() ? "Match expression is not exhaustive. hint: match may not cover all cases."
                                                    : metadata.string_operand);
            case BytecodeOp::Return: {
                ZEPHYR_TRY_ASSIGN(result, pop_value(span));
                ZEPHYR_TRY(enforce_type(result, current_frame.return_type_name, span, module.name, "coroutine return"));
                ++current_frame.ip;
                return finalize(result, false);
            }
#if defined(_MSC_VER)
            default: __assume(0);
#else
            default: __builtin_unreachable();
#endif
        }
#undef span
#undef metadata
    }

    return finalize(Value::nil(), false);
}

RuntimeResult<Value> Runtime::execute_bytecode(ScriptFunctionObject* function, Environment* call_env, ModuleRecord& module, const Span& call_span) {
    if (function->bytecode == nullptr) {
        ZEPHYR_TRY_ASSIGN(flow, execute_block(call_env, function->body, module));
        if (flow.kind == FlowSignal::Kind::Return) {
            return flow.value;
        }
        if (flow.kind == FlowSignal::Kind::Break) {
            return make_loc_error<Value>(module.name, call_span, "break used outside loop.");
        }
        if (flow.kind == FlowSignal::Kind::Continue) {
            return make_loc_error<Value>(module.name, call_span, "continue used outside loop.");
        }
        return Value::nil();
    }
    if (function->bytecode->uses_register_mode) {
        return execute_register_bytecode(*function->bytecode, function->params, call_env, module, call_span, &function->captured_upvalues, nullptr);
    }
    return execute_bytecode_chunk(*function->bytecode, function->params, call_env, module, call_span, &function->captured_upvalues);
}

RuntimeResult<std::string> read_all_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return make_error<std::string>("Failed to open file: " + path.string());
    }
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

static bool path_like(const std::string& specifier) {
    return specifier.find('/') != std::string::npos || specifier.find('\\') != std::string::npos ||
           (specifier.size() >= 4 && specifier.substr(specifier.size() - 4) == ".zph");
}

std::filesystem::path Runtime::resolve_import_path(const std::filesystem::path& base_dir, const std::string& path) const {
    auto try_candidate = [](const std::filesystem::path& candidate) -> std::optional<std::filesystem::path> {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::weakly_canonical(candidate);
        }
        return std::nullopt;
    };

    auto with_default_extension = [](std::filesystem::path candidate) {
        if (!candidate.has_extension()) {
            candidate += ".zph";
        }
        return candidate;
    };

    if (auto resolved = try_candidate(base_dir / path)) {
        return *resolved;
    }
    if (auto resolved = try_candidate(with_default_extension(base_dir / path))) {
        return *resolved;
    }

    for (const auto& search_path : module_search_paths_) {
        if (auto resolved = try_candidate(search_path / path)) {
            return *resolved;
        }
        if (auto resolved = try_candidate(with_default_extension(search_path / path))) {
            return *resolved;
        }
    }

    const std::filesystem::path unresolved(path);
    if (unresolved.is_absolute()) {
        return with_default_extension(unresolved);
    }
    return with_default_extension(base_dir / path);
}

std::string Runtime::canonical_module_key(const std::filesystem::path& path) const {
    return std::filesystem::weakly_canonical(path).string();
}

RuntimeResult<ModuleRecord*> Runtime::load_file_record(const std::filesystem::path& path) {
    const auto resolved = std::filesystem::weakly_canonical(path);
    const std::string key = canonical_module_key(resolved);
    const std::uint64_t file_mtime = module_file_mtime(resolved);
    auto it = modules_.find(key);
    if (it != modules_.end() && it->second.file_mtime != 0 && file_mtime != 0 && it->second.file_mtime != file_mtime) {
        // mtime changed — read source and check content hash before evicting
        auto read_result = read_all_text(resolved);
        if (read_result) {
            const std::uint64_t new_hash = bytecode_cache::fnv1a_64(*read_result);
            if (new_hash != it->second.source_hash) {
                modules_.erase(it);
                it = modules_.end();
            } else {
                // Same content despite mtime change (e.g. touch), just update mtime
                it->second.file_mtime = file_mtime;
            }
        } else {
            modules_.erase(it);
            it = modules_.end();
        }
    }
    if (it == modules_.end()) {
        ModuleRecord record;
        record.name = key;
        record.path = resolved;
        record.file_mtime = file_mtime;
        if (bytecode_cache_enabled_) {
            auto cache_it = bytecode_cache_.find(key);
            if (cache_it != bytecode_cache_.end() && cache_it->second.file_mtime == file_mtime) {
                record.bytecode = bytecode_cache::deserialize_module_bytecode(cache_it->second.serialized_bytecode);
                if (record.bytecode == nullptr) {
                    bytecode_cache_.erase(cache_it);
                } else if (!bytecode_cache_roundtrip_supported(record.bytecode.get())) {
                    record.bytecode.reset();
                }
            }
            // If mtime differs but hash matches, also try hash-based cache lookup
            if (record.bytecode == nullptr && cache_it != bytecode_cache_.end()) {
                // source will be read below; hash comparison happens after
            }
        }
        if (record.bytecode == nullptr) {
            ZEPHYR_TRY_ASSIGN(source, read_all_text(resolved));
            record.source_text = source;
            record.source_hash = bytecode_cache::fnv1a_64(source);
            // Hash-based cache hit: same content, different mtime
            if (bytecode_cache_enabled_) {
                auto cache_it = bytecode_cache_.find(key);
                if (cache_it != bytecode_cache_.end() && cache_it->second.content_hash == record.source_hash) {
                    record.bytecode = bytecode_cache::deserialize_module_bytecode(cache_it->second.serialized_bytecode);
                    if (record.bytecode != nullptr && !bytecode_cache_roundtrip_supported(record.bytecode.get())) {
                        record.bytecode.reset();
                    }
                    if (record.bytecode == nullptr) {
                        bytecode_cache_.erase(cache_it);
                    }
                }
            }
            if (record.bytecode == nullptr) {
                ZEPHYR_TRY_ASSIGN(program, parse_source(source, key));
                record.program = std::move(program);
            }
        }
        record.environment = allocate_pinned<Environment>(root_environment_, EnvironmentKind::Module);
        record.namespace_object = allocate_pinned<ModuleNamespaceObject>(key, record.environment);
        it = modules_.emplace(key, std::move(record)).first;
    }

    ModuleRecord& module = it->second;
    if (module.loading) {
        return make_error<ModuleRecord*>("Circular import detected while loading '" + module.name + "'.");
    }
    if (!module.loaded) {
        module.loading = true;
        auto run_result = run_program(module);
        module.loading = false;
        if (!run_result) {
            return std::unexpected(append_stack_frame(std::move(run_result.error()), "<main>", module.name, Span{1, 1}));
        }
        module.loaded = true;
    }
    return &module;
}

RuntimeResult<ModuleRecord*> Runtime::load_virtual_record(const std::string& source, const std::string& module_name,
                                                          const std::filesystem::path& base_dir) {
    auto it = modules_.find(module_name);
    if (it == modules_.end()) {
        ModuleRecord record;
        record.name = module_name;
        record.path = base_dir / module_name;
        record.source_text = source;
        ZEPHYR_TRY_ASSIGN(program, parse_source(source, module_name));
        record.program = std::move(program);
        record.environment = allocate_pinned<Environment>(root_environment_, EnvironmentKind::Module);
        record.namespace_object = allocate_pinned<ModuleNamespaceObject>(module_name, record.environment);
        it = modules_.emplace(module_name, std::move(record)).first;
    }

    ModuleRecord& module = it->second;
    if (!module.loaded) {
        module.loading = true;
        auto run_result = run_program(module);
        module.loading = false;
        if (!run_result) {
            return std::unexpected(append_stack_frame(std::move(run_result.error()), "<main>", module.name, Span{1, 1}));
        }
        module.loaded = true;
    }
    return &module;
}

RuntimeResult<ModuleRecord*> Runtime::load_host_module(const std::string& module_name) {
    auto existing = modules_.find(module_name);
    if (existing != modules_.end()) {
        return &existing->second;
    }

    const auto host_it = host_modules_.find(module_name);
    if (host_it == host_modules_.end()) {
        return make_error<ModuleRecord*>("Unknown host module: " + module_name);
    }

    ModuleRecord record;
    record.name = module_name;
    record.environment = allocate_pinned<Environment>(root_environment_, EnvironmentKind::Module);
    record.namespace_object = allocate_pinned<ModuleNamespaceObject>(module_name, record.environment);
    record.loaded = true;

    ZephyrModuleBinder binder(this, record.environment, &record.namespace_object->exports);
    host_it->second.initializer(binder);

    auto [inserted_it, _] = modules_.emplace(module_name, std::move(record));
    return &inserted_it->second;
}

RuntimeResult<ModuleRecord*> Runtime::import_module(const std::filesystem::path& base_dir, const std::string& specifier) {
    // Registered host modules take priority over file-based resolution.
    // This allows built-in std/math and std/string to override on-disk stubs.
    if (host_modules_.count(specifier) > 0) {
        return load_host_module(specifier);
    }
    const auto resolved = resolve_import_path(base_dir, specifier);
    if (path_like(specifier) || std::filesystem::exists(resolved)) {
        return load_file_record(resolved);
    }
    return load_host_module(specifier);
}

VoidResult Runtime::import_exports(Environment* environment, ModuleRecord& imported, std::optional<std::string> alias,
                                   const std::vector<std::string>& named_pairs,
                                   const std::string& module_name, const Span& span) {
    if (alias.has_value()) {
        define_value(environment, *alias, Value::object(imported.namespace_object), false);
        return ok_result();
    }

    if (!named_pairs.empty()) {
        // Named import mode: pairs of (exported_name, local_name)
        for (std::size_t i = 0; i + 1 < named_pairs.size(); i += 2) {
            const auto& exported_name = named_pairs[i];
            const auto& local_name = named_pairs[i + 1];
            Binding* binding = lookup_binding(imported.environment, exported_name);
            if (binding == nullptr) {
                return make_loc_error<std::monostate>(module_name, span, "Module does not export '" + exported_name + "'.");
            }
            define_value(environment, local_name, read_binding_value(*binding), false, binding->type_name);
        }
        return ok_result();
    }

    for (const auto& name : imported.namespace_object->exports) {
        Binding* binding = lookup_binding(imported.environment, name);
        if (binding == nullptr) {
            return make_loc_error<std::monostate>(module_name, span, "Imported module is missing export '" + name + "'.");
        }
        define_value(environment, name, read_binding_value(*binding), false, binding->type_name);
    }
    return ok_result();
}

RuntimeResult<Value> Runtime::run_program(ModuleRecord& module) {
    ScopedVectorPush<Environment> scope(active_environments_, module.environment);
    if (module.bytecode == nullptr) {
        if (module.program == nullptr) {
            return make_error<Value>("Module '" + module.name + "' is missing both AST and bytecode.");
        }
        ZEPHYR_TRY_ASSIGN(compiled_bytecode, compile_module_bytecode(module.name, module.program.get()));
        module.bytecode = std::move(compiled_bytecode);
        if (bytecode_cache_enabled_ && !module.path.empty() && module.bytecode != nullptr) {
            bytecode_cache_[module.name] =
                BytecodeCacheEntry{module.path.string(), module.file_mtime,
                                   module.source_hash,
                                   bytecode_cache::serialize_module_bytecode(*module.bytecode)};
        }
    }
    ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(module.bytecode.get(), Span{}, module.name, "Module bytecode"));
    if (module.bytecode->uses_register_mode) {
        return execute_register_bytecode(*module.bytecode, {}, module.environment, module, Span{}, nullptr, nullptr);
    }
    return execute_bytecode_chunk(*module.bytecode, {}, module.environment, module, Span{}, nullptr);
}

RuntimeResult<Value> Runtime::execute_string_module(const std::string& source, const std::string& module_name,
                                                    const std::filesystem::path& base_dir) {
    ZEPHYR_TRY(load_virtual_record(source, module_name, base_dir));
    return Value::nil();
}

RuntimeResult<Value> Runtime::execute_file_module(const std::filesystem::path& path) {
    if (bytecode_cache_enabled_) {
        const auto resolved = std::filesystem::weakly_canonical(path);
        modules_.erase(canonical_module_key(resolved));
    }
    ZEPHYR_TRY(load_file_record(path));
    return Value::nil();
}

VoidResult Runtime::check_source(const std::string& source, const std::string& module_name, const std::filesystem::path& base_dir) {
    ZEPHYR_TRY_ASSIGN(program, parse_source(source, module_name));
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> checked;
    std::unordered_map<std::string, CheckModuleSummary> summaries;
    ZEPHYR_TRY(check_program_recursive(module_name, base_dir, *program, visiting, checked, summaries));
    return ok_result();
}

VoidResult Runtime::check_file(const std::filesystem::path& path) {
    const auto resolved = std::filesystem::weakly_canonical(path);
    ZEPHYR_TRY_ASSIGN(source, read_all_text(resolved));
    return check_source(source, resolved.string(), resolved.parent_path());
}

VoidResult Runtime::check_program_recursive(const std::string& module_name, const std::filesystem::path& base_dir, Program& program,
                                            std::unordered_set<std::string>& visiting,
                                            std::unordered_set<std::string>& checked,
                                            std::unordered_map<std::string, CheckModuleSummary>& summaries) {
    if (checked.contains(module_name)) {
        return ok_result();
    }
    if (!visiting.insert(module_name).second) {
        return make_error<std::monostate>("Circular import detected while checking '" + module_name + "'.");
    }

    struct ResolvedImport {
        const ImportStmt* stmt = nullptr;
        std::string module_name;
    };
    std::vector<ResolvedImport> resolved_imports;
    for (const auto& statement : program.statements) {
        if (const auto* import_stmt = dynamic_cast<ImportStmt*>(statement.get())) {
            const auto path = resolve_import_path(base_dir, import_stmt->path);
            const bool is_host = host_modules_.contains(import_stmt->path);
            if (!is_host && (path_like(import_stmt->path) || std::filesystem::exists(path))) {
                const std::string child_name = path.string();
                ZEPHYR_TRY_ASSIGN(child_source, read_all_text(path));
                ZEPHYR_TRY_ASSIGN(child_program, parse_source(child_source, child_name));
                ZEPHYR_TRY(check_program_recursive(child_name, path.parent_path(), *child_program, visiting, checked, summaries));
                resolved_imports.push_back(ResolvedImport{import_stmt, child_name});
            } else {
                if (!host_modules_.contains(import_stmt->path)) {
                    return make_error<std::monostate>("Unknown host module during check: " + import_stmt->path);
                }
                ZEPHYR_TRY_ASSIGN(host_module, load_host_module(import_stmt->path));
                if (!summaries.contains(host_module->name)) {
                    CheckModuleSummary host_summary;
                    host_summary.module_name = host_module->name;
                    if (host_module->namespace_object != nullptr) {
                        for (const auto& export_name : host_module->namespace_object->exports) {
                            host_summary.exports.insert(export_name);
                        }
                    }
                    summaries.emplace(host_module->name, std::move(host_summary));
                }
                resolved_imports.push_back(ResolvedImport{import_stmt, host_module->name});
            }
        }
    }

    CheckModuleSummary summary;
    summary.module_name = module_name;
    for (const auto& import : resolved_imports) {
        if (import.stmt->alias.has_value()) {
            summary.import_aliases[*import.stmt->alias] = import.module_name;
        } else if (!import.stmt->named.empty()) {
            // Named imports: each local_name is individually visible
            summary.imported_modules.push_back(import.module_name);
        } else {
            summary.imported_modules.push_back(import.module_name);
        }
    }

    for (const auto& statement : program.statements) {
        if (const auto* re_export = dynamic_cast<ReExportStmt*>(statement.get())) {
            for (const auto& item : re_export->items) {
                summary.exports.insert(item.exported_as);
            }
            continue;
        }
        if (const auto* function_decl = dynamic_cast<FunctionDecl*>(statement.get())) {
            summary.functions[function_decl->name] =
                CheckFunctionInfo{module_name, function_decl->name, function_decl->span, function_decl->params, function_decl->return_type};
            if (function_decl->exported) {
                summary.exports.insert(function_decl->name);
            }
            continue;
        }
        if (const auto* trait_decl = dynamic_cast<TraitDecl*>(statement.get())) {
            CheckTraitInfo trait_info;
            trait_info.module_name = module_name;
            trait_info.name = trait_decl->name;
            trait_info.span = trait_decl->span;
            for (const auto& method : trait_decl->methods) {
                trait_info.methods[method.name] = method;
            }
            summary.traits[trait_decl->name] = std::move(trait_info);
            if (trait_decl->exported) {
                summary.exports.insert(trait_decl->name);
            }
            continue;
        }
        if (const auto* let_stmt = dynamic_cast<LetStmt*>(statement.get())) {
            if (let_stmt->exported) {
                summary.exports.insert(let_stmt->name);
            }
            continue;
        }
        if (const auto* struct_decl = dynamic_cast<StructDecl*>(statement.get())) {
            if (struct_decl->exported) {
                summary.exports.insert(struct_decl->name);
            }
            continue;
        }
        if (const auto* enum_decl = dynamic_cast<EnumDecl*>(statement.get())) {
            if (enum_decl->exported) {
                summary.exports.insert(enum_decl->name);
            }
        }
    }

    auto render_optional_type = [](const std::optional<TypeRef>& type) {
        return type.has_value() ? type->display_name() : std::string("any");
    };
    auto render_signature = [&](const std::string& method_name, const std::vector<Param>& params, const std::optional<TypeRef>& return_type) {
        std::ostringstream out;
        out << "fn " << method_name << "(";
        for (std::size_t index = 0; index < params.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << params[index].name << ": " << render_optional_type(params[index].type);
        }
        out << ") -> " << render_optional_type(return_type);
        return out.str();
    };
    auto same_type = [](const std::optional<TypeRef>& left, const std::optional<TypeRef>& right) {
        if (left.has_value() != right.has_value()) {
            return false;
        }
        return !left.has_value() || left->display_name() == right->display_name();
    };

    std::unordered_map<std::string, CheckFunctionInfo> visible_functions = summary.functions;
    for (const auto& imported_name : summary.imported_modules) {
        const auto imported_it = summaries.find(imported_name);
        if (imported_it == summaries.end()) {
            continue;
        }
        for (const auto& [function_name, function_info] : imported_it->second.functions) {
            if (imported_it->second.exports.contains(function_name)) {
                visible_functions.emplace(function_name, function_info);
            }
        }
    }

    auto resolve_trait = [&](const TypeRef& type_name) -> const CheckTraitInfo* {
        if (type_name.parts.empty()) {
            return nullptr;
        }
        if (type_name.parts.size() > 1) {
            const auto alias_it = summary.import_aliases.find(type_name.parts.front());
            if (alias_it == summary.import_aliases.end()) {
                return nullptr;
            }
            const auto imported_it = summaries.find(alias_it->second);
            if (imported_it == summaries.end()) {
                return nullptr;
            }
            const std::string trait_name = type_name.parts.back();
            const auto trait_it = imported_it->second.traits.find(trait_name);
            if (trait_it == imported_it->second.traits.end() || !imported_it->second.exports.contains(trait_name)) {
                return nullptr;
            }
            return &trait_it->second;
        }

        const std::string trait_name = type_name.display_name();
        const auto local_trait_it = summary.traits.find(trait_name);
        if (local_trait_it != summary.traits.end()) {
            return &local_trait_it->second;
        }
        for (const auto& imported_name : summary.imported_modules) {
            const auto imported_it = summaries.find(imported_name);
            if (imported_it == summaries.end()) {
                continue;
            }
            const auto trait_it = imported_it->second.traits.find(trait_name);
            if (trait_it != imported_it->second.traits.end() && imported_it->second.exports.contains(trait_name)) {
                return &trait_it->second;
            }
        }
        return nullptr;
    };
    auto check_call_arity = [&](const CheckFunctionInfo& function_info, std::size_t arg_count, const Span& span) -> VoidResult {
        if (function_info.params.size() == arg_count) {
            return ok_result();
        }
        return make_loc_error<std::monostate>(
            module_name,
            span,
            "function '" + function_info.name + "' expects " + std::to_string(function_info.params.size()) + " arguments, got " +
                std::to_string(arg_count) + " (defined at " + format_location(function_info.module_name, function_info.span) + ")");
    };
    auto validate_module_export = [&](Expr* object, const std::string& member, const Span& span) -> VoidResult {
        auto* variable = dynamic_cast<VariableExpr*>(object);
        if (variable == nullptr) {
            return ok_result();
        }
        const auto alias_it = summary.import_aliases.find(variable->name);
        if (alias_it == summary.import_aliases.end()) {
            return ok_result();
        }
        const auto imported_it = summaries.find(alias_it->second);
        if (imported_it == summaries.end()) {
            return ok_result();
        }
        if (!imported_it->second.exports.contains(member)) {
            return make_loc_error<std::monostate>(module_name, span, "module '" + variable->name + "' does not export '" + member + "'");
        }
        return ok_result();
    };
    auto resolve_module_function = [&](Expr* object, const std::string& member) -> const CheckFunctionInfo* {
        auto* variable = dynamic_cast<VariableExpr*>(object);
        if (variable == nullptr) {
            return nullptr;
        }
        const auto alias_it = summary.import_aliases.find(variable->name);
        if (alias_it == summary.import_aliases.end()) {
            return nullptr;
        }
        const auto imported_it = summaries.find(alias_it->second);
        if (imported_it == summaries.end() || !imported_it->second.exports.contains(member)) {
            return nullptr;
        }
        const auto function_it = imported_it->second.functions.find(member);
        return function_it == imported_it->second.functions.end() ? nullptr : &function_it->second;
    };

    std::function<VoidResult(Expr*)> validate_expr;
    std::function<VoidResult(Stmt*)> validate_stmt;
    validate_expr = [&](Expr* expr) -> VoidResult {
        if (expr == nullptr) {
            return ok_result();
        }
        if (dynamic_cast<LiteralExpr*>(expr) != nullptr || dynamic_cast<VariableExpr*>(expr) != nullptr) {
            return ok_result();
        }
        if (auto* interpolated = dynamic_cast<InterpolatedStringExpr*>(expr)) {
            for (auto& segment : interpolated->segments) {
                if (segment.expression) {
                    ZEPHYR_TRY(validate_expr(segment.expression.get()));
                }
            }
            return ok_result();
        }
        if (auto* array = dynamic_cast<ArrayExpr*>(expr)) {
            for (auto& element : array->elements) {
                ZEPHYR_TRY(validate_expr(element.get()));
            }
            return ok_result();
        }
        if (auto* group = dynamic_cast<GroupExpr*>(expr)) {
            return validate_expr(group->inner.get());
        }
        if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
            return validate_expr(unary->right.get());
        }
        if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
            ZEPHYR_TRY(validate_expr(binary->left.get()));
            ZEPHYR_TRY(validate_expr(binary->right.get()));
            return ok_result();
        }
        if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
            ZEPHYR_TRY(validate_expr(assign->target.get()));
            ZEPHYR_TRY(validate_expr(assign->value.get()));
            return ok_result();
        }
        if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
            ZEPHYR_TRY(validate_module_export(member->object.get(), member->member, member->span));
            return validate_expr(member->object.get());
        }
        if (auto* optional_member = dynamic_cast<OptionalMemberExpr*>(expr)) {
            ZEPHYR_TRY(validate_module_export(optional_member->object.get(), optional_member->member, optional_member->span));
            return validate_expr(optional_member->object.get());
        }
        if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
            ZEPHYR_TRY(validate_expr(index->object.get()));
            ZEPHYR_TRY(validate_expr(index->index.get()));
            return ok_result();
        }
        if (auto* call = dynamic_cast<CallExpr*>(expr)) {
            for (auto& argument : call->arguments) {
                ZEPHYR_TRY(validate_expr(argument.get()));
            }
            if (auto* callee_variable = dynamic_cast<VariableExpr*>(call->callee.get())) {
                const auto visible_it = visible_functions.find(callee_variable->name);
                if (visible_it != visible_functions.end()) {
                    ZEPHYR_TRY(check_call_arity(visible_it->second, call->arguments.size(), call->span));
                }
            }
            if (auto* callee_member = dynamic_cast<MemberExpr*>(call->callee.get())) {
                ZEPHYR_TRY(validate_module_export(callee_member->object.get(), callee_member->member, callee_member->span));
                if (dynamic_cast<OptionalMemberExpr*>(callee_member->object.get()) != nullptr) {
                    emit_warning(module_name,
                                 call->span,
                                 "optional chaining result may be nil before calling method '" + callee_member->member + "'");
                }
                if (const auto* imported_function = resolve_module_function(callee_member->object.get(), callee_member->member)) {
                    ZEPHYR_TRY(check_call_arity(*imported_function, call->arguments.size(), call->span));
                }
            }
            return validate_expr(call->callee.get());
        }
        if (auto* optional_call = dynamic_cast<OptionalCallExpr*>(expr)) {
            ZEPHYR_TRY(validate_expr(optional_call->object.get()));
            for (auto& argument : optional_call->arguments) {
                ZEPHYR_TRY(validate_expr(argument.get()));
            }
            return ok_result();
        }
        if (auto* function = dynamic_cast<FunctionExpr*>(expr)) {
            for (auto& statement : function->body->statements) {
                ZEPHYR_TRY(validate_stmt(statement.get()));
            }
            return ok_result();
        }
        if (auto* coroutine = dynamic_cast<CoroutineExpr*>(expr)) {
            for (auto& statement : coroutine->body->statements) {
                ZEPHYR_TRY(validate_stmt(statement.get()));
            }
            return ok_result();
        }
        if (auto* resume = dynamic_cast<ResumeExpr*>(expr)) {
            return validate_expr(resume->target.get());
        }
        if (auto* struct_init = dynamic_cast<StructInitExpr*>(expr)) {
            for (auto& field : struct_init->fields) {
                ZEPHYR_TRY(validate_expr(field.value.get()));
            }
            return ok_result();
        }
        if (auto* enum_init = dynamic_cast<EnumInitExpr*>(expr)) {
            for (auto& argument : enum_init->arguments) {
                ZEPHYR_TRY(validate_expr(argument.get()));
            }
            return ok_result();
        }
        if (auto* match_expr = dynamic_cast<MatchExpr*>(expr)) {
            ZEPHYR_TRY(validate_expr(match_expr->subject.get()));
            bool has_wildcard = false;
            bool has_nil_pattern = false;
            bool has_true_pattern = false;
            bool has_false_pattern = false;
            bool has_string_pattern = false;
            std::function<void(Pattern*)> note_pattern;
            note_pattern = [&](Pattern* pattern) {
                if (pattern == nullptr) {
                    return;
                }
                if (dynamic_cast<WildcardPattern*>(pattern) != nullptr || dynamic_cast<BindingPattern*>(pattern) != nullptr) {
                    has_wildcard = true;
                    return;
                }
                if (auto* literal = dynamic_cast<LiteralPattern*>(pattern)) {
                    if (std::holds_alternative<std::monostate>(literal->value)) {
                        has_nil_pattern = true;
                    } else if (const auto* flag = std::get_if<bool>(&literal->value)) {
                        has_true_pattern = has_true_pattern || *flag;
                        has_false_pattern = has_false_pattern || !*flag;
                    } else if (std::holds_alternative<std::string>(literal->value)) {
                        has_string_pattern = true;
                    }
                    return;
                }
                if (auto* or_pattern = dynamic_cast<OrPattern*>(pattern)) {
                    for (auto& alternative : or_pattern->alternatives) {
                        note_pattern(alternative.get());
                    }
                }
            };
            for (auto& arm : match_expr->arms) {
                note_pattern(arm.pattern.get());
                if (arm.guard_expr) {
                    ZEPHYR_TRY(validate_expr(arm.guard_expr.get()));
                }
                ZEPHYR_TRY(validate_expr(arm.expression.get()));
            }
            if (!has_wildcard) {
                std::vector<std::string> missing_cases;
                if (has_nil_pattern || has_string_pattern) {
                    if (!has_nil_pattern) {
                        missing_cases.push_back("nil");
                    }
                    if (!has_string_pattern) {
                        missing_cases.push_back("string patterns");
                    }
                }
                if (has_true_pattern != has_false_pattern) {
                    missing_cases.push_back(has_true_pattern ? "false pattern" : "true pattern");
                }
                if (!missing_cases.empty()) {
                    emit_warning(module_name, match_expr->span, "match may not cover all cases: missing " + join_strings(missing_cases, ", "));
                }
            }
            return ok_result();
        }
        return ok_result();
    };
    validate_stmt = [&](Stmt* stmt) -> VoidResult {
        if (stmt == nullptr || dynamic_cast<ImportStmt*>(stmt) != nullptr || dynamic_cast<StructDecl*>(stmt) != nullptr ||
            dynamic_cast<EnumDecl*>(stmt) != nullptr || dynamic_cast<TraitDecl*>(stmt) != nullptr ||
            dynamic_cast<BreakStmt*>(stmt) != nullptr || dynamic_cast<ContinueStmt*>(stmt) != nullptr) {
            return ok_result();
        }
        if (auto* let_stmt = dynamic_cast<LetStmt*>(stmt)) {
            ZEPHYR_TRY(validate_expr(let_stmt->initializer.get()));
            if (let_stmt->else_branch) {
                ZEPHYR_TRY(validate_stmt(let_stmt->else_branch.get()));
            }
            return ok_result();
        }
        if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
            for (auto& statement : block->statements) {
                ZEPHYR_TRY(validate_stmt(statement.get()));
            }
            return ok_result();
        }
        if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt)) {
            if (if_stmt->let_pattern != nullptr) {
                ZEPHYR_TRY(validate_expr(if_stmt->let_subject.get()));
            } else {
                ZEPHYR_TRY(validate_expr(if_stmt->condition.get()));
            }
            ZEPHYR_TRY(validate_stmt(if_stmt->then_branch.get()));
            if (if_stmt->else_branch) {
                ZEPHYR_TRY(validate_stmt(if_stmt->else_branch.get()));
            }
            return ok_result();
        }
        if (auto* while_stmt = dynamic_cast<WhileStmt*>(stmt)) {
            if (while_stmt->let_pattern != nullptr) {
                ZEPHYR_TRY(validate_expr(while_stmt->let_subject.get()));
            } else {
                ZEPHYR_TRY(validate_expr(while_stmt->condition.get()));
            }
            return validate_stmt(while_stmt->body.get());
        }
        if (auto* for_stmt = dynamic_cast<ForStmt*>(stmt)) {
            ZEPHYR_TRY(validate_expr(for_stmt->iterable.get()));
            return validate_stmt(for_stmt->body.get());
        }
        if (auto* return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
            return return_stmt->value ? validate_expr(return_stmt->value.get()) : ok_result();
        }
        if (auto* yield_stmt = dynamic_cast<YieldStmt*>(stmt)) {
            return yield_stmt->value ? validate_expr(yield_stmt->value.get()) : ok_result();
        }
        if (auto* function_decl = dynamic_cast<FunctionDecl*>(stmt)) {
            for (auto& statement : function_decl->body->statements) {
                ZEPHYR_TRY(validate_stmt(statement.get()));
            }
            return ok_result();
        }
        if (auto* impl_decl = dynamic_cast<ImplDecl*>(stmt)) {
            const auto* trait_info = resolve_trait(impl_decl->trait_name);
            if (trait_info == nullptr) {
                return make_loc_error<std::monostate>(
                    module_name, impl_decl->span, "Unknown trait '" + impl_decl->trait_name.display_name() + "'.");
            }
            std::unordered_set<std::string> seen_methods;
            for (const auto& method : impl_decl->methods) {
                if (method == nullptr) {
                    continue;
                }
                if (!seen_methods.insert(method->name).second) {
                    return make_loc_error<std::monostate>(
                        module_name,
                        method->span,
                        "Impl for '" + impl_decl->for_type.display_name() + "' defines method '" + method->name + "' more than once.");
                }
                const auto trait_method_it = trait_info->methods.find(method->name);
                if (trait_method_it == trait_info->methods.end()) {
                    emit_warning(module_name, method->span, "method '" + method->name + "' is not part of trait '" +
                                                              impl_decl->trait_name.display_name() + "'");
                } else {
                    const auto& trait_method = trait_method_it->second;
                    bool mismatch = method->params.size() != trait_method.params.size() ||
                                    !same_type(method->return_type, trait_method.return_type);
                    if (!mismatch) {
                        for (std::size_t index = 0; index < method->params.size(); ++index) {
                            if (!same_type(method->params[index].type, trait_method.params[index].type)) {
                                mismatch = true;
                                break;
                            }
                        }
                    }
                    if (mismatch) {
                        return make_loc_error<std::monostate>(
                            module_name,
                            method->span,
                            "Trait method '" + method->name + "' in impl of '" + impl_decl->trait_name.display_name() + "' for '" +
                                impl_decl->for_type.display_name() + "' does not match the trait signature. expected " +
                                render_signature(trait_method.name, trait_method.params, trait_method.return_type) + ", got " +
                                render_signature(method->name, method->params, method->return_type));
                    }
                }
                for (auto& nested_statement : method->body->statements) {
                    ZEPHYR_TRY(validate_stmt(nested_statement.get()));
                }
            }
            for (const auto& [method_name, _] : trait_info->methods) {
                if (!seen_methods.contains(method_name)) {
                    return make_loc_error<std::monostate>(
                        module_name,
                        impl_decl->span,
                        "error: impl of '" + impl_decl->trait_name.display_name() + "' for '" + impl_decl->for_type.display_name() +
                            "' is missing method '" + method_name + "'");
                }
            }
            return ok_result();
        }
        if (auto* expression_stmt = dynamic_cast<ExprStmt*>(stmt)) {
            return validate_expr(expression_stmt->expression.get());
        }
        return ok_result();
    };

    for (const auto& statement : program.statements) {
        ZEPHYR_TRY(validate_stmt(statement.get()));
    }

    visiting.erase(module_name);
    summaries[module_name] = std::move(summary);
    checked.insert(module_name);
    return ok_result();
}

Binding* Runtime::lookup_binding(Environment* environment, const std::string& name) {
    Binding* result = nullptr;
    walk_environment_chain(environment, [&](Environment* current) {
        const auto it = current->values.find(name);
        if (it != current->values.end()) {
            result = &it->second;
            return false;
        }
        return true;
    });
    return result;
}

RuntimeResult<Value> Runtime::lookup_value(Environment* environment, const std::string& name, const Span& span,
                                           const std::string& module_name,
                                           const std::vector<std::string>& extra_hint_names) {
    Binding* binding = lookup_binding(environment, name);
    if (binding == nullptr) {
        std::vector<std::string> all_names = extra_hint_names;
        if (environment != nullptr) {
            environment->collect_names(all_names);
        }
        std::string source_text;
        auto mod_it = modules_.find(module_name);
        if (mod_it != modules_.end()) {
            source_text = mod_it->second.source_text;
        }
        std::string context = format_source_context(source_text, span, name.size());
        std::string message = context + "Unknown identifier '" + name + "'.";
        const auto suggestion = suggest_similar_name(name, all_names);
        if (suggestion) {
            message += "\nhint: did you mean '" + *suggestion + "'?";
        }
        return make_loc_error<Value>(module_name, span, message);
    }
    return read_binding_value(*binding);
}

void Runtime::define_value(Environment* environment, const std::string& name, Value value, bool mutable_value,
                           std::optional<std::string> type_name) {
    if (environment != nullptr) {
        if ((environment->kind == EnvironmentKind::Root || environment->kind == EnvironmentKind::Module) && value.is_host_handle()) {
            auto result = lookup_host_handle_entry(value.as_host_handle(), Span{}, "<internal>", "global binding '" + name + "'");
            if (!result) {
                fail(result.error());
            }
            if (!handle_store_allowed((*result)->lifetime, (*result)->policy, HandleContainerKind::Global)) {
                fail("Host handle cannot be stored in global binding '" + name + "'.");
            }
        }
    }
    Binding binding;
    binding.value = value;
    binding.mutable_value = mutable_value;
    binding.type_name = std::move(type_name);
    const std::size_t binding_index = ensure_environment_binding_slot(environment, name);
    environment->values[name] = std::move(binding);
    ++environment->version;
    if (environment != nullptr) {
        note_environment_binding_write(environment, binding_index, value);
    }
}

VoidResult Runtime::assign_value(Environment* environment, const std::string& name, Value value, const Span& span,
                                 const std::string& module_name) {
    for (Environment* current = environment; current != nullptr; current = current->parent) {
        auto it = current->values.find(name);
        if (it == current->values.end()) {
            continue;
        }
        if (!it->second.mutable_value) {
            return make_loc_error<std::monostate>(module_name, span, "Cannot assign to immutable binding '" + name + "'.");
        }
        ZEPHYR_TRY(enforce_type(value, it->second.type_name, span, module_name, "assignment"));
        if (current->kind == EnvironmentKind::Root || current->kind == EnvironmentKind::Module) {
            ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::Global, span, module_name, "global assignment"));
        }
        if (it->second.cell != nullptr) {
            const std::string capture_context = it->second.cell->container_kind == HandleContainerKind::CoroutineFrame
                                                   ? "coroutine capture assignment"
                                                   : "closure capture assignment";
            ZEPHYR_TRY(validate_handle_store(value, it->second.cell->container_kind, span, module_name, capture_context));
        }
        write_binding_value(it->second, value);
        note_environment_binding_write(current, ensure_environment_binding_slot(current, name), value);
        if (it->second.cell != nullptr) {
            note_write(static_cast<GcObject*>(it->second.cell), value);
        }
        return ok_result();
    }
    return make_loc_error<std::monostate>(module_name, span, "Unknown identifier '" + name + "'.");
}

bool Runtime::is_truthy(const Value& value) const {
    if (value.is_nil()) {
        return false;
    }
    if (value.is_bool()) {
        return value.as_bool();
    }
    if (value.is_int()) {
        return value.as_int() != 0;
    }
    if (value.is_float()) {
        return std::abs(value.as_float()) > 0.0;
    }
    return true;
}

bool Runtime::values_equal(const Value& left, const Value& right) const {
    if (left.is_number() && right.is_number()) {
        return std::abs(left.as_float() - right.as_float()) < 1e-9;
    }
    if (left.is_nil() || right.is_nil()) {
        return left.is_nil() && right.is_nil();
    }
    if (left.is_bool() || right.is_bool()) {
        return left.is_bool() && right.is_bool() && left.as_bool() == right.as_bool();
    }
    if (left.is_host_handle() || right.is_host_handle()) {
        if (!left.is_host_handle() || !right.is_host_handle()) {
            return false;
        }
        const HostHandleToken left_token = left.as_host_handle();
        const HostHandleToken right_token = right.as_host_handle();
        if (left_token.slot == right_token.slot && left_token.generation == right_token.generation) {
            return true;
        }
        if (left_token.slot >= host_handles_.size() || right_token.slot >= host_handles_.size()) {
            return false;
        }
        const HostHandleEntry& left_entry = host_handles_[left_token.slot];
        const HostHandleEntry& right_entry = host_handles_[right_token.slot];
        if (left_entry.host_class.get() != right_entry.host_class.get()) {
            return false;
        }
        if (left_entry.stable_guid.valid() && left_entry.stable_guid == right_entry.stable_guid) {
            return true;
        }
        const auto left_instance = left_entry.residency_owner ? left_entry.residency_owner : left_entry.instance.lock();
        const auto right_instance = right_entry.residency_owner ? right_entry.residency_owner : right_entry.instance.lock();
        return left_instance.get() != nullptr && left_instance.get() == right_instance.get();
    }
    if (left.is_object() || right.is_object()) {
        if (!left.is_object() || !right.is_object()) {
            return false;
        }
        GcObject* lhs = left.as_object();
        GcObject* rhs = right.as_object();
        if (lhs->kind != rhs->kind) {
            return false;
        }
        if (lhs->kind == ObjectKind::String) {
            const auto* lhs_string = static_cast<StringObject*>(lhs);
            const auto* rhs_string = static_cast<StringObject*>(rhs);
            if (lhs_string->is_interned && rhs_string->is_interned) {
                return lhs_string == rhs_string;
            }
            return lhs_string->value == rhs_string->value;
        }
        return lhs == rhs;
    }
    return false;
}

bool Runtime::matches_type(const Value& value, const std::string& type_name) const {
    if (type_name.empty() || type_name == "any") {
        return true;
    }
    if (type_name == "Nil") {
        return value.is_nil();
    }
    if (type_name == "bool") {
        return value.is_bool();
    }
    if (type_name == "int") {
        return value.is_int();
    }
    if (type_name == "float") {
        return value.is_float();
    }
    if (type_name == "Number") {
        return value.is_number();
    }
    if (type_name == "string") {
        return value.is_object() && value.as_object()->kind == ObjectKind::String;
    }
    if (type_name == "Array") {
        return value.is_object() && value.as_object()->kind == ObjectKind::Array;
    }
    if (type_name == "Function") {
        return value.is_object() &&
               (value.as_object()->kind == ObjectKind::ScriptFunction || value.as_object()->kind == ObjectKind::NativeFunction);
    }
    if (type_name == "Coroutine") {
        return value.is_object() && value.as_object()->kind == ObjectKind::Coroutine;
    }
    if (type_name == "HostObject") {
        return value.is_host_handle();
    }
    if (value.is_object() && value.as_object()->kind == ObjectKind::StructInstance) {
        return static_cast<StructInstanceObject*>(value.as_object())->type->name == type_name;
    }
    if (value.is_object() && value.as_object()->kind == ObjectKind::EnumInstance) {
        return static_cast<EnumInstanceObject*>(value.as_object())->type->name == type_name;
    }
    return false;
}

std::string Runtime::describe_value_type(const Value& value) const {
    if (value.is_nil()) {
        return "Nil";
    }
    if (value.is_bool()) {
        return "bool";
    }
    if (value.is_int()) {
        return "int";
    }
    if (value.is_float()) {
        return "float";
    }
    if (value.is_string()) {
        return "string";
    }
    if (value.is_host_handle()) {
        return "HostObject";
    }
    if (!value.is_object()) {
        return "Unknown";
    }

    switch (value.as_object()->kind) {
        case ObjectKind::Array:
            return "Array";
        case ObjectKind::String:
            return "string";
        case ObjectKind::NativeFunction:
        case ObjectKind::ScriptFunction:
            return "Function";
        case ObjectKind::StructType:
            return static_cast<const StructTypeObject*>(value.as_object())->name;
        case ObjectKind::StructInstance:
            return static_cast<const StructInstanceObject*>(value.as_object())->type->name;
        case ObjectKind::EnumType:
            return static_cast<const EnumTypeObject*>(value.as_object())->name;
        case ObjectKind::EnumInstance:
            return static_cast<const EnumInstanceObject*>(value.as_object())->type->name;
        case ObjectKind::Coroutine:
            return "Coroutine";
        case ObjectKind::ModuleNamespace:
            return "Module";
        case ObjectKind::Environment:
            return "Environment";
        case ObjectKind::UpvalueCell:
            return "UpvalueCell";
    }
    return "Unknown";
}

VoidResult Runtime::enforce_type(const Value& value, const std::optional<std::string>& type_name, const Span& span,
                                 const std::string& module_name, const std::string& context) const {
    if (!type_name.has_value()) {
        return ok_result();
    }
    if (!matches_type(value, *type_name)) {
        return make_loc_error<std::monostate>(
            module_name,
            span,
            "Type mismatch in " + context + ". Expected '" + *type_name + "', got '" + describe_value_type(value) + "'.");
    }
    return ok_result();
}

void Runtime::emit_warning(const std::string& module_name, const Span& span, const std::string& message) const {
    std::cerr << format_location(module_name, span) << ": warning: " << message << std::endl;
}

std::string Runtime::append_stack_frame(std::string message,
                                        const std::string& function_name,
                                        const std::string& module_name,
                                        const Span& span) const {
    if (function_name.empty()) {
        return message;
    }
    message += "\n  at " + function_name + " (" + format_location(module_name.empty() ? "<script>" : module_name, span) + ")";
    return message;
}

std::string Runtime::append_coroutine_stack_trace(std::string message, const CoroutineObject* coroutine) const {
    if (coroutine == nullptr) {
        return message;
    }
    for (auto it = coroutine->frames.rbegin(); it != coroutine->frames.rend(); ++it) {
        const auto& frame = *it;
        if (frame.bytecode == nullptr) {
            continue;
        }
        const Span span = frame.ip < frame.bytecode->instructions.size() ? instruction_span(frame.bytecode->instructions[frame.ip]) : Span{1, 1};
        message = append_stack_frame(message,
                                     frame.bytecode->name.empty() ? std::string("<coroutine>") : frame.bytecode->name,
                                     frame.module_name.empty() ? std::string("<coroutine>") : frame.module_name,
                                     span);
    }
    return message;
}

std::string Runtime::describe_match_missing_case(const Value& value) const {
    if (value.is_nil()) {
        return "nil pattern";
    }
    if (value.is_bool()) {
        return std::string(value.as_bool() ? "true" : "false") + " pattern";
    }
    if (value.is_string()) {
        return "string patterns";
    }
    if (value.is_int()) {
        return "Int patterns";
    }
    if (value.is_float()) {
        return "Float patterns";
    }
    if (value.is_object()) {
        switch (value.as_object()->kind) {
            case ObjectKind::EnumInstance: {
                const auto* enum_value = static_cast<const EnumInstanceObject*>(value.as_object());
                return enum_value->type->name + "::" + enum_value->variant + " pattern";
            }
            case ObjectKind::StructInstance:
                return static_cast<const StructInstanceObject*>(value.as_object())->type->name + " patterns";
            default:
                break;
        }
    }
    return describe_value_type(value) + " patterns";
}

std::string Runtime::value_to_string(const Value& value) const {
    if (value.is_nil()) {
        return "nil";
    }
    if (value.is_bool()) {
        return value.as_bool() ? "true" : "false";
    }
    if (value.is_int()) {
        return std::to_string(value.as_int());
    }
    if (value.is_float()) {
        std::ostringstream out;
        out << value.as_float();
        return out.str();
    }
    if (value.is_host_handle()) {
        const HostHandleToken token = value.as_host_handle();
        if (token.slot < host_handles_.size()) {
            const HostHandleEntry& entry = host_handles_[token.slot];
            const std::string class_name = entry.host_class ? entry.host_class->name() : std::string("host");
            return "<host:" + class_name + ">";
        }
        return "<host:invalid>";
    }
    if (!value.is_object()) {
        return "<value>";
    }

    GcObject* object = value.as_object();
    switch (object->kind) {
        case ObjectKind::String:
            return static_cast<StringObject*>(object)->value;
        case ObjectKind::Array: {
            const auto* array = static_cast<ArrayObject*>(object);
            std::ostringstream out;
            out << "[";
            for (std::size_t i = 0; i < array->elements.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << value_to_string(array->elements[i]);
            }
            out << "]";
            return out.str();
        }
        case ObjectKind::StructInstance: {
            const auto* instance = static_cast<StructInstanceObject*>(object);
            std::ostringstream out;
            out << instance->type->name << " { ";
            bool first = true;
            for (std::size_t field_index = 0; field_index < instance->field_values.size() && field_index < instance->type->fields.size(); ++field_index) {
                if (!first) {
                    out << ", ";
                }
                first = false;
                out << instance->type->fields[field_index].name << ": " << value_to_string(instance->field_values[field_index]);
            }
            out << " }";
            return out.str();
        }
        case ObjectKind::EnumInstance: {
            const auto* instance = static_cast<EnumInstanceObject*>(object);
            std::ostringstream out;
            out << instance->type->name << "::" << instance->variant;
            if (!instance->payload.empty()) {
                out << "(";
                for (std::size_t i = 0; i < instance->payload.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << value_to_string(instance->payload[i]);
                }
                out << ")";
            }
            return out.str();
        }
        case ObjectKind::ScriptFunction:
            return "<fn>";
        case ObjectKind::NativeFunction:
            return "<native fn>";
        case ObjectKind::UpvalueCell:
            return "<upvalue>";
        case ObjectKind::ModuleNamespace:
            return "<module " + static_cast<ModuleNamespaceObject*>(object)->name + ">";
        case ObjectKind::StructType:
            return "<struct " + static_cast<StructTypeObject*>(object)->name + ">";
        case ObjectKind::EnumType:
            return "<enum " + static_cast<EnumTypeObject*>(object)->name + ">";
        case ObjectKind::Environment:
            return "<env>";
        case ObjectKind::Coroutine:
            return "<coroutine>";
    }
    return "<value>";
}

RuntimeResult<StructTypeObject*> Runtime::expect_struct_type(Environment* environment, const TypeRef& type_name,
                                                             const std::string& module_name, const Span& span) {
    if (type_name.parts.empty()) {
        return make_loc_error<StructTypeObject*>(module_name, span, "Expected struct type name.");
    }
    if (type_name.parts.size() == 1) {
        ZEPHYR_TRY_ASSIGN(value, lookup_value(environment, type_name.parts.front(), span, module_name));
        if (!value.is_object() || value.as_object()->kind != ObjectKind::StructType) {
            return make_loc_error<StructTypeObject*>(module_name, span, "'" + type_name.display_name() + "' is not a struct type.");
        }
        return static_cast<StructTypeObject*>(value.as_object());
    }

    ZEPHYR_TRY_ASSIGN(module_value, lookup_value(environment, type_name.parts.front(), span, module_name));
    ZEPHYR_TRY_ASSIGN(namespace_object, expect_module_namespace(module_value, span, module_name));
    Binding* binding = lookup_binding(namespace_object->environment, type_name.parts.back());
    const Value bound_value = binding == nullptr ? Value::nil() : read_binding_value(*binding);
    if (binding == nullptr || !bound_value.is_object() || bound_value.as_object()->kind != ObjectKind::StructType) {
        return make_loc_error<StructTypeObject*>(module_name, span, "'" + type_name.display_name() + "' is not a struct type.");
    }
    return static_cast<StructTypeObject*>(bound_value.as_object());
}

RuntimeResult<EnumTypeObject*> Runtime::expect_enum_type(Environment* environment, const TypeRef& type_name,
                                                         const std::string& module_name, const Span& span) {
    if (type_name.parts.empty()) {
        return make_loc_error<EnumTypeObject*>(module_name, span, "Expected enum type name.");
    }
    if (type_name.parts.size() == 1) {
        ZEPHYR_TRY_ASSIGN(value, lookup_value(environment, type_name.parts.front(), span, module_name));
        if (!value.is_object() || value.as_object()->kind != ObjectKind::EnumType) {
            return make_loc_error<EnumTypeObject*>(module_name, span, "'" + type_name.display_name() + "' is not an enum type.");
        }
        return static_cast<EnumTypeObject*>(value.as_object());
    }

    ZEPHYR_TRY_ASSIGN(module_value, lookup_value(environment, type_name.parts.front(), span, module_name));
    ZEPHYR_TRY_ASSIGN(namespace_object, expect_module_namespace(module_value, span, module_name));
    Binding* binding = lookup_binding(namespace_object->environment, type_name.parts.back());
    const Value bound_value = binding == nullptr ? Value::nil() : read_binding_value(*binding);
    if (binding == nullptr || !bound_value.is_object() || bound_value.as_object()->kind != ObjectKind::EnumType) {
        return make_loc_error<EnumTypeObject*>(module_name, span, "'" + type_name.display_name() + "' is not an enum type.");
    }
    return static_cast<EnumTypeObject*>(bound_value.as_object());
}

RuntimeResult<ModuleNamespaceObject*> Runtime::expect_module_namespace(const Value& value, const Span& span,
                                                                       const std::string& module_name) {
    if (!value.is_object() || value.as_object()->kind != ObjectKind::ModuleNamespace) {
        return make_loc_error<ModuleNamespaceObject*>(module_name, span, "Expected module namespace.");
    }
    return static_cast<ModuleNamespaceObject*>(value.as_object());
}

RuntimeResult<Value> Runtime::evaluate_literal(const LiteralExpr& expr) {
    if (std::holds_alternative<std::monostate>(expr.value)) {
        return Value::nil();
    }
    if (const auto* boolean = std::get_if<bool>(&expr.value)) {
        return Value::boolean(*boolean);
    }
    if (const auto* integer = std::get_if<std::int64_t>(&expr.value)) {
        return Value::integer(*integer);
    }
    if (const auto* floating = std::get_if<double>(&expr.value)) {
        return Value::floating(*floating);
    }
    return make_literal_string(std::get<std::string>(expr.value));
}

RuntimeResult<Value> Runtime::evaluate_interpolated_string(Environment* environment, InterpolatedStringExpr& expr, const std::string& module_name) {
    std::string result;
    for (auto& segment : expr.segments) {
        if (segment.is_literal) {
            result += segment.literal;
        } else {
            ZEPHYR_TRY_ASSIGN(value, evaluate(environment, segment.expression.get(), module_name));
            result += value_to_string(value);
        }
    }
    return make_string(std::move(result));
}

RuntimeResult<Value> Runtime::evaluate_array(Environment* environment, ArrayExpr& expr, const std::string& module_name) {
    auto* array = allocate<ArrayObject>();
    for (auto& element : expr.elements) {
        ZEPHYR_TRY_ASSIGN(element_value, evaluate(environment, element.get(), module_name));
        ZEPHYR_TRY(validate_handle_store(element_value, HandleContainerKind::ArrayElement, expr.span, module_name, "array literal"));
        const std::size_t index = array->elements.size();
        array->elements.push_back(element_value);
        note_array_element_write(array, index, element_value);
    }
    return Value::object(array);
}

RuntimeResult<double> numeric_value(const Value& value, const std::string& module_name, const Span& span) {
    if (!value.is_number()) {
        return make_loc_error<double>(module_name, span, "Expected number.");
    }
    return value.as_float();
}

RuntimeResult<Value> Runtime::evaluate_binary(Environment* environment, BinaryExpr& expr, const std::string& module_name) {
    if (expr.op == TokenType::AndAnd) {
        ZEPHYR_TRY_ASSIGN(left, evaluate(environment, expr.left.get(), module_name));
        if (!is_truthy(left)) {
            return Value::boolean(false);
        }
        ZEPHYR_TRY_ASSIGN(right, evaluate(environment, expr.right.get(), module_name));
        return Value::boolean(is_truthy(right));
    }

    if (expr.op == TokenType::OrOr) {
        ZEPHYR_TRY_ASSIGN(left, evaluate(environment, expr.left.get(), module_name));
        if (is_truthy(left)) {
            return Value::boolean(true);
        }
        ZEPHYR_TRY_ASSIGN(right, evaluate(environment, expr.right.get(), module_name));
        return Value::boolean(is_truthy(right));
    }

    ZEPHYR_TRY_ASSIGN(left, evaluate(environment, expr.left.get(), module_name));
    ZEPHYR_TRY_ASSIGN(right, evaluate(environment, expr.right.get(), module_name));
    return apply_binary_op(expr.op, left, right, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_unary(Environment* environment, UnaryExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(right, evaluate(environment, expr.right.get(), module_name));
    return apply_unary_op(expr.op, right, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_assign(Environment* environment, AssignExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(rhs_value, evaluate(environment, expr.value.get(), module_name));
    auto combine_value = [&](const Value& current) -> RuntimeResult<Value> {
        if (expr.assignment_op == TokenType::Equal) {
            return rhs_value;
        }
        const auto op = compound_assignment_binary_op(expr.assignment_op);
        if (!op.has_value()) {
            return make_loc_error<Value>(module_name, expr.span, "Unsupported compound assignment operator.");
        }
        return apply_binary_op(*op, current, rhs_value, expr.span, module_name);
    };
    if (auto* variable = dynamic_cast<VariableExpr*>(expr.target.get())) {
        Value value = rhs_value;
        if (expr.assignment_op != TokenType::Equal) {
            ZEPHYR_TRY_ASSIGN(current, lookup_value(environment, variable->name, variable->span, module_name));
            ZEPHYR_TRY_ASSIGN(combined, combine_value(current));
            value = combined;
        }
        ZEPHYR_TRY(assign_value(environment, variable->name, value, variable->span, module_name));
        return value;
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr.target.get())) {
        ZEPHYR_TRY_ASSIGN(object, evaluate(environment, member->object.get(), module_name));
        Value value = rhs_value;
        if (expr.assignment_op != TokenType::Equal) {
            ZEPHYR_TRY_ASSIGN(current, get_member_value(object, member->member, member->span, module_name));
            ZEPHYR_TRY_ASSIGN(combined, combine_value(current));
            value = combined;
        }
        return set_member_value(object, member->member, value, member->span, module_name);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr.target.get())) {
        ZEPHYR_TRY_ASSIGN(object, evaluate(environment, index->object.get(), module_name));
        ZEPHYR_TRY_ASSIGN(position, evaluate(environment, index->index.get(), module_name));
        Value value = rhs_value;
        if (expr.assignment_op != TokenType::Equal) {
            ZEPHYR_TRY_ASSIGN(current, get_index_value(object, position, index->span, module_name));
            ZEPHYR_TRY_ASSIGN(combined, combine_value(current));
            value = combined;
        }
        return set_index_value(object, position, value, index->span, module_name);
    }
    return make_loc_error<Value>(module_name, expr.span, "Invalid assignment target.");
}

RuntimeResult<Value> Runtime::evaluate_member(Environment* environment, MemberExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(object, evaluate(environment, expr.object.get(), module_name));
    return get_member_value(object, expr.member, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_optional_member(Environment* environment, OptionalMemberExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(object, evaluate(environment, expr.object.get(), module_name));
    if (object.is_nil()) {
        return Value::nil();
    }
    return get_member_value(object, expr.member, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_index(Environment* environment, IndexExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(object, evaluate(environment, expr.object.get(), module_name));
    ZEPHYR_TRY_ASSIGN(index, evaluate(environment, expr.index.get(), module_name));
    return get_index_value(object, index, expr.span, module_name);
}

RuntimeResult<Value> Runtime::call_value(const Value& callee, const std::vector<Value>& args, const Span& call_span,
                                         const std::string& module_name) {
    ScopedVectorItem<const Value*> callee_root(rooted_values_, &callee);
    ScopedVectorItem<const std::vector<Value>*> args_root(rooted_value_vectors_, &args);
    struct ActiveProfileScope {
        Runtime& runtime;

        ~ActiveProfileScope() {
            runtime.end_profile_scope();
        }
    };
    if (!callee.is_object()) {
        return make_loc_error<Value>(module_name, call_span, "Attempted to call a non-function value.");
    }

    GcObject* raw = callee.as_object();
    if (raw->kind == ObjectKind::NativeFunction) {
        auto* function = static_cast<NativeFunctionObject*>(raw);
        auto invoke_native = [&]() -> RuntimeResult<Value> {
            if (!function->param_types.empty() && function->param_types.size() != args.size()) {
                return make_loc_error<Value>(module_name,
                                             call_span,
                                             "function '" + function->name + "' expects " +
                                                 std::to_string(function->param_types.size()) + " arguments, got " +
                                                 std::to_string(args.size()));
            }
            auto public_args_lease = acquire_public_args_buffer(args.size());
            auto& public_args = public_args_lease.args();
            for (std::size_t i = 0; i < args.size(); ++i) {
                const std::optional<std::string> expected =
                    i < function->param_types.size() ? std::optional<std::string>(function->param_types[i]) : std::nullopt;
                ZEPHYR_TRY(enforce_type(args[i], expected, call_span, module_name, "native call"));
                public_args.push_back(to_public_value(args[i]));
            }
            Value result;
            try {
                ++callback_invocations_;
                result = from_public_value(function->callback(public_args));
            } catch (const std::exception& error) {
                return make_loc_error<Value>(module_name, call_span, error.what());
            }
            ZEPHYR_TRY(enforce_type(result, function->return_type, call_span, module_name, "native return"));
            return result;
        };
        if (profiling_active_) {
            begin_profile_scope(function->name);
            ActiveProfileScope profile_scope{*this};
            auto result = invoke_native();
            if (!result) {
                return std::unexpected(append_stack_frame(std::move(result.error()), function->name, module_name, call_span));
            }
            return result;
        }
        auto result = invoke_native();
        if (!result) {
            return std::unexpected(append_stack_frame(std::move(result.error()), function->name, module_name, call_span));
        }
        return result;
    }

    if (raw->kind == ObjectKind::ScriptFunction) {
        auto* function = static_cast<ScriptFunctionObject*>(raw);
        const std::string execution_module = function->module_name.empty() ? module_name : function->module_name;
        auto invoke_script = [&]() -> RuntimeResult<Value> {
            const std::string definition_location = format_location(execution_module, function->definition_span);
            if (function->params.size() != args.size()) {
                return make_loc_error<Value>(module_name,
                                             call_span,
                                             "function '" + function->name + "' expects " +
                                                 std::to_string(function->params.size()) + " arguments, got " +
                                                 std::to_string(args.size()) + " (defined at " + definition_location + ")");
            }

            for (const auto& bound : function->where_clauses) {
                for (std::size_t i = 0; i < function->params.size(); ++i) {
                    const auto& param = function->params[i];
                    if (param.type.has_value() && param.type->display_name() == bound.type_param) {
                        const std::string concrete_type = describe_value_type(args[i]);
                        for (const auto& trait_name : bound.traits) {
                            const auto impls_it = trait_impls_.find(concrete_type);
                            if (impls_it == trait_impls_.end() || impls_it->second.find(trait_name) == impls_it->second.end()) {
                                return make_loc_error<Value>(module_name, call_span,
                                    "type '" + concrete_type + "' does not implement trait '" + trait_name + "'");
                            }
                        }
                    }
                }
            }

            if (function->bytecode != nullptr &&
                (function->bytecode->uses_register_mode || function->bytecode->uses_only_locals_and_upvalues)) {
                if (function->bytecode->uses_only_locals_and_upvalues) {
                    ++lightweight_calls_;
                }
                Value result = Value::nil();
                for (std::size_t i = 0; i < function->params.size(); ++i) {
                    const auto& param = function->params[i];
                    const bool is_generic = param.type.has_value() &&
                        std::find(function->generic_params.begin(), function->generic_params.end(), param.type->display_name()) != function->generic_params.end();
                    const std::optional<std::string> type_name =
                        (param.type.has_value() && !is_generic) ? std::optional<std::string>(param.type->display_name()) : std::nullopt;
                    auto type_result = enforce_type(args[i], type_name, param.span, execution_module, "parameter '" + param.name + "'");
                    if (!type_result) {
                        return make_loc_error<Value>(module_name,
                                                     call_span,
                                                     "function '" + function->name + "' argument " + std::to_string(i + 1) +
                                                         " for parameter '" + param.name + "' expects '" +
                                                         (type_name.has_value() ? *type_name : std::string("any")) + "', got '" +
                                                         describe_value_type(args[i]) + "' (defined at " + definition_location + ")");
                    }
                }
                ModuleRecord fake_module;
                fake_module.name = execution_module;
                fake_module.environment = function->closure;
                if (function->bytecode->uses_register_mode) {
                    ZEPHYR_TRY_ASSIGN(register_result,
                                      execute_register_bytecode(*function->bytecode,
                                                                function->params,
                                                                function->closure,
                                                                fake_module,
                                                                call_span,
                                                                &function->captured_upvalues,
                                                                &args));
                    result = register_result;
                } else {
                    ZEPHYR_TRY_ASSIGN(lightweight_result,
                                      execute_bytecode_chunk(*function->bytecode,
                                                             function->params,
                                                             function->closure,
                                                             fake_module,
                                                             call_span,
                                                             &function->captured_upvalues,
                                                             &args));
                    result = lightweight_result;
                }
                const bool return_is_generic = function->return_type.has_value() &&
                    std::find(function->generic_params.begin(), function->generic_params.end(), function->return_type->display_name()) != function->generic_params.end();
                const std::optional<std::string> return_type =
                    (function->return_type.has_value() && !return_is_generic) ? std::optional<std::string>(function->return_type->display_name()) : std::nullopt;
                ZEPHYR_TRY(enforce_type(result, return_type, call_span, execution_module, "return"));
                return result;
            }

            auto* call_env = allocate<Environment>(function->closure);
            ScopedVectorPush<Environment> scope(active_environments_, call_env);
            if (function->bytecode != nullptr) {
                install_upvalue_bindings(call_env, *function->bytecode, function->captured_upvalues);
            }
            for (std::size_t i = 0; i < function->params.size(); ++i) {
                const auto& param = function->params[i];
                const bool is_generic = param.type.has_value() &&
                    std::find(function->generic_params.begin(), function->generic_params.end(), param.type->display_name()) != function->generic_params.end();
                const std::optional<std::string> type_name =
                    (param.type.has_value() && !is_generic) ? std::optional<std::string>(param.type->display_name()) : std::nullopt;
                auto type_result = enforce_type(args[i], type_name, param.span, execution_module, "parameter '" + param.name + "'");
                if (!type_result) {
                    return make_loc_error<Value>(module_name,
                                                 call_span,
                                                 "function '" + function->name + "' argument " + std::to_string(i + 1) +
                                                     " for parameter '" + param.name + "' expects '" +
                                                     (type_name.has_value() ? *type_name : std::string("any")) + "', got '" +
                                                     describe_value_type(args[i]) + "' (defined at " + definition_location + ")");
                }
                define_value(call_env, param.name, args[i], true, type_name);
            }

            ModuleRecord fake_module;
            fake_module.name = execution_module;
            fake_module.environment = call_env;
            ZEPHYR_TRY_ASSIGN(result, execute_bytecode(function, call_env, fake_module, call_span));
            const bool ret_is_generic = function->return_type.has_value() &&
                std::find(function->generic_params.begin(), function->generic_params.end(), function->return_type->display_name()) != function->generic_params.end();
            const std::optional<std::string> return_type =
                (function->return_type.has_value() && !ret_is_generic) ? std::optional<std::string>(function->return_type->display_name()) : std::nullopt;
            ZEPHYR_TRY(enforce_type(result, return_type, call_span, execution_module, "return"));
            return result;
        };
        if (profiling_active_) {
            begin_profile_scope(function->name);
            ActiveProfileScope profile_scope{*this};
            auto result = invoke_script();
            if (!result) {
                return std::unexpected(append_stack_frame(std::move(result.error()), function->name, execution_module, function->definition_span));
            }
            return result;
        }
        auto result = invoke_script();
        if (!result) {
            return std::unexpected(append_stack_frame(std::move(result.error()), function->name, execution_module, function->definition_span));
        }
        return result;
    }

    return make_loc_error<Value>(module_name, call_span, "Attempted to call non-callable object.");
}

RuntimeResult<Value> Runtime::evaluate_call(Environment* environment, CallExpr& expr, const std::string& module_name) {
    std::vector<Value> arguments;
    arguments.reserve(expr.arguments.size());
    for (auto& argument : expr.arguments) {
        ZEPHYR_TRY_ASSIGN(argument_value, evaluate(environment, argument.get(), module_name));
        arguments.push_back(argument_value);
    }

    if (auto* member = dynamic_cast<MemberExpr*>(expr.callee.get())) {
        ZEPHYR_TRY_ASSIGN(object, evaluate(environment, member->object.get(), module_name));
        return call_member_value(object, member->member, arguments, expr.span, module_name);
    }

    ZEPHYR_TRY_ASSIGN(callee, evaluate(environment, expr.callee.get(), module_name));
    return call_value(callee, arguments, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_optional_call(Environment* environment, OptionalCallExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(object, evaluate(environment, expr.object.get(), module_name));
    if (object.is_nil()) {
        return Value::nil();
    }

    std::vector<Value> arguments;
    arguments.reserve(expr.arguments.size());
    for (auto& argument : expr.arguments) {
        ZEPHYR_TRY_ASSIGN(argument_value, evaluate(environment, argument.get(), module_name));
        arguments.push_back(argument_value);
    }
    return call_member_value(object, expr.member, arguments, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_function(Environment* environment, FunctionExpr& expr) {
    ZEPHYR_TRY_ASSIGN(function,
                      create_script_function("<anonymous>",
                                             "<anonymous>",
                                             expr.params,
                                             expr.return_type,
                                             expr.body.get(),
                                             environment,
                                             compile_bytecode_function("<anonymous>", expr.params, expr.body.get()),
                                             expr.span,
                                             std::vector<std::string>{}));
    return Value::object(function);
}

RuntimeResult<Value> Runtime::evaluate_coroutine(Environment* environment, CoroutineExpr& expr, const std::string& module_name) {
    if (!expr.params.empty()) {
        return make_loc_error<Value>(module_name, expr.span, "coroutine fn expressions do not support parameters yet.");
    }
    ZEPHYR_TRY(ensure_capture_cells(environment, HandleContainerKind::CoroutineFrame, expr.span, module_name));
    auto bytecode = compile_bytecode_function("<coroutine>", expr.params, expr.body.get());
    ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(bytecode.get(), expr.span, module_name, "Coroutine expression"));
    auto* coroutine = allocate<CoroutineObject>(
        module_name,
        select_closure_environment(environment, bytecode),
        bytecode,
        expr.return_type.has_value() ? std::optional<std::string>(expr.return_type->display_name()) : std::nullopt);
    ensure_coroutine_trace_id(coroutine);
    record_coroutine_trace_event(CoroutineTraceEvent::Type::Created, coroutine);
    coroutine->frames.front().global_resolution_env = module_or_root_environment(coroutine->frames.front().closure);
    if (coroutine->frames.front().bytecode != nullptr) {
        ZEPHYR_TRY_ASSIGN(captured_cells,
                          capture_upvalue_cells(environment,
                                                coroutine->frames.front().bytecode->upvalue_names,
                                                HandleContainerKind::CoroutineFrame,
                                                expr.span,
                                                module_name));
        coroutine->frames.front().captured_upvalues = std::move(captured_cells);
    }
    return Value::object(coroutine);
}

RuntimeResult<Value> Runtime::resume_coroutine_value(const Value& value, const Span& span, const std::string& module_name) {
    ScopedVectorItem<const Value*> coroutine_root(rooted_values_, &value);
    if (!value.is_object() || value.as_object()->kind != ObjectKind::Coroutine) {
        return make_loc_error<Value>(module_name, span, "resume expects a coroutine value.");
    }

    auto* coroutine = static_cast<CoroutineObject*>(value.as_object());
    if (coroutine->completed) {
        return Value::nil();
    }
    if (coroutine->frames.empty()) {
        return make_loc_error<Value>(module_name, span, "Coroutine frame stack is empty.");
    }
    ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(coroutine->frames.front().bytecode.get(), span, module_name, "Coroutine"));
    if (coroutine->suspended) {
        unregister_suspended_coroutine(coroutine);
        coroutine->suspended = false;
    }
    ++coroutine->resume_count;
    ensure_coroutine_trace_id(coroutine);
    record_coroutine_trace_event(CoroutineTraceEvent::Type::Resumed, coroutine);

    auto& root = coroutine->frames.front();

    if (!coroutine->started) {
        root.root_env = allocate<Environment>(root.closure);
        if (root.bytecode != nullptr) {
            install_upvalue_bindings(root.root_env, *root.bytecode, root.captured_upvalues);
        }
        root.current_env = root.root_env;
        root.global_resolution_env = module_or_root_environment(root.root_env);
        root.stack.clear();
        root.locals.assign(root.bytecode ? static_cast<std::size_t>(std::max(root.bytecode->local_count, 0)) : 0, Value::nil());
        root.uses_register_mode = root.bytecode != nullptr && root.bytecode->uses_register_mode;
        if (root.bytecode && root.uses_register_mode) {
            const int needed = std::max(root.bytecode->max_regs, root.bytecode->local_count);
            if (needed > 0 && needed <= CoroutineFrameState::kInlineRegs) {
                root.reg_count = static_cast<std::uint8_t>(needed);
                std::fill_n(root.inline_regs, needed, Value::nil());
            } else {
                root.regs.assign(static_cast<std::size_t>(std::max(needed, 0)), Value::nil());
            }
        }
        root.local_binding_owners.assign(root.locals.size(), nullptr);
        root.local_bindings.assign(root.locals.size(), nullptr);
        root.local_binding_versions.assign(root.locals.size(), 0);
        root.global_binding_owners.assign(root.bytecode ? root.bytecode->global_names.size() : 0, nullptr);
        root.global_bindings.assign(root.bytecode ? root.bytecode->global_names.size() : 0, nullptr);
        root.global_binding_versions.assign(root.bytecode ? root.bytecode->global_names.size() : 0, 0);
        root.scope_stack.clear();
        root.ip = 0;
        root.ip_index = 0;
        coroutine->started = true;
    }

    // Step 2 fast path: already-started single-frame register-mode coroutine —
    // skip fake_module construction (string copy) and resume_coroutine_bytecode overhead.
    if (coroutine->frames.size() == 1 && coroutine->frames[0].uses_register_mode) {
        ScopedVectorPush<CoroutineObject> fast_active(active_coroutines_, coroutine);
        auto fast_result = resume_register_coroutine_fast(coroutine, span);
        if (!fast_result) {
            const std::string error_with_trace = append_coroutine_stack_trace(std::move(fast_result.error()), coroutine);
            unregister_suspended_coroutine(coroutine);
            coroutine->completed = true;
            coroutine->suspended = false;
            record_coroutine_completed(coroutine);
            coroutine->last_resume_step_count = 0;
            auto& reset_root = coroutine->frames.front();
            reset_root.stack.clear();
            reset_root.locals.clear();
            reset_root.captured_upvalues.clear();
            reset_root.local_binding_owners.clear();
            reset_root.local_bindings.clear();
            reset_root.local_binding_versions.clear();
            reset_root.global_binding_owners.clear();
            reset_root.global_bindings.clear();
            reset_root.global_binding_versions.clear();
            reset_root.scope_stack.clear();
            reset_root.regs.clear();
            reset_root.reg_cards.clear();
            reset_root.reg_count = 0;
            reset_root.current_env = nullptr;
            reset_root.root_env = nullptr;
            reset_root.ip = 0;
            reset_root.ip_index = 0;
            return std::unexpected(error_with_trace);
        }
        coroutine->last_resume_step_count = fast_result->step_count;
        coroutine->total_step_count += fast_result->step_count;
        coroutine->max_resume_step_count = std::max(coroutine->max_resume_step_count, fast_result->step_count);
        if (fast_result->yielded) {
            ++coroutine->yield_count;
        }
        return fast_result->value;
    }

    ModuleRecord fake_module;
    fake_module.name = root.module_name.empty() ? module_name : root.module_name;
    fake_module.environment = root.root_env;

    ScopedVectorPush<CoroutineObject> active_scope(active_coroutines_, coroutine);
    auto step_result = resume_coroutine_bytecode(coroutine, fake_module, span);
    if (!step_result) {
        const std::string error_with_trace = append_coroutine_stack_trace(std::move(step_result.error()), coroutine);
        unregister_suspended_coroutine(coroutine);
        coroutine->completed = true;
        coroutine->suspended = false;
        record_coroutine_completed(coroutine);
        coroutine->last_resume_step_count = 0;
        if (coroutine->frames.size() > 1) {
            coroutine->frames.erase(coroutine->frames.begin() + 1, coroutine->frames.end());
        }
        auto& reset_root = coroutine->frames.front();
        reset_root.stack.clear();
        reset_root.locals.clear();
        reset_root.captured_upvalues.clear();
        reset_root.local_binding_owners.clear();
        reset_root.local_bindings.clear();
        reset_root.local_binding_versions.clear();
        reset_root.global_binding_owners.clear();
        reset_root.global_bindings.clear();
        reset_root.global_binding_versions.clear();
        reset_root.scope_stack.clear();
        reset_root.regs.clear();
        reset_root.reg_cards.clear();
        reset_root.reg_count = 0;
        reset_root.current_env = nullptr;
        reset_root.root_env = nullptr;
        reset_root.global_resolution_env = nullptr;
        reset_root.ip = 0;
        reset_root.ip_index = 0;
        return std::unexpected(error_with_trace);
    }
    coroutine->last_resume_step_count = step_result->step_count;
    coroutine->total_step_count += step_result->step_count;
    coroutine->max_resume_step_count = std::max(coroutine->max_resume_step_count, step_result->step_count);
    if (step_result->yielded) {
        ++coroutine->yield_count;
    }
    return step_result->value;
}

RuntimeResult<Value> Runtime::evaluate_resume(Environment* environment, ResumeExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(target, evaluate(environment, expr.target.get(), module_name));
    return resume_coroutine_value(target, expr.span, module_name);
}

RuntimeResult<Value> Runtime::evaluate_struct_init(Environment* environment, StructInitExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(type, expect_struct_type(environment, expr.type_name, module_name, expr.span));
    auto* instance = allocate<StructInstanceObject>(type);
    initialize_struct_instance(instance);

    for (auto& field : expr.fields) {
        const std::size_t field_index = instance->field_slot(field.name);
        if (field_index == static_cast<std::size_t>(-1) || field_index >= type->fields.size()) {
            return make_loc_error<Value>(module_name, field.span, "Unknown struct field '" + field.name + "'.");
        }
        ZEPHYR_TRY_ASSIGN(value, evaluate(environment, field.value.get(), module_name));
        const auto& raw_ft = type->fields[field_index].type_name;
        std::optional<std::string> eff_ft = (std::find(type->generic_params.begin(), type->generic_params.end(), raw_ft) != type->generic_params.end())
            ? std::optional<std::string>(std::nullopt) : std::optional<std::string>(raw_ft);
        ZEPHYR_TRY(enforce_type(value, eff_ft, field.span, module_name, "struct field"));
        ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::HeapField, field.span, module_name, "struct field"));
        instance->field_values[field_index] = value;
        note_struct_field_write(instance, field_index, value);
    }

    for (std::size_t field_index = 0; field_index < type->fields.size(); ++field_index) {
        const auto& field_spec = type->fields[field_index];
        if (instance->field_values[field_index].is_nil() && field_spec.type_name != "Nil" && field_spec.type_name != "any") {
            return make_loc_error<Value>(module_name, expr.span, "Missing required field '" + field_spec.name + "' in struct literal.");
        }
    }

    return Value::object(instance);
}

RuntimeResult<Value> Runtime::evaluate_enum_init(Environment* environment, EnumInitExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(type, expect_enum_type(environment, expr.enum_name, module_name, expr.span));
    const auto variant_it = std::find_if(type->variants.begin(), type->variants.end(),
                                         [&](const EnumVariantSpec& variant) { return variant.name == expr.variant_name; });
    if (variant_it == type->variants.end()) {
        return make_loc_error<Value>(module_name, expr.span, "Unknown enum variant '" + expr.variant_name + "'.");
    }
    if (variant_it->payload_types.size() != expr.arguments.size()) {
        return make_loc_error<Value>(module_name, expr.span, "Wrong payload count for enum variant '" + expr.variant_name + "'.");
    }
    auto* instance = allocate<EnumInstanceObject>(type, expr.variant_name);
    for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
        ZEPHYR_TRY_ASSIGN(value, evaluate(environment, expr.arguments[i].get(), module_name));
        ZEPHYR_TRY(enforce_type(value, variant_it->payload_types[i], expr.arguments[i]->span, module_name, "enum payload"));
        ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::HeapField, expr.arguments[i]->span, module_name, "enum payload"));
        instance->payload.push_back(value);
        note_enum_payload_write(instance, instance->payload.size() - 1, value);
    }
    return Value::object(instance);
}

RuntimeResult<bool> Runtime::bind_pattern(Environment* target_env, const Value& value, Pattern* pattern, const std::string& module_name) {
    auto collect_pattern_binding_names = [&](auto&& self, Pattern* candidate, std::vector<std::string>& names) -> void {
        if (candidate == nullptr) {
            return;
        }
        if (auto* binding = dynamic_cast<BindingPattern*>(candidate)) {
            if (std::find(names.begin(), names.end(), binding->name) == names.end()) {
                names.push_back(binding->name);
            }
            return;
        }
        if (auto* enum_pattern = dynamic_cast<EnumPattern*>(candidate)) {
            for (auto& payload : enum_pattern->payload) {
                self(self, payload.get(), names);
            }
            return;
        }
        if (auto* struct_pattern = dynamic_cast<StructPattern*>(candidate)) {
            for (auto& field : struct_pattern->fields) {
                self(self, field.pattern.get(), names);
            }
            return;
        }
        if (auto* array_pattern = dynamic_cast<ArrayPattern*>(candidate)) {
            for (auto& element : array_pattern->elements) {
                self(self, element.get(), names);
            }
            if (array_pattern->has_rest && !array_pattern->rest_name.empty() && array_pattern->rest_name != "_" &&
                std::find(names.begin(), names.end(), array_pattern->rest_name) == names.end()) {
                names.push_back(array_pattern->rest_name);
            }
            return;
        }
        if (auto* tuple_pattern = dynamic_cast<TuplePattern*>(candidate)) {
            for (auto& element : tuple_pattern->elements) {
                self(self, element.get(), names);
            }
            return;
        }
        if (auto* or_pattern = dynamic_cast<OrPattern*>(candidate)) {
            if (!or_pattern->alternatives.empty()) {
                self(self, or_pattern->alternatives.front().get(), names);
            }
        }
    };

    if (dynamic_cast<WildcardPattern*>(pattern) != nullptr) {
        return true;
    }
    if (auto* binding = dynamic_cast<BindingPattern*>(pattern)) {
        define_value(target_env, binding->name, value, false);
        return true;
    }
    if (auto* literal = dynamic_cast<LiteralPattern*>(pattern)) {
        LiteralExpr synthetic;
        synthetic.value = literal->value;
        ZEPHYR_TRY_ASSIGN(literal_value, evaluate_literal(synthetic));
        return values_equal(value, literal_value);
    }
    if (auto* enum_pattern = dynamic_cast<EnumPattern*>(pattern)) {
        if (!value.is_object() || value.as_object()->kind != ObjectKind::EnumInstance) {
            return false;
        }
        auto* enum_value = static_cast<EnumInstanceObject*>(value.as_object());
        if (!enum_pattern->enum_name.parts.empty()) {
            const std::string expected_enum = enum_pattern->enum_name.parts.back();
            if (enum_value->type->name != expected_enum) {
                return false;
            }
        }
        if (enum_value->variant != enum_pattern->variant_name || enum_value->payload.size() != enum_pattern->payload.size()) {
            return false;
        }
        for (std::size_t i = 0; i < enum_value->payload.size(); ++i) {
            ZEPHYR_TRY_ASSIGN(matched, bind_pattern(target_env, enum_value->payload[i], enum_pattern->payload[i].get(), module_name));
            if (!matched) {
                return false;
            }
        }
        return true;
    }
    if (auto* struct_pattern = dynamic_cast<StructPattern*>(pattern)) {
        if (!value.is_object() || value.as_object()->kind != ObjectKind::StructInstance) {
            return false;
        }
        auto* struct_value = static_cast<StructInstanceObject*>(value.as_object());
        if (struct_pattern->type_name.parts.empty()) {
            return false;
        }
        const std::string expected_struct = struct_pattern->type_name.parts.back();
        if (struct_value->type->name != expected_struct) {
            return false;
        }
        for (auto& field : struct_pattern->fields) {
            const std::size_t field_index = struct_value->field_slot(field.name);
            if (field_index == static_cast<std::size_t>(-1) || field_index >= struct_value->field_values.size()) {
                return false;
            }
            ZEPHYR_TRY_ASSIGN(field_matched,
                              bind_pattern(target_env, struct_value->field_values[field_index], field.pattern.get(), module_name));
            if (!field_matched) {
                return false;
            }
        }
        return true;
    }
    if (auto* range_pattern = dynamic_cast<RangePattern*>(pattern)) {
        const auto bound_to_double = [](const std::variant<std::int64_t, double>& bound) -> double {
            if (const auto* int_value = std::get_if<std::int64_t>(&bound)) {
                return static_cast<double>(*int_value);
            }
            return std::get<double>(bound);
        };

        double candidate = 0.0;
        if (value.is_int()) {
            candidate = static_cast<double>(value.as_int());
        } else if (value.is_float()) {
            candidate = value.as_float();
        } else {
            return false;
        }

        const double start = bound_to_double(range_pattern->start);
        const double end = bound_to_double(range_pattern->end);
        if (candidate < start) {
            return false;
        }
        if (range_pattern->inclusive_end) {
            return candidate <= end;
        }
        return candidate < end;
    }
    if (auto* array_pattern = dynamic_cast<ArrayPattern*>(pattern)) {
        if (!value.is_object() || value.as_object()->kind != ObjectKind::Array) {
            return false;
        }
        auto* array_value = static_cast<ArrayObject*>(value.as_object());
        if ((!array_pattern->has_rest && array_value->elements.size() != array_pattern->elements.size()) ||
            (array_pattern->has_rest && array_value->elements.size() < array_pattern->elements.size())) {
            return false;
        }
        for (std::size_t i = 0; i < array_pattern->elements.size(); ++i) {
            ZEPHYR_TRY_ASSIGN(element_matched, bind_pattern(target_env, array_value->elements[i], array_pattern->elements[i].get(), module_name));
            if (!element_matched) {
                return false;
            }
        }
        if (array_pattern->has_rest && !array_pattern->rest_name.empty() && array_pattern->rest_name != "_") {
            auto* rest = allocate<ArrayObject>();
            for (std::size_t i = array_pattern->elements.size(); i < array_value->elements.size(); ++i) {
                const std::size_t index = rest->elements.size();
                rest->elements.push_back(array_value->elements[i]);
                note_array_element_write(rest, index, array_value->elements[i]);
            }
            define_value(target_env, array_pattern->rest_name, Value::object(rest), false);
        }
        return true;
    }
    if (auto* tuple_pattern = dynamic_cast<TuplePattern*>(pattern)) {
        if (!value.is_object() || value.as_object()->kind != ObjectKind::Array) {
            return false;
        }
        auto* tuple_value = static_cast<ArrayObject*>(value.as_object());
        if (tuple_value->elements.size() != tuple_pattern->elements.size()) {
            return false;
        }
        for (std::size_t i = 0; i < tuple_pattern->elements.size(); ++i) {
            ZEPHYR_TRY_ASSIGN(element_matched, bind_pattern(target_env, tuple_value->elements[i], tuple_pattern->elements[i].get(), module_name));
            if (!element_matched) {
                return false;
            }
        }
        return true;
    }
    if (auto* or_pattern = dynamic_cast<OrPattern*>(pattern)) {
        std::vector<std::string> binding_names;
        collect_pattern_binding_names(collect_pattern_binding_names, pattern, binding_names);
        for (auto& alternative : or_pattern->alternatives) {
            auto* scratch_env = allocate<Environment>(target_env->parent);
            ZEPHYR_TRY_ASSIGN(matched, bind_pattern(scratch_env, value, alternative.get(), module_name));
            if (!matched) {
                continue;
            }
            for (const auto& binding_name : binding_names) {
                const auto it = scratch_env->values.find(binding_name);
                if (it == scratch_env->values.end()) {
                    return make_loc_error<bool>(module_name, pattern->span, "OR-pattern alternatives must bind the same names.");
                }
                define_value(target_env, binding_name, it->second.value, it->second.mutable_value, it->second.type_name);
            }
            return true;
        }
        return false;
    }
    return make_loc_error<bool>(module_name, pattern->span, "Unsupported match pattern.");
}

VoidResult Runtime::register_trait_decl(Environment* environment, TraitDecl* trait_decl, ModuleRecord& module) {
    if (trait_decl == nullptr) {
        return ok_result();
    }
    if (traits_.find(trait_decl->name) != traits_.end()) {
        return make_loc_error<std::monostate>(module.name, trait_decl->span, "Trait '" + trait_decl->name + "' is already defined.");
    }

    TraitDefinition definition;
    definition.name = trait_decl->name;
    for (const auto& method : trait_decl->methods) {
        if (definition.methods.find(method.name) != definition.methods.end()) {
            return make_loc_error<std::monostate>(module.name,
                                                  method.span,
                                                  "Trait '" + trait_decl->name + "' declares method '" + method.name + "' more than once.");
        }
        definition.methods.emplace(method.name, method);
    }

    traits_.emplace(trait_decl->name, std::move(definition));
    define_value(environment, trait_decl->name, Value::nil(), false, std::string("Trait"));
    if (trait_decl->exported &&
        std::find(module.namespace_object->exports.begin(), module.namespace_object->exports.end(), trait_decl->name) == module.namespace_object->exports.end()) {
        module.namespace_object->exports.push_back(trait_decl->name);
    }
    return ok_result();
}

VoidResult Runtime::register_impl_decl(Environment* environment, ImplDecl* impl_decl, ModuleRecord& module) {
    if (impl_decl == nullptr) {
        return ok_result();
    }

    // Freestanding impl: impl TypeName { ... } — for_type.parts is empty
    if (impl_decl->for_type.parts.empty()) {
        const std::string struct_name = impl_decl->trait_name.display_name();
        ZEPHYR_TRY_ASSIGN(type_value, lookup_value(environment, struct_name, impl_decl->span, module.name));
        if (!type_value.is_object() || type_value.as_object()->kind != ObjectKind::StructType) {
            return make_loc_error<std::monostate>(module.name, impl_decl->span,
                                                  "Freestanding impl target '" + struct_name + "' must be a struct type.");
        }
        auto* struct_type = static_cast<StructTypeObject*>(type_value.as_object());
        for (const auto& method : impl_decl->methods) {
            if (method == nullptr) {
                continue;
            }
            const bool has_self = !method->params.empty() && method->params.front().name == "self";
            const std::string hidden_name = "__impl_" + struct_name + "_" + method->name;
            ZEPHYR_TRY_ASSIGN(function_object,
                              create_script_function(hidden_name,
                                                     module.name,
                                                     method->params,
                                                     method->return_type,
                                                     method->body.get(),
                                                     environment,
                                                     compile_bytecode_function(hidden_name, method->params, method->body.get()),
                                                     method->span,
                                                     method->generic_params));
            const Value fn_value = Value::object(function_object);
            define_value(environment, hidden_name, fn_value, false, std::string("Function"));
            if (has_self) {
                struct_type->instance_methods[method->name] = fn_value;
            } else {
                struct_type->static_methods[method->name] = fn_value;
            }
        }
        return ok_result();
    }

    const std::string trait_name = impl_decl->trait_name.display_name();
    const std::string type_name = impl_decl->for_type.display_name();
    const auto trait_it = traits_.find(trait_name);
    if (trait_it == traits_.end()) {
        return make_loc_error<std::monostate>(module.name, impl_decl->span, "Unknown trait '" + trait_name + "'.");
    }

    ZEPHYR_TRY_ASSIGN(type_value, lookup_value(environment, type_name, impl_decl->span, module.name));
    if (!type_value.is_object() ||
        (type_value.as_object()->kind != ObjectKind::StructType && type_value.as_object()->kind != ObjectKind::EnumType)) {
        return make_loc_error<std::monostate>(module.name, impl_decl->span, "Type '" + type_name + "' does not support trait impls.");
    }

    auto& by_trait = trait_impls_[type_name];
    if (by_trait.find(trait_name) != by_trait.end()) {
        return make_loc_error<std::monostate>(module.name, impl_decl->span,
                                              "Trait '" + trait_name + "' is already implemented for '" + type_name + "'.");
    }

    ImplDefinition implementation;
    implementation.trait_name = trait_name;
    implementation.type_name = type_name;
    auto render_optional_type = [](const std::optional<TypeRef>& type) {
        return type.has_value() ? type->display_name() : std::string("any");
    };
    auto render_signature = [&](const std::string& method_name, const std::vector<Param>& params, const std::optional<TypeRef>& return_type) {
        std::ostringstream out;
        out << "fn " << method_name << "(";
        for (std::size_t index = 0; index < params.size(); ++index) {
            if (index > 0) {
                out << ", ";
            }
            out << params[index].name << ": " << render_optional_type(params[index].type);
        }
        out << ") -> " << render_optional_type(return_type);
        return out.str();
    };
    auto same_type = [](const std::optional<TypeRef>& left, const std::optional<TypeRef>& right) {
        if (left.has_value() != right.has_value()) {
            return false;
        }
        return !left.has_value() || left->display_name() == right->display_name();
    };

    auto sanitize_name = [](std::string value) {
        for (char& ch : value) {
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                ch = '_';
            }
        }
        return value;
    };

    for (const auto& method : impl_decl->methods) {
        if (method == nullptr) {
            continue;
        }
        if (implementation.methods.find(method->name) != implementation.methods.end()) {
            return make_loc_error<std::monostate>(module.name,
                                                  method->span,
                                                  "Impl for '" + type_name + "' defines method '" + method->name + "' more than once.");
        }
        const auto signature_it = trait_it->second.methods.find(method->name);
        if (signature_it == trait_it->second.methods.end()) {
            emit_warning(module.name, method->span, "method '" + method->name + "' is not part of trait '" + trait_name + "'");
            continue;
        }
        if (method->params.empty()) {
            return make_loc_error<std::monostate>(module.name,
                                                  method->span,
                                                  "Trait impl method '" + method->name + "' must declare self as its first parameter.");
        }
        if (method->params.size() != signature_it->second.params.size()) {
            return make_loc_error<std::monostate>(
                module.name,
                method->span,
                "Trait method '" + method->name + "' in impl of '" + trait_name + "' for '" + type_name +
                    "' does not match the trait signature. expected " +
                    render_signature(signature_it->second.name, signature_it->second.params, signature_it->second.return_type) +
                    ", got " + render_signature(method->name, method->params, method->return_type));
        }
        for (std::size_t param_index = 0; param_index < method->params.size(); ++param_index) {
            if (!same_type(method->params[param_index].type, signature_it->second.params[param_index].type)) {
                return make_loc_error<std::monostate>(
                    module.name,
                    method->params[param_index].span,
                    "Trait method '" + method->name + "' in impl of '" + trait_name + "' for '" + type_name +
                        "' does not match the trait signature. expected " +
                        render_signature(signature_it->second.name, signature_it->second.params, signature_it->second.return_type) +
                        ", got " + render_signature(method->name, method->params, method->return_type));
            }
        }
        if (!same_type(method->return_type, signature_it->second.return_type)) {
            return make_loc_error<std::monostate>(
                module.name,
                method->span,
                "Trait method '" + method->name + "' in impl of '" + trait_name + "' for '" + type_name +
                    "' does not match the trait signature. expected " +
                    render_signature(signature_it->second.name, signature_it->second.params, signature_it->second.return_type) +
                    ", got " + render_signature(method->name, method->params, method->return_type));
        }

        const std::string hidden_name =
            "__trait_" + sanitize_name(trait_name) + "_" + sanitize_name(type_name) + "_" + sanitize_name(method->name);
        ZEPHYR_TRY_ASSIGN(function_object,
                          create_script_function(hidden_name,
                                                 module.name,
                                                 method->params,
                                                 method->return_type,
                                                 method->body.get(),
                                                 environment,
                                                 compile_bytecode_function(hidden_name, method->params, method->body.get()),
                                                 method->span,
                                                 method->generic_params));
        define_value(environment, hidden_name, Value::object(function_object), false, std::string("Function"));
        implementation.methods.emplace(method->name, ImplMethodEntry{module.name, hidden_name});
    }

    for (const auto& [method_name, _] : trait_it->second.methods) {
        if (implementation.methods.find(method_name) == implementation.methods.end()) {
            return make_loc_error<std::monostate>(module.name,
                                                  impl_decl->span,
                                                  "error: impl of '" + trait_name + "' for '" + type_name + "' is missing method '" +
                                                      method_name + "'");
        }
    }

    by_trait.emplace(trait_name, std::move(implementation));
    return ok_result();
}

RuntimeResult<std::optional<Value>> Runtime::resolve_trait_method(const Value& receiver,
                                                                  const std::string& member,
                                                                  const Span& span,
                                                                  const std::string& module_name) {
    if (!receiver.is_object()) {
        return std::optional<Value>{};
    }

    std::string type_name;
    switch (receiver.as_object()->kind) {
        case ObjectKind::StructInstance:
            type_name = static_cast<StructInstanceObject*>(receiver.as_object())->type->name;
            break;
        case ObjectKind::EnumInstance:
            type_name = static_cast<EnumInstanceObject*>(receiver.as_object())->type->name;
            break;
        default:
            return std::optional<Value>{};
    }

    const auto impls_it = trait_impls_.find(type_name);
    std::optional<Value> resolved;
    if (impls_it != trait_impls_.end()) {
        for (const auto& [trait_name, implementation] : impls_it->second) {
            const auto method_it = implementation.methods.find(member);
            if (method_it == implementation.methods.end()) {
                continue;
            }
            if (resolved.has_value()) {
                return make_loc_error<std::optional<Value>>(module_name,
                                                            span,
                                                            "Trait method call '" + member + "' is ambiguous for type '" + type_name + "'.");
            }
            const auto module_it = modules_.find(method_it->second.module_name);
            if (module_it == modules_.end()) {
                return make_loc_error<std::optional<Value>>(module_name,
                                                            span,
                                                            "Trait method module '" + method_it->second.module_name + "' is not loaded.");
            }
            Binding* binding = lookup_binding(module_it->second.environment, method_it->second.binding_name);
            if (binding == nullptr) {
                return make_loc_error<std::optional<Value>>(module_name,
                                                            span,
                                                            "Missing trait method binding '" + method_it->second.binding_name + "'.");
            }
            resolved = read_binding_value(*binding);
        }
    }
    if (resolved.has_value()) {
        return resolved;
    }

    std::vector<std::string> candidate_traits;
    for (const auto& [trait_name, definition] : traits_) {
        if (definition.methods.contains(member)) {
            candidate_traits.push_back(trait_name);
        }
    }
    if (candidate_traits.empty()) {
        return std::optional<Value>{};
    }

    std::sort(candidate_traits.begin(), candidate_traits.end());
    const std::string trait_name = candidate_traits.front();
    return make_loc_error<std::optional<Value>>(
        module_name,
        span,
        "RuntimeError: '" + type_name + "' does not implement method '" + member + "'\n"
        "hint: add 'impl " + trait_name + " for " + type_name + " { fn " + member + "(...) { ... } }'");
}

RuntimeResult<Value> Runtime::evaluate_match(Environment* environment, MatchExpr& expr, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(subject, evaluate(environment, expr.subject.get(), module_name));
    for (auto& arm : expr.arms) {
        auto* arm_env = allocate<Environment>(environment);
        ZEPHYR_TRY_ASSIGN(matched, bind_pattern(arm_env, subject, arm.pattern.get(), module_name));
        if (matched) {
            if (arm.guard_expr) {
                ZEPHYR_TRY_ASSIGN(guard_value, evaluate(arm_env, arm.guard_expr.get(), module_name));
                if (!is_truthy(guard_value)) {
                    continue;
                }
            }
            return evaluate(arm_env, arm.expression.get(), module_name);
        }
    }
    return make_loc_error<Value>(module_name,
                                 expr.span,
                                 "Match expression is not exhaustive. hint: match may not cover all cases: missing " +
                                     describe_match_missing_case(subject));
}

RuntimeResult<Value> Runtime::evaluate(Environment* environment, Expr* expr, const std::string& module_name) {
    if (auto* literal = dynamic_cast<LiteralExpr*>(expr)) {
        return evaluate_literal(*literal);
    }
    if (auto* interpolated = dynamic_cast<InterpolatedStringExpr*>(expr)) {
        return evaluate_interpolated_string(environment, *interpolated, module_name);
    }
    if (auto* variable = dynamic_cast<VariableExpr*>(expr)) {
        return lookup_value(environment, variable->name, variable->span, module_name);
    }
    if (auto* array = dynamic_cast<ArrayExpr*>(expr)) {
        return evaluate_array(environment, *array, module_name);
    }
    if (auto* group = dynamic_cast<GroupExpr*>(expr)) {
        return evaluate(environment, group->inner.get(), module_name);
    }
    if (auto* unary = dynamic_cast<UnaryExpr*>(expr)) {
        return evaluate_unary(environment, *unary, module_name);
    }
    if (auto* binary = dynamic_cast<BinaryExpr*>(expr)) {
        return evaluate_binary(environment, *binary, module_name);
    }
    if (auto* assign = dynamic_cast<AssignExpr*>(expr)) {
        return evaluate_assign(environment, *assign, module_name);
    }
    if (auto* member = dynamic_cast<MemberExpr*>(expr)) {
        return evaluate_member(environment, *member, module_name);
    }
    if (auto* optional_member = dynamic_cast<OptionalMemberExpr*>(expr)) {
        return evaluate_optional_member(environment, *optional_member, module_name);
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr)) {
        return evaluate_index(environment, *index, module_name);
    }
    if (auto* call = dynamic_cast<CallExpr*>(expr)) {
        return evaluate_call(environment, *call, module_name);
    }
    if (auto* optional_call = dynamic_cast<OptionalCallExpr*>(expr)) {
        return evaluate_optional_call(environment, *optional_call, module_name);
    }
    if (auto* function = dynamic_cast<FunctionExpr*>(expr)) {
        return evaluate_function(environment, *function);
    }
    if (auto* coroutine = dynamic_cast<CoroutineExpr*>(expr)) {
        return evaluate_coroutine(environment, *coroutine, module_name);
    }
    if (auto* resume = dynamic_cast<ResumeExpr*>(expr)) {
        return evaluate_resume(environment, *resume, module_name);
    }
    if (auto* struct_init = dynamic_cast<StructInitExpr*>(expr)) {
        return evaluate_struct_init(environment, *struct_init, module_name);
    }
    if (auto* enum_init = dynamic_cast<EnumInitExpr*>(expr)) {
        return evaluate_enum_init(environment, *enum_init, module_name);
    }
    if (auto* match_expr = dynamic_cast<MatchExpr*>(expr)) {
        return evaluate_match(environment, *match_expr, module_name);
    }
    return make_loc_error<Value>(module_name, expr->span, "Unsupported expression node.");
}

Runtime::FlowResult Runtime::execute_block(Environment* environment, BlockStmt* block, ModuleRecord& module) {
    auto* block_env = allocate<Environment>(environment);
    ScopedVectorPush<Environment> scope(active_environments_, block_env);
    for (auto& statement : block->statements) {
        ZEPHYR_TRY_ASSIGN(flow, execute(block_env, statement.get(), module));
        if (flow.kind != FlowSignal::Kind::None) {
            return flow;
        }
    }
    return FlowSignal{};
}

Runtime::FlowResult Runtime::execute(Environment* environment, Stmt* stmt, ModuleRecord& module) {
    if (auto* import_stmt = dynamic_cast<ImportStmt*>(stmt)) {
        ZEPHYR_TRY_ASSIGN(imported, import_module(module.path.empty() ? std::filesystem::current_path() : module.path.parent_path(), import_stmt->path));
        std::vector<std::string> named_pairs;
        for (const auto& item : import_stmt->named) {
            named_pairs.push_back(item.name);
            named_pairs.push_back(item.local_name);
        }
        ZEPHYR_TRY(import_exports(environment, *imported, import_stmt->alias, named_pairs, module.name, import_stmt->span));
        return FlowSignal{};
    }
    if (auto* re_export = dynamic_cast<ReExportStmt*>(stmt)) {
        const auto base = module.path.empty() ? std::filesystem::current_path() : module.path.parent_path();
        ModuleRecord* source = nullptr;
        if (!re_export->path.empty()) {
            ZEPHYR_TRY_ASSIGN(src, import_module(base, re_export->path));
            source = src;
        }
        for (const auto& item : re_export->items) {
            Value val;
            if (source != nullptr) {
                Binding* binding = lookup_binding(source->environment, item.name);
                if (binding == nullptr) {
                    return make_loc_error<FlowSignal>(module.name, re_export->span, "Module does not export '" + item.name + "'.");
                }
                val = read_binding_value(*binding);
            } else {
                Binding* binding = lookup_binding(environment, item.name);
                if (binding == nullptr) {
                    return make_loc_error<FlowSignal>(module.name, re_export->span, "'" + item.name + "' is not defined.");
                }
                val = read_binding_value(*binding);
            }
            define_value(environment, item.exported_as, val, false);
            if (module.namespace_object != nullptr &&
                std::find(module.namespace_object->exports.begin(), module.namespace_object->exports.end(), item.exported_as) ==
                    module.namespace_object->exports.end()) {
                module.namespace_object->exports.push_back(item.exported_as);
            }
        }
        return FlowSignal{};
    }
    if (auto* let_stmt = dynamic_cast<LetStmt*>(stmt)) {
        ZEPHYR_TRY_ASSIGN(value, evaluate(environment, let_stmt->initializer.get(), module.name));
        if (let_stmt->pattern != nullptr) {
            auto* arm_env = allocate<Environment>(environment);
            ScopedVectorPush<Environment> scope(active_environments_, arm_env);
            ZEPHYR_TRY_ASSIGN(matched, bind_pattern(arm_env, value, let_stmt->pattern.get(), module.name));
            if (matched) {
                for (const auto& [binding_name, binding] : arm_env->values) {
                    define_value(environment, binding_name, binding.value, binding.mutable_value, binding.type_name);
                }
                return FlowSignal{};
            }
            if (let_stmt->else_branch) {
                return execute_block(environment, let_stmt->else_branch.get(), module);
            }
            return make_loc_error<FlowSignal>(module.name, let_stmt->span, "let pattern did not match.");
        }
        const std::optional<std::string> type_name =
            let_stmt->type.has_value() ? std::optional<std::string>(let_stmt->type->display_name()) : std::nullopt;
        ZEPHYR_TRY(enforce_type(value, type_name, let_stmt->span, module.name, "let binding"));
        define_value(environment, let_stmt->name, value, let_stmt->mutable_value, type_name);
        if (stmt->exported) {
            module.namespace_object->exports.push_back(let_stmt->name);
        }
        return FlowSignal{};
    }
    if (auto* block = dynamic_cast<BlockStmt*>(stmt)) {
        return execute_block(environment, block, module);
    }
    if (auto* if_stmt = dynamic_cast<IfStmt*>(stmt)) {
        if (if_stmt->let_pattern != nullptr) {
            ZEPHYR_TRY_ASSIGN(subject, evaluate(environment, if_stmt->let_subject.get(), module.name));
            auto* arm_env = allocate<Environment>(environment);
            ScopedVectorPush<Environment> scope(active_environments_, arm_env);
            ZEPHYR_TRY_ASSIGN(matched, bind_pattern(arm_env, subject, if_stmt->let_pattern.get(), module.name));
            if (matched) {
                return execute_block(arm_env, if_stmt->then_branch.get(), module);
            }
            if (if_stmt->else_branch) {
                return execute(environment, if_stmt->else_branch.get(), module);
            }
            return FlowSignal{};
        }

        ZEPHYR_TRY_ASSIGN(condition, evaluate(environment, if_stmt->condition.get(), module.name));
        if (is_truthy(condition)) {
            return execute_block(environment, if_stmt->then_branch.get(), module);
        } else if (if_stmt->else_branch) {
            return execute(environment, if_stmt->else_branch.get(), module);
        }
        return FlowSignal{};
    }
    if (auto* while_stmt = dynamic_cast<WhileStmt*>(stmt)) {
        while (true) {
            FlowSignal flow;
            if (while_stmt->let_pattern != nullptr) {
                ZEPHYR_TRY_ASSIGN(subject, evaluate(environment, while_stmt->let_subject.get(), module.name));
                auto* arm_env = allocate<Environment>(environment);
                ScopedVectorPush<Environment> scope(active_environments_, arm_env);
                ZEPHYR_TRY_ASSIGN(matched, bind_pattern(arm_env, subject, while_stmt->let_pattern.get(), module.name));
                if (!matched) {
                    break;
                }
                ZEPHYR_TRY_ASSIGN(flow_result, execute_block(arm_env, while_stmt->body.get(), module));
                flow = flow_result;
            } else {
                ZEPHYR_TRY_ASSIGN(condition, evaluate(environment, while_stmt->condition.get(), module.name));
                if (!is_truthy(condition)) {
                    break;
                }
                ZEPHYR_TRY_ASSIGN(flow_result, execute_block(environment, while_stmt->body.get(), module));
                flow = flow_result;
            }

            if (flow.kind == FlowSignal::Kind::Return) {
                return flow;
            }
            if (flow.kind == FlowSignal::Kind::Break) {
                if (!flow.label.empty() && flow.label != while_stmt->label) {
                    return flow;
                }
                break;
            }
            if (flow.kind == FlowSignal::Kind::Continue) {
                if (!flow.label.empty() && flow.label != while_stmt->label) {
                    return flow;
                }
                continue;
            }
        }
        return FlowSignal{};
    }
    if (auto* for_stmt = dynamic_cast<ForStmt*>(stmt)) {
        ZEPHYR_TRY_ASSIGN(iterable, evaluate(environment, for_stmt->iterable.get(), module.name));
        if (!iterable.is_object() || iterable.as_object()->kind != ObjectKind::Array) {
            return make_loc_error<FlowSignal>(module.name, for_stmt->span, "for-in expects Array.");
        }
        auto* array = static_cast<ArrayObject*>(iterable.as_object());
        for (const Value& element : array->elements) {
            auto* loop_env = allocate<Environment>(environment);
            define_value(loop_env, for_stmt->name, element, true);
            ScopedVectorPush<Environment> scope(active_environments_, loop_env);
            for (auto& statement : for_stmt->body->statements) {
                ZEPHYR_TRY_ASSIGN(flow, execute(loop_env, statement.get(), module));
                if (flow.kind == FlowSignal::Kind::Return) {
                    return flow;
                }
                if (flow.kind == FlowSignal::Kind::Break) {
                    if (!flow.label.empty() && flow.label != for_stmt->label) {
                        return flow;
                    }
                    return FlowSignal{};
                }
                if (flow.kind == FlowSignal::Kind::Continue) {
                    if (!flow.label.empty() && flow.label != for_stmt->label) {
                        return flow;
                    }
                    break;
                }
            }
        }
        return FlowSignal{};
    }
    if (auto* break_stmt = dynamic_cast<BreakStmt*>(stmt)) {
        FlowSignal flow;
        flow.kind = FlowSignal::Kind::Break;
        flow.label = break_stmt->label;
        return flow;
    }
    if (auto* continue_stmt = dynamic_cast<ContinueStmt*>(stmt)) {
        FlowSignal flow;
        flow.kind = FlowSignal::Kind::Continue;
        flow.label = continue_stmt->label;
        return flow;
    }
    if (auto* return_stmt = dynamic_cast<ReturnStmt*>(stmt)) {
        Value value = Value::nil();
        if (return_stmt->value) {
            ZEPHYR_TRY_ASSIGN(return_value, evaluate(environment, return_stmt->value.get(), module.name));
            value = return_value;
        }
        FlowSignal flow;
        flow.kind = FlowSignal::Kind::Return;
        flow.value = value;
        return flow;
    }
    if (auto* yield_stmt = dynamic_cast<YieldStmt*>(stmt)) {
        if (active_coroutines_.empty()) {
            return make_loc_error<FlowSignal>(module.name, yield_stmt->span, "yield used outside coroutine.");
        }
        return make_loc_error<FlowSignal>(module.name, yield_stmt->span, "yield requires coroutine bytecode execution context.");
    }
    if (auto* function_decl = dynamic_cast<FunctionDecl*>(stmt)) {
        ZEPHYR_TRY_ASSIGN(function_object,
                          create_script_function(function_decl->name,
                                                 module.name,
                                                 function_decl->params,
                                                 function_decl->return_type,
                                                 function_decl->body.get(),
                                                 environment,
                                                 compile_bytecode_function(function_decl->name, function_decl->params, function_decl->body.get(), function_decl->generic_params),
                                                 function_decl->span,
                                                 function_decl->generic_params));
        Value function = Value::object(function_object);
        define_value(environment, function_decl->name, function, false, std::string("Function"));
        if (stmt->exported) {
            module.namespace_object->exports.push_back(function_decl->name);
        }
        return FlowSignal{};
    }
    if (auto* struct_decl = dynamic_cast<StructDecl*>(stmt)) {
        auto* type = allocate<StructTypeObject>(struct_decl->name);
        type->generic_params = struct_decl->generic_params;
        for (const auto& field : struct_decl->fields) {
            type->fields.push_back(StructFieldSpec{field.name, field.type.display_name()});
        }
        define_value(environment, struct_decl->name, Value::object(type), false);
        if (stmt->exported) {
            module.namespace_object->exports.push_back(struct_decl->name);
        }
        return FlowSignal{};
    }
    if (auto* enum_decl = dynamic_cast<EnumDecl*>(stmt)) {
        auto* type = allocate<EnumTypeObject>(enum_decl->name);
        for (const auto& variant : enum_decl->variants) {
            EnumVariantSpec spec;
            spec.name = variant.name;
            for (const auto& payload : variant.payload_types) {
                spec.payload_types.push_back(payload.display_name());
            }
            type->variants.push_back(std::move(spec));
        }
        define_value(environment, enum_decl->name, Value::object(type), false);
        if (stmt->exported) {
            module.namespace_object->exports.push_back(enum_decl->name);
        }
        return FlowSignal{};
    }
    if (auto* trait_decl = dynamic_cast<TraitDecl*>(stmt)) {
        ZEPHYR_TRY(register_trait_decl(environment, trait_decl, module));
        return FlowSignal{};
    }
    if (auto* impl_decl = dynamic_cast<ImplDecl*>(stmt)) {
        ZEPHYR_TRY(register_impl_decl(environment, impl_decl, module));
        return FlowSignal{};
    }
    if (auto* expression_stmt = dynamic_cast<ExprStmt*>(stmt)) {
        ZEPHYR_TRY(evaluate(environment, expression_stmt->expression.get(), module.name));
        return FlowSignal{};
    }
    return make_loc_error<FlowSignal>(module.name, stmt->span, "Unsupported statement node.");
}

void Runtime::register_global_function(const std::string& name, ZephyrNativeFunction function, std::vector<std::string> param_types,
                                       std::string return_type) {
    auto* native = allocate<NativeFunctionObject>(
        name, std::move(function), std::move(param_types),
        return_type.empty() ? std::optional<std::string>{} : std::optional<std::string>(return_type));
    native_callback_registry_.push_back(native);
    define_value(root_environment_,
                 name,
                 Value::object(native),
                 false,
                 std::string("Function"));
}

// ─── std/json helpers ────────────────────────────────────────────────────────

static void json_skip_whitespace(const std::string& src, std::size_t& pos) {
    while (pos < src.size() &&
           (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r'))
        ++pos;
}

static ZephyrValue json_parse_value(const std::string& src, std::size_t& pos);

static ZephyrValue json_parse_value(const std::string& src, std::size_t& pos) {
    json_skip_whitespace(src, pos);
    if (pos >= src.size()) throw std::runtime_error("Unexpected end of JSON");
    char c = src[pos];
    if (c == 'n') {
        if (src.substr(pos, 4) == "null") { pos += 4; return ZephyrValue(); }
        throw std::runtime_error("Invalid JSON token at pos " + std::to_string(pos));
    }
    if (c == 't') {
        if (src.substr(pos, 4) == "true") { pos += 4; return ZephyrValue(true); }
        throw std::runtime_error("Invalid JSON token at pos " + std::to_string(pos));
    }
    if (c == 'f') {
        if (src.substr(pos, 5) == "false") { pos += 5; return ZephyrValue(false); }
        throw std::runtime_error("Invalid JSON token at pos " + std::to_string(pos));
    }
    if (c == '"') {
        ++pos;
        std::string result;
        while (pos < src.size() && src[pos] != '"') {
            if (src[pos] == '\\') {
                ++pos;
                if (pos >= src.size()) throw std::runtime_error("Unterminated escape in JSON string");
                switch (src[pos]) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += src[pos]; break;
                }
            } else {
                result += src[pos];
            }
            ++pos;
        }
        if (pos >= src.size()) throw std::runtime_error("Unterminated string in JSON");
        ++pos;
        return ZephyrValue(result);
    }
    if (c == '[') {
        ++pos;
        ZephyrValue::Array arr;
        json_skip_whitespace(src, pos);
        if (pos < src.size() && src[pos] == ']') { ++pos; return ZephyrValue(arr); }
        while (true) {
            arr.push_back(json_parse_value(src, pos));
            json_skip_whitespace(src, pos);
            if (pos >= src.size()) throw std::runtime_error("Unterminated array in JSON");
            if (src[pos] == ']') { ++pos; break; }
            if (src[pos] != ',') throw std::runtime_error("Expected ',' in JSON array");
            ++pos;
        }
        return ZephyrValue(arr);
    }
    if (c == '{') {
        ++pos;
        ZephyrRecord rec;
        rec.type_name = "object";
        json_skip_whitespace(src, pos);
        if (pos < src.size() && src[pos] == '}') { ++pos; return ZephyrValue(rec); }
        while (true) {
            json_skip_whitespace(src, pos);
            if (pos >= src.size() || src[pos] != '"')
                throw std::runtime_error("Expected string key in JSON object");
            ZephyrValue key_val = json_parse_value(src, pos);
            const std::string key = key_val.as_string();
            json_skip_whitespace(src, pos);
            if (pos >= src.size() || src[pos] != ':')
                throw std::runtime_error("Expected ':' in JSON object");
            ++pos;
            ZephyrValue val = json_parse_value(src, pos);
            rec.fields[key] = std::move(val);
            json_skip_whitespace(src, pos);
            if (pos >= src.size()) throw std::runtime_error("Unterminated object in JSON");
            if (src[pos] == '}') { ++pos; break; }
            if (src[pos] != ',') throw std::runtime_error("Expected ',' in JSON object");
            ++pos;
        }
        return ZephyrValue(rec);
    }
    // number
    std::size_t start = pos;
    bool is_float = false;
    if (c == '-') ++pos;
    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    if (pos < src.size() && src[pos] == '.') {
        is_float = true; ++pos;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    }
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
        is_float = true; ++pos;
        if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) ++pos;
    }
    std::string num_str = src.substr(start, pos - start);
    if (num_str.empty()) throw std::runtime_error("Invalid JSON at pos " + std::to_string(pos));
    if (is_float) return ZephyrValue(std::stod(num_str));
    return ZephyrValue(static_cast<std::int64_t>(std::stoll(num_str)));
}

static std::string json_stringify_value(const ZephyrValue& val) {
    if (val.is_nil())   return "null";
    if (val.is_bool())  return val.as_bool() ? "true" : "false";
    if (val.is_int())   return std::to_string(val.as_int());
    if (val.is_float()) {
        std::ostringstream oss;
        oss << val.as_float();
        return oss.str();
    }
    if (val.is_string()) {
        std::string s = "\"";
        for (char ch : val.as_string()) {
            if      (ch == '"')  s += "\\\"";
            else if (ch == '\\') s += "\\\\";
            else if (ch == '\n') s += "\\n";
            else if (ch == '\r') s += "\\r";
            else if (ch == '\t') s += "\\t";
            else                 s += ch;
        }
        return s + "\"";
    }
    if (val.is_array()) {
        std::string s = "[";
        const auto& arr = val.as_array();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) s += ",";
            s += json_stringify_value(arr[i]);
        }
        return s + "]";
    }
    if (val.is_record()) {
        const auto& rec = val.as_record();
        std::string s = "{";
        bool first = true;
        for (const auto& kv : rec.fields) {
            if (!first) s += ",";
            first = false;
            s += "\"" + kv.first + "\":" + json_stringify_value(kv.second);
        }
        return s + "}";
    }
    return "null";
}

// ─── std/collections value-equality helper ───────────────────────────────────

static bool zephyr_value_eq(const ZephyrValue& a, const ZephyrValue& b) {
    if (a.kind() != b.kind()) return false;
    switch (a.kind()) {
        case ZephyrValue::Kind::Nil:    return true;
        case ZephyrValue::Kind::Bool:   return a.as_bool()   == b.as_bool();
        case ZephyrValue::Kind::Int:    return a.as_int()    == b.as_int();
        case ZephyrValue::Kind::Float:  return a.as_float()  == b.as_float();
        case ZephyrValue::Kind::String: return a.as_string() == b.as_string();
        default:                        return false;
    }
}

// ─── std/collections host-handle factory ─────────────────────────────────────

static ZephyrValue make_collection_handle(std::shared_ptr<ZephyrHostClass> cls,
                                          std::shared_ptr<void>            instance) {
    ZephyrHostObjectRef ref;
    ref.host_class             = std::move(cls);
    ref.instance               = std::move(instance);
    ref.strong_residency       = true;
    ref.has_explicit_policy    = true;
    ref.policy.allow_field_store       = true;
    ref.policy.allow_closure_capture   = true;
    ref.policy.allow_coroutine_capture = true;
    ref.policy.strong_residency_allowed = true;
    return ZephyrValue(ref);
}

void Runtime::install_core() {
    register_global_function(
        "print",
        [](const std::vector<ZephyrValue>& args) {
            bool first = true;
            for (const auto& value : args) {
                if (!first) {
                    std::cout << " ";
                }
                first = false;
                std::cout << to_string(value);
            }
            std::cout << std::endl;
            return ZephyrValue();
        },
        {},
        "Nil");

    register_global_function(
        "assert",
        [](const std::vector<ZephyrValue>& args) {
            if (args.empty() || !args.front().is_bool() || !args.front().as_bool()) {
                fail("Assertion failed.");
            }
            return ZephyrValue();
        },
        {},
        "Nil");

    register_global_function(
        "len",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) {
                fail("len expects one argument.");
            }
            const auto& value = args.front();
            switch (value.kind()) {
                case ZephyrValue::Kind::String:
                    return ZephyrValue(static_cast<std::int64_t>(value.as_string().size()));
                case ZephyrValue::Kind::Array:
                    return ZephyrValue(static_cast<std::int64_t>(value.as_array().size()));
                default:
                    fail("len expects String or Array.");
            }
        },
        {}, "int");
    register_global_function(
        "__zephyr_std_len",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) {
                fail("__zephyr_std_len expects one argument.");
            }
            const auto& value = args.front();
            switch (value.kind()) {
                case ZephyrValue::Kind::String:
                    return ZephyrValue(static_cast<std::int64_t>(value.as_string().size()));
                case ZephyrValue::Kind::Array:
                    return ZephyrValue(static_cast<std::int64_t>(value.as_array().size()));
                default:
                    fail("__zephyr_std_len expects String or Array.");
            }
        },
        {}, "int");

    register_global_function(
        "str",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) {
                fail("str expects one argument.");
            }
            return ZephyrValue(to_string(args.front()));
        },
        {}, "string");

    register_global_function(
        "contains",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
                fail("contains expects (String, String).");
            }
            return ZephyrValue(args[0].as_string().find(args[1].as_string()) != std::string::npos);
        },
        {"string", "string"}, "bool");

    register_global_function(
        "starts_with",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
                fail("starts_with expects (String, String).");
            }
            return ZephyrValue(args[0].as_string().starts_with(args[1].as_string()));
        },
        {"string", "string"}, "bool");
    register_global_function(
        "__zephyr_std_starts_with",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
                fail("__zephyr_std_starts_with expects (String, String).");
            }
            return ZephyrValue(args[0].as_string().starts_with(args[1].as_string()));
        },
        {"string", "string"}, "bool");

    register_global_function(
        "ends_with",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
                fail("ends_with expects (String, String).");
            }
            return ZephyrValue(args[0].as_string().ends_with(args[1].as_string()));
        },
        {"string", "string"}, "bool");
    register_global_function(
        "__zephyr_std_ends_with",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
                fail("__zephyr_std_ends_with expects (String, String).");
            }
            return ZephyrValue(args[0].as_string().ends_with(args[1].as_string()));
        },
        {"string", "string"}, "bool");

    register_global_function(
        "push",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_array()) {
                fail("push expects (Array, value).");
            }
            auto values = args[0].as_array();
            values.push_back(args[1]);
            return ZephyrValue(std::move(values));
        },
        {"Array", "any"},
        "Array");
    register_global_function(
        "__zephyr_std_push",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_array()) {
                fail("__zephyr_std_push expects (Array, value).");
            }
            auto values = args[0].as_array();
            values.push_back(args[1]);
            return ZephyrValue(std::move(values));
        },
        {"Array", "any"},
        "Array");

    register_global_function(
        "concat",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_array() || !args[1].is_array()) {
                fail("concat expects (Array, Array).");
            }
            auto values = args[0].as_array();
            const auto& extra = args[1].as_array();
            values.insert(values.end(), extra.begin(), extra.end());
            return ZephyrValue(std::move(values));
        },
        {"Array", "Array"},
        "Array");

    register_global_function(
        "join",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_array() || !args[1].is_string()) {
                fail("join expects (Array, String).");
            }
            const auto& values = args[0].as_array();
            const auto& separator = args[1].as_string();
            std::ostringstream out;
            for (std::size_t index = 0; index < values.size(); ++index) {
                if (index > 0) {
                    out << separator;
                }
                out << to_string(values[index]);
            }
            return ZephyrValue(out.str());
        },
        {"Array", "string"}, "string");

    register_global_function(
        "range",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_int() || !args[1].is_int()) {
                fail("range expects (Int, Int).");
            }
            const auto start = args[0].as_int();
            const auto end = args[1].as_int();
            ZephyrValue::Array values;
            if (start <= end) {
                values.reserve(static_cast<std::size_t>(end - start));
                for (std::int64_t value = start; value < end; ++value) {
                    values.emplace_back(value);
                }
            } else {
                values.reserve(static_cast<std::size_t>(start - end));
                for (std::int64_t value = start; value > end; --value) {
                    values.emplace_back(value);
                }
            }
            return ZephyrValue(std::move(values));
        },
        {"int", "int"},
        "Array");
    register_global_function(
        "__zephyr_std_range",
        [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_int() || !args[1].is_int()) {
                fail("__zephyr_std_range expects (Int, Int).");
            }
            const auto start = args[0].as_int();
            const auto end = args[1].as_int();
            ZephyrValue::Array values;
            if (start <= end) {
                values.reserve(static_cast<std::size_t>(end - start));
                for (std::int64_t value = start; value < end; ++value) {
                    values.emplace_back(value);
                }
            } else {
                values.reserve(static_cast<std::size_t>(start - end));
                for (std::int64_t value = start; value > end; --value) {
                    values.emplace_back(value);
                }
            }
            return ZephyrValue(std::move(values));
        },
        {"int", "int"},
        "Array");

    // Built-in Result enum with Ok and Err variants
    {
        auto* result_type = allocate<EnumTypeObject>("Result");
        EnumVariantSpec ok_spec;
        ok_spec.name = "Ok";
        ok_spec.payload_types.push_back("");
        result_type->variants.push_back(ok_spec);
        EnumVariantSpec err_spec;
        err_spec.name = "Err";
        err_spec.payload_types.push_back("");
        result_type->variants.push_back(err_spec);
        define_value(root_environment_, "Result", Value::object(result_type), false);
    }

    // std/math built-in module
    register_module("std/math", [](ZephyrModuleBinder& m) {
        m.add_constant("pi", ZephyrValue(3.14159265358979323846));
        m.add_constant("e",  ZephyrValue(2.71828182845904523536));
        m.add_function("floor", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("floor expects 1 argument.");
            return ZephyrValue(std::floor(args[0].as_float()));
        }, {}, "float");
        m.add_function("ceil", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("ceil expects 1 argument.");
            return ZephyrValue(std::ceil(args[0].as_float()));
        }, {}, "float");
        m.add_function("sqrt", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("sqrt expects 1 argument.");
            return ZephyrValue(std::sqrt(args[0].as_float()));
        }, {}, "float");
        m.add_function("abs", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("abs expects 1 argument.");
            if (args[0].is_int()) return ZephyrValue(static_cast<std::int64_t>(std::abs(args[0].as_int())));
            return ZephyrValue(std::abs(args[0].as_float()));
        }, {}, "");
        m.add_function("min", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 2) fail("min expects 2 arguments.");
            const double a = args[0].as_float(), b = args[1].as_float();
            return ZephyrValue(a < b ? a : b);
        }, {}, "float");
        m.add_function("max", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 2) fail("max expects 2 arguments.");
            const double a = args[0].as_float(), b = args[1].as_float();
            return ZephyrValue(a > b ? a : b);
        }, {}, "float");
        m.add_function("pow", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2) fail("pow expects 2 arguments.");
            return ZephyrValue(std::pow(args[0].as_float(), args[1].as_float()));
        }, {}, "float");
        m.add_function("sin", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("sin expects 1 argument.");
            return ZephyrValue(std::sin(args[0].as_float()));
        }, {}, "float");
        m.add_function("cos", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("cos expects 1 argument.");
            return ZephyrValue(std::cos(args[0].as_float()));
        }, {}, "float");
        m.add_function("log", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("log expects 1 argument.");
            return ZephyrValue(std::log(args[0].as_float()));
        }, {}, "float");
        m.add_function("round", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1) fail("round expects 1 argument.");
            return ZephyrValue(std::round(args[0].as_float()));
        }, {}, "float");
        m.add_function("clamp", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 3) fail("clamp expects 3 arguments.");
            const double val = args[0].as_float();
            const double lo  = args[1].as_float();
            const double hi  = args[2].as_float();
            return ZephyrValue(val < lo ? lo : val > hi ? hi : val);
        }, {}, "float");
    });

    // std/string built-in module
    register_module("std/string", [](ZephyrModuleBinder& m) {
        m.add_function("len", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1 || !args[0].is_string()) fail("len expects a String argument.");
            return ZephyrValue(static_cast<std::int64_t>(args[0].as_string().size()));
        }, {"string"}, "int");
        m.add_function("upper", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1 || !args[0].is_string()) fail("upper expects a String argument.");
            std::string r = args[0].as_string();
            std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return ZephyrValue(std::move(r));
        }, {"string"}, "string");
        m.add_function("lower", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1 || !args[0].is_string()) fail("lower expects a String argument.");
            std::string r = args[0].as_string();
            std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ZephyrValue(std::move(r));
        }, {"string"}, "string");
        m.add_function("trim", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 1 || !args[0].is_string()) fail("trim expects a String argument.");
            const std::string& s = args[0].as_string();
            std::size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
            std::size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            return ZephyrValue(s.substr(start, end - start));
        }, {"string"}, "string");
        m.add_function("starts_with", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) fail("starts_with expects (String, String).");
            return ZephyrValue(args[0].as_string().starts_with(args[1].as_string()));
        }, {"string", "string"}, "bool");
        m.add_function("ends_with", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) fail("ends_with expects (String, String).");
            return ZephyrValue(args[0].as_string().ends_with(args[1].as_string()));
        }, {"string", "string"}, "bool");
        m.add_function("contains", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) fail("contains expects (String, String).");
            return ZephyrValue(args[0].as_string().find(args[1].as_string()) != std::string::npos);
        }, {"string", "string"}, "bool");
        m.add_function("split", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) fail("split expects (String, String).");
            const std::string& s   = args[0].as_string();
            const std::string& sep = args[1].as_string();
            std::vector<ZephyrValue> parts;
            if (sep.empty()) {
                for (char c : s) parts.emplace_back(std::string(1, c));
            } else {
                std::size_t pos = 0, found;
                while ((found = s.find(sep, pos)) != std::string::npos) {
                    parts.emplace_back(s.substr(pos, found - pos));
                    pos = found + sep.size();
                }
                parts.emplace_back(s.substr(pos));
            }
            return ZephyrValue(std::move(parts));
        }, {"string", "string"}, "Array");
        m.add_function("join", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 2 || !args[0].is_array() || !args[1].is_string()) fail("join expects (Array, String).");
            const auto& arr = args[0].as_array();
            const std::string& sep = args[1].as_string();
            std::string result;
            for (std::size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) result += sep;
                if (arr[i].is_string()) result += arr[i].as_string();
                else fail("join: array elements must be strings.");
            }
            return ZephyrValue(std::move(result));
        }, {"Array", "string"}, "string");
        m.add_function("replace", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 3 || !args[0].is_string() || !args[1].is_string() || !args[2].is_string())
                fail("replace expects (String, String, String).");
            std::string result = args[0].as_string();
            const std::string& from = args[1].as_string();
            const std::string& to   = args[2].as_string();
            if (!from.empty()) {
                std::size_t pos = 0;
                while ((pos = result.find(from, pos)) != std::string::npos) {
                    result.replace(pos, from.size(), to);
                    pos += to.size();
                }
            }
            return ZephyrValue(std::move(result));
        }, {"string", "string", "string"}, "string");
        m.add_function("substr", [](const std::vector<ZephyrValue>& args) {
            if (args.size() != 3 || !args[0].is_string() || !args[1].is_int() || !args[2].is_int())
                fail("substr expects (String, Int, Int).");
            const std::string& s = args[0].as_string();
            const std::int64_t start = args[1].as_int();
            const std::int64_t len   = args[2].as_int();
            if (start < 0 || len < 0 || static_cast<std::size_t>(start) > s.size())
                return ZephyrValue(std::string{});
            return ZephyrValue(s.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(len)));
        }, {"string", "int", "int"}, "string");
        m.add_function("to_int", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 1 || !args[0].is_string()) fail("to_int expects a String argument.");
            const std::string& s = args[0].as_string();
            std::int64_t val = 0;
            const char* begin = s.data();
            const char* end   = s.data() + s.size();
            const auto [parsed, ec] = std::from_chars(begin, end, val);
            ZephyrEnumValue result;
            result.type_name = "Result";
            if (ec == std::errc{} && parsed == end) {
                result.variant_name = "Ok";
                result.payload.emplace_back(val);
            } else {
                result.variant_name = "Err";
                result.payload.emplace_back(std::string("parse error"));
            }
            return ZephyrValue(result);
        }, {"string"}, "Result");
        m.add_function("to_float", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 1 || !args[0].is_string()) fail("to_float expects a String argument.");
            const std::string& s = args[0].as_string();
            char* parsed_end = nullptr;
            errno = 0;
            const double val = std::strtod(s.c_str(), &parsed_end);
            const bool ok = (errno == 0 && parsed_end == s.c_str() + s.size() && !s.empty());
            ZephyrEnumValue result;
            result.type_name = "Result";
            if (ok) {
                result.variant_name = "Ok";
                result.payload.emplace_back(val);
            } else {
                result.variant_name = "Err";
                result.payload.emplace_back(std::string("parse error"));
            }
            return ZephyrValue(result);
        }, {"string"}, "Result");
        m.add_function("repeat", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 2 || !args[0].is_string() || !args[1].is_int())
                fail("repeat expects (String, Int).");
            const std::string& s = args[0].as_string();
            const std::int64_t n = args[1].as_int();
            std::string result;
            for (std::int64_t i = 0; i < n; ++i) result += s;
            return ZephyrValue(std::move(result));
        }, {"string", "int"}, "string");
    });

    // std/json built-in module
    register_module("std/json", [](ZephyrModuleBinder& m) {
        m.add_function("parse", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 1 || !args[0].is_string())
                fail("json.parse expects a string argument.");
            std::size_t pos = 0;
            return json_parse_value(args[0].as_string(), pos);
        }, {"string"}, "any");

        m.add_function("stringify", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 1) fail("json.stringify expects 1 argument.");
            return ZephyrValue(json_stringify_value(args[0]));
        }, {"any"}, "string");

        m.add_function("parse_safe", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            if (args.size() != 1 || !args[0].is_string())
                fail("json.parse_safe expects a string argument.");
            ZephyrEnumValue result;
            result.type_name = "Result";
            try {
                std::size_t pos = 0;
                ZephyrValue parsed = json_parse_value(args[0].as_string(), pos);
                result.variant_name = "Ok";
                result.payload.emplace_back(std::move(parsed));
            } catch (const std::exception& e) {
                result.variant_name = "Err";
                result.payload.emplace_back(std::string(e.what()));
            }
            return ZephyrValue(result);
        }, {"string"}, "any");
    });

    // std/io built-in module
    register_module("std/io", [this](ZephyrModuleBinder& m) {
        m.add_function("print", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cout << " ";
                first = false;
                std::cout << to_string(v);
            }
            std::cout << std::endl;
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("println", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cout << " ";
                first = false;
                std::cout << to_string(v);
            }
            std::cout << "\n";
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("eprint", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cerr << " ";
                first = false;
                std::cerr << to_string(v);
            }
            std::cerr << std::endl;
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("eprintln", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cerr << " ";
                first = false;
                std::cerr << to_string(v);
            }
            std::cerr << "\n";
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("read_line", [](const std::vector<ZephyrValue>&) -> ZephyrValue {
            std::string line;
            if (!std::getline(std::cin, line)) return ZephyrValue(std::string(""));
            return ZephyrValue(line);
        }, {}, "string");
    });

    // std/gc built-in module
    register_module("std/gc", [this](ZephyrModuleBinder& m) {
        m.add_function("pause_p50_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p50_ns / 1000));
        }, {}, "int");

        m.add_function("pause_p95_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p95_ns / 1000));
        }, {}, "int");

        m.add_function("pause_p99_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p99_ns / 1000));
        }, {}, "int");

        m.add_function("frame_miss_count", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().frame_budget_miss_count));
        }, {}, "int");

        m.add_function("collect", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            collect_garbage();
            return ZephyrValue();
        }, {}, "Nil");
    });

    // std/profiler built-in module
    register_module("std/profiler", [this](ZephyrModuleBinder& m) {
        m.add_function("start", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            start_profiling();
            return ZephyrValue();
        }, {}, "Nil");

        // stop() returns array of [name, calls, total_us, self_us] per entry
        m.add_function("stop", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            ZephyrProfileReport report = stop_profiling();
            std::vector<ZephyrValue> entries;
            entries.reserve(report.entries.size());
            for (const auto& e : report.entries) {
                std::vector<ZephyrValue> row = {
                    ZephyrValue(e.function_name),
                    ZephyrValue(static_cast<std::int64_t>(e.call_count)),
                    ZephyrValue(static_cast<std::int64_t>(e.total_ns / 1000)),
                    ZephyrValue(static_cast<std::int64_t>(e.self_ns  / 1000)),
                };
                entries.push_back(ZephyrValue(std::move(row)));
            }
            return ZephyrValue(std::move(entries));
        }, {}, "Array");
    });

    // std/collections — HashMap, Set, Queue host-backed global functions
    {
        static auto s_hashmap_class = std::make_shared<ZephyrHostClass>("HashMap");
        static auto s_set_class     = std::make_shared<ZephyrHostClass>("Set");
        static auto s_queue_class   = std::make_shared<ZephyrHostClass>("Queue");

        using HashMap = std::unordered_map<std::string, ZephyrValue>;
        using SetVec  = std::vector<ZephyrValue>;
        using Queue   = std::deque<ZephyrValue>;

        register_global_function("__zephyr_hashmap_new",
            [](const std::vector<ZephyrValue>&) -> ZephyrValue {
                return make_collection_handle(s_hashmap_class,
                    std::static_pointer_cast<void>(std::make_shared<HashMap>()));
            }, {}, "any");

        register_global_function("__zephyr_hashmap_set",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 3 || !args[0].is_host_object() || !args[1].is_string())
                    fail("HashMap.set expects (HashMap, string, any).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                (*m)[args[1].as_string()] = args[2];
                return ZephyrValue();
            }, {}, "any");

        register_global_function("__zephyr_hashmap_get",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object() || !args[1].is_string())
                    fail("HashMap.get expects (HashMap, string).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                const auto it = m->find(args[1].as_string());
                return it == m->end() ? ZephyrValue() : it->second;
            }, {}, "any");

        register_global_function("__zephyr_hashmap_has",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object() || !args[1].is_string())
                    fail("HashMap.has expects (HashMap, string).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                return ZephyrValue(m->count(args[1].as_string()) > 0);
            }, {}, "bool");

        register_global_function("__zephyr_hashmap_delete",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object() || !args[1].is_string())
                    fail("HashMap.delete expects (HashMap, string).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                m->erase(args[1].as_string());
                return ZephyrValue();
            }, {}, "any");

        register_global_function("__zephyr_hashmap_keys",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("HashMap.keys expects (HashMap).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                ZephyrValue::Array keys;
                keys.reserve(m->size());
                for (const auto& kv : *m) keys.emplace_back(kv.first);
                return ZephyrValue(std::move(keys));
            }, {}, "Array");

        register_global_function("__zephyr_hashmap_values",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("HashMap.values expects (HashMap).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                ZephyrValue::Array vals;
                vals.reserve(m->size());
                for (const auto& kv : *m) vals.push_back(kv.second);
                return ZephyrValue(std::move(vals));
            }, {}, "Array");

        register_global_function("__zephyr_hashmap_size",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("HashMap.size expects (HashMap).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                return ZephyrValue(static_cast<std::int64_t>(m->size()));
            }, {}, "int");

        register_global_function("__zephyr_hashmap_clear",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("HashMap.clear expects (HashMap).");
                auto* m = static_cast<HashMap*>(args[0].as_host_object().instance.get());
                m->clear();
                return ZephyrValue();
            }, {}, "any");

        register_global_function("__zephyr_set_new",
            [](const std::vector<ZephyrValue>&) -> ZephyrValue {
                return make_collection_handle(s_set_class,
                    std::static_pointer_cast<void>(std::make_shared<SetVec>()));
            }, {}, "any");

        register_global_function("__zephyr_set_add",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object())
                    fail("Set.add expects (Set, any).");
                auto* s = static_cast<SetVec*>(args[0].as_host_object().instance.get());
                for (const auto& v : *s)
                    if (zephyr_value_eq(v, args[1])) return ZephyrValue();
                s->push_back(args[1]);
                return ZephyrValue();
            }, {}, "any");

        register_global_function("__zephyr_set_has",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object())
                    fail("Set.has expects (Set, any).");
                auto* s = static_cast<SetVec*>(args[0].as_host_object().instance.get());
                for (const auto& v : *s)
                    if (zephyr_value_eq(v, args[1])) return ZephyrValue(true);
                return ZephyrValue(false);
            }, {}, "bool");

        register_global_function("__zephyr_set_delete",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object())
                    fail("Set.delete expects (Set, any).");
                auto* s = static_cast<SetVec*>(args[0].as_host_object().instance.get());
                s->erase(std::remove_if(s->begin(), s->end(),
                    [&](const ZephyrValue& v) { return zephyr_value_eq(v, args[1]); }),
                    s->end());
                return ZephyrValue();
            }, {}, "any");

        register_global_function("__zephyr_set_size",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Set.size expects (Set).");
                auto* s = static_cast<SetVec*>(args[0].as_host_object().instance.get());
                return ZephyrValue(static_cast<std::int64_t>(s->size()));
            }, {}, "int");

        register_global_function("__zephyr_set_to_array",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Set.to_array expects (Set).");
                auto* s = static_cast<SetVec*>(args[0].as_host_object().instance.get());
                return ZephyrValue(ZephyrValue::Array(s->begin(), s->end()));
            }, {}, "Array");

        register_global_function("__zephyr_queue_new",
            [](const std::vector<ZephyrValue>&) -> ZephyrValue {
                return make_collection_handle(s_queue_class,
                    std::static_pointer_cast<void>(std::make_shared<Queue>()));
            }, {}, "any");

        register_global_function("__zephyr_queue_push",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 2 || !args[0].is_host_object())
                    fail("Queue.push expects (Queue, any).");
                auto* q = static_cast<Queue*>(args[0].as_host_object().instance.get());
                q->push_back(args[1]);
                return ZephyrValue();
            }, {}, "any");

        register_global_function("__zephyr_queue_pop",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Queue.pop expects (Queue).");
                auto* q = static_cast<Queue*>(args[0].as_host_object().instance.get());
                if (q->empty()) return ZephyrValue();
                ZephyrValue front = q->front();
                q->pop_front();
                return front;
            }, {}, "any");

        register_global_function("__zephyr_queue_peek",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Queue.peek expects (Queue).");
                auto* q = static_cast<Queue*>(args[0].as_host_object().instance.get());
                return q->empty() ? ZephyrValue() : q->front();
            }, {}, "any");

        register_global_function("__zephyr_queue_size",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Queue.size expects (Queue).");
                auto* q = static_cast<Queue*>(args[0].as_host_object().instance.get());
                return ZephyrValue(static_cast<std::int64_t>(q->size()));
            }, {}, "int");

        register_global_function("__zephyr_queue_is_empty",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Queue.is_empty expects (Queue).");
                auto* q = static_cast<Queue*>(args[0].as_host_object().instance.get());
                return ZephyrValue(q->empty());
            }, {}, "bool");

        register_global_function("__zephyr_queue_to_array",
            [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
                if (args.size() != 1 || !args[0].is_host_object())
                    fail("Queue.to_array expects (Queue).");
                auto* q = static_cast<Queue*>(args[0].as_host_object().instance.get());
                return ZephyrValue(ZephyrValue::Array(q->begin(), q->end()));
            }, {}, "Array");
    }

    // std/io built-in module
    register_module("std/io", [](ZephyrModuleBinder& m) {
        m.add_function("print", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cout << " ";
                first = false;
                std::cout << to_string(v);
            }
            std::cout << std::endl;
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("println", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cout << " ";
                first = false;
                std::cout << to_string(v);
            }
            std::cout << "\n";
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("eprint", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cerr << " ";
                first = false;
                std::cerr << to_string(v);
            }
            std::cerr << std::endl;
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("eprintln", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
            bool first = true;
            for (const auto& v : args) {
                if (!first) std::cerr << " ";
                first = false;
                std::cerr << to_string(v);
            }
            std::cerr << "\n";
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("read_line", [](const std::vector<ZephyrValue>&) -> ZephyrValue {
            std::string line;
            if (!std::getline(std::cin, line)) return ZephyrValue(std::string(""));
            return ZephyrValue(line);
        }, {}, "string");
    });

    // std/gc built-in module
    register_module("std/gc", [this](ZephyrModuleBinder& m) {
        m.add_function("pause_p50_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p50_ns / 1000));
        }, {}, "int");

        m.add_function("pause_p95_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p95_ns / 1000));
        }, {}, "int");

        m.add_function("pause_p99_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p99_ns / 1000));
        }, {}, "int");

        m.add_function("frame_miss_count", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().frame_budget_miss_count));
        }, {}, "int");

        m.add_function("collect", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            collect_garbage();
            return ZephyrValue();
        }, {}, "Nil");
    });

    // std/profiler built-in module
    register_module("std/profiler", [this](ZephyrModuleBinder& m) {
        m.add_function("start", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            start_profiling();
            return ZephyrValue();
        }, {}, "Nil");

        m.add_function("stop", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
            ZephyrProfileReport report = stop_profiling();
            ZephyrValue::Array entries;
            entries.reserve(report.entries.size());
            for (const auto& e : report.entries) {
                ZephyrValue::Array row = {
                    ZephyrValue(e.function_name),
                    ZephyrValue(static_cast<std::int64_t>(e.call_count)),
                    ZephyrValue(static_cast<std::int64_t>(e.total_ns / 1000)),
                    ZephyrValue(static_cast<std::int64_t>(e.self_ns  / 1000)),
                };
                entries.push_back(ZephyrValue(std::move(row)));
            }
            return ZephyrValue(std::move(entries));
        }, {}, "Array");
    });
}

void Runtime::mark_roots() {
    visit_root_references(
        [this](GcObject* object) { mark_object(object); },
        [this](const Value& value) { mark_value(value); });
}

void Runtime::request_gc_cycle() {
    gc_collection_kind_ = GcCollectionKind::Full;
    gc_cycle_requested_ = true;
}

void Runtime::begin_gc_cycle() {
    if (!full_gc_pause_active_) {
        full_gc_pause_active_ = true;
        full_gc_pause_start_ = ProfileClock::now();
        full_gc_heap_before_ = live_bytes_;
        record_gc_trace_event(GCTraceEvent::Type::FullStart, full_gc_heap_before_, full_gc_heap_before_);
    }
    gray_stack_.clear();
    dirty_root_environments_.clear();
    dirty_objects_.clear();
    remembered_objects_.clear();
    for (auto* space : all_spaces_) {
        space->begin_cycle();
        space->for_each_object([](GcObject* object) {
            object->header.color = GcColor::White;
            object->header.flags &= static_cast<std::uint8_t>(~(GcDirtyQueuedBit | GcMinorRememberedBit));
            object->header.next_gray = nullptr;
        });
    }
    ++total_gc_cycles_;
    ++total_major_gc_cycles_;
    ++total_full_collections_;
    gc_phase_ = ZephyrGcPhase::SeedRoots;
}

void Runtime::seed_roots() {
    mark_roots();
    gc_phase_ = gray_stack_.empty() ? ZephyrGcPhase::RescanDirtyRoots : ZephyrGcPhase::DrainGray;
}

void Runtime::process_dirty_roots() {
    for (auto* environment : dirty_root_environments_) {
        if (environment != nullptr) {
            environment->header.flags &= static_cast<std::uint8_t>(~GcDirtyQueuedBit);
        }
        mark_object(environment);
    }
    dirty_root_environments_.clear();

    for (auto* object : dirty_objects_) {
        if (object != nullptr) {
            object->header.flags &= static_cast<std::uint8_t>(~GcDirtyQueuedBit);
        }
        mark_object(object);
    }
    dirty_objects_.clear();

    gc_phase_ = gray_stack_.empty() ? ZephyrGcPhase::SweepObjects : ZephyrGcPhase::DrainGray;
    if (gc_phase_ == ZephyrGcPhase::SweepObjects) {
        // Initialize all sweep cursors at the mark→sweep transition.
        // process_dirty_roots() transitions to SweepObjects exactly once per cycle,
        // so calling begin_sweep() here is safe (no risk of re-initialization).
        // Objects allocated AFTER this point are prepended before each cursor and
        // will NOT be swept this cycle (they are live new allocations).
        nursery_.begin_sweep();
        old_small_.begin_sweep();
    }
}

void Runtime::drain_gray(std::size_t& budget_work) {
    while (budget_work > 0 && !gray_stack_.empty()) {
        GcObject* object = gray_stack_.back();
        gray_stack_.pop_back();
        if (object == nullptr || object->header.color != GcColor::Gray) {
            --budget_work;
            continue;
        }
        object->trace(*this);
        object->header.color = GcColor::Black;
        --budget_work;
    }
    if (gray_stack_.empty()) {
        gc_phase_ = ZephyrGcPhase::RescanDirtyRoots;
    }
}

void Runtime::sweep(std::size_t& budget_work) {
    // ── nursery_ sweep (young objects — full GC path) ────────────────────────
    // NurserySpace::sweep() decrements both nursery_.live_bytes_own_ and the bound
    // Runtime::live_bytes_ for each freed object.
    if (budget_work > 0) {
        nursery_.sweep(budget_work);
    }

    // ── old_small_ sweep (linked-list, non-large old objects) ────────────────
    // OldSmallSpace::sweep() decrements both old_small_.live_bytes_ and the bound
    // Runtime::live_bytes_ for each freed object.
    if (budget_work > 0) {
        old_small_.sweep(budget_work);
    }

    // ── los_ sweep (doubly-linked node list) ─────────────────────────────────
    // LargeObjectSpace::sweep() decrements both los_.live_bytes_ and the bound
    // Runtime::live_bytes_ for each freed object.
    if (budget_work > 0) {
        los_.sweep(budget_work);
    }

    // ── pinned_ color-reset ───────────────────────────────────────────────────
    // PinnedSpace objects are never freed; their sweep() only resets Black→White.
    // This runs non-budgeted (pinned set is tiny: root env + module envs).
    const bool all_swept = nursery_.sweep_complete()
                        && old_small_.sweep_complete()
                        && los_.sweep_complete();
    if (all_swept) {
        pinned_.sweep(budget_work);
    }

    // Transition out of SweepObjects only when all spaces are fully swept.
    if (all_swept) {
        gc_phase_ = detach_queue_.empty() ? ZephyrGcPhase::Complete : ZephyrGcPhase::DetachQueue;
    }
}

void Runtime::process_detach_queue(std::size_t& budget_work) {
    while (budget_work > 0 && !detach_queue_.empty()) {
        GcObject* object = detach_queue_.back();
        detach_queue_.pop_back();
        if (object->kind == ObjectKind::Coroutine) {
            record_coroutine_destroyed(static_cast<CoroutineObject*>(object));
        }
        if (object->header.space_kind == GcSpaceKind::LargeObject) {
            // LargeObjectSpace::free_object decrements both los_.live_bytes_
            // and Runtime::live_bytes_ (via bound global pointer).
            los_.free_object(object);
        } else if (object->header.space_kind == GcSpaceKind::OldSmall) {
            // Phase 4C: slab objects carry GcSlabBit and must be freed via
            // free_slab_object(); linked-list objects use free_object().
            if ((object->header.flags & GcSlabBit) != 0) {
                old_small_.free_slab_object(object);
            } else if ((object->header.flags & GcBumpAllocBit) != 0) {
                // Phase 5C: promoted bump object in detach queue.
                old_small_.free_bump_object(object);
            } else {
                old_small_.free_object(object);
            }
        } else if (object->header.space_kind == GcSpaceKind::Nursery) {
            // NurserySpace finalizable object: must decrement both per-space and global counters.
            // Phase 5C: bump-allocated objects live in chunk memory — destructor only.
            nursery_.live_bytes_own_ -= object->header.size_bytes;
            live_bytes_ -= object->header.size_bytes;
            if ((object->header.flags & GcBumpAllocBit) != 0) {
                object->~GcObject();
            } else {
                delete object;
            }
        } else if (object->header.space_kind == GcSpaceKind::Pinned) {
            // PinnedSpace::free_object decrements both pinned_.live_bytes_
            // and Runtime::live_bytes_ (via bound global pointer).
            pinned_.free_object(object);
        } else {
            // All valid GcSpaceKind values must have an explicit branch above.
            // Reaching here means space_kind was never set (Uninitialized) or
            // a new space kind was added without updating this dispatch.
            assert(false && "process_detach_queue: unhandled space_kind — "
                            "add an explicit branch for every GcSpaceKind value");
            live_bytes_ -= object->header.size_bytes;
            delete object;
        }
        --budget_work;
    }
    if (detach_queue_.empty()) {
        gc_phase_ = ZephyrGcPhase::Complete;
    }
}

void Runtime::maybe_run_gc_stress_safe_point() {
    if (!gc_stress_enabled_) {
        return;
    }
    ++total_gc_stress_safe_points_;
    request_gc_cycle();
    gc_step(gc_stress_budget_work_);
}

void Runtime::note_write(Environment* environment, const Value& value) {
    ++barrier_hits_;
    // Fast path: young environments never need the cross-generation remembered set,
    // and when the GC is not actively marking there is no further work to do.
    // During marking we still need dirty-queue tracking even for young objects
    // to uphold the tri-color invariant.
    if (environment == nullptr || (!is_old_object(environment) && !gc_marking())) {
        return;
    }
    if (is_old_object(environment) && value.is_object() && value.as_object() != nullptr &&
        !is_old_object(value.as_object())) {
        remember_minor_owner(environment);
    }
    if (!gc_marking()) {
        return;
    }
    if (environment->kind == EnvironmentKind::Root || environment->kind == EnvironmentKind::Module) {
        if ((environment->header.flags & GcDirtyQueuedBit) == 0) {
            environment->header.flags |= GcDirtyQueuedBit;
            dirty_root_environments_.push_back(environment);
        }
    }
    mark_value(value);
}

void Runtime::note_write(GcObject* owner, const Value& value) {
    ++barrier_hits_;
    // Fast path: young objects never need the cross-generation remembered set,
    // and when the GC is not actively marking there is no further work to do.
    // During marking we still need dirty-queue tracking even for young objects
    // to uphold the tri-color invariant.
    if (owner == nullptr || (!is_old_object(owner) && !gc_marking())) {
        return;
    }
    if (is_old_object(owner) && value.is_object() && value.as_object() != nullptr &&
        !is_old_object(value.as_object())) {
        remember_minor_owner(owner);
    }
    if (!gc_marking()) {
        return;
    }
    if ((owner->header.flags & GcDirtyQueuedBit) == 0) {
        owner->header.flags |= GcDirtyQueuedBit;
        dirty_objects_.push_back(owner);
    }
    mark_value(value);
}

bool Runtime::gc_marking() const {
    return gc_phase_ == ZephyrGcPhase::SeedRoots || gc_phase_ == ZephyrGcPhase::DrainGray ||
           gc_phase_ == ZephyrGcPhase::RescanDirtyRoots;
}

Value Runtime::make_string(std::string value) {
    return Value::object(allocate<StringObject>(std::move(value)));
}

Value Runtime::make_literal_string(std::string value) {
    constexpr std::size_t kInternedStringMaxLength = 64;
    if (value.size() > kInternedStringMaxLength) {
        return make_string(std::move(value));
    }
    return Value::object(intern_string(value));
}

StringObject* Runtime::intern_string(const std::string& value) {
    const auto found = interned_strings_.find(value);
    if (found != interned_strings_.end()) {
        ++string_intern_hits_;
        return found->second;
    }

    auto* string_object = allocate<StringObject>(value);
    string_object->is_interned = true;
    interned_strings_.emplace(string_object->value, string_object);
    ++string_intern_misses_;
    return string_object;
}

ZephyrHostHandlePolicy Runtime::default_policy_for_kind(ZephyrHostHandleKind kind) const {
    ZephyrHostHandlePolicy policy;
    policy.debug_mode = config_.handle.debug_invalid_access_mode;
    policy.release_mode = config_.handle.release_invalid_access_mode;
    policy.allow_field_store = true;
    policy.allow_closure_capture = true;
    policy.allow_coroutine_capture = true;
    policy.allow_serialize = false;
    policy.allow_cross_scene = false;
    policy.strong_residency_allowed = false;
    policy.weak_by_default = config_.handle.default_entity_handles_are_weak;

    switch (kind) {
        case ZephyrHostHandleKind::Texture:
        case ZephyrHostHandleKind::Mesh:
        case ZephyrHostHandleKind::Material:
            if (config_.handle.trap_invalid_gpu_resources) {
                policy.release_mode = ZephyrInvalidAccessMode::Trap;
            }
            break;
        case ZephyrHostHandleKind::Asset:
            policy.strong_residency_allowed = true;
            break;
        default:
            break;
    }
    return policy;
}

bool Runtime::handle_store_allowed(ZephyrHostHandleLifetime lifetime, const ZephyrHostHandlePolicy& policy,
                                   HandleContainerKind container) const {
    switch (container) {
        case HandleContainerKind::Stack:
            return true;
        case HandleContainerKind::HeapField:
        case HandleContainerKind::ArrayElement:
        case HandleContainerKind::Global:
            return lifetime != ZephyrHostHandleLifetime::Frame && lifetime != ZephyrHostHandleLifetime::Tick &&
                   policy.allow_field_store;
        case HandleContainerKind::ClosureCapture:
            return lifetime != ZephyrHostHandleLifetime::Frame && lifetime != ZephyrHostHandleLifetime::Tick &&
                   policy.allow_closure_capture;
        case HandleContainerKind::CoroutineFrame:
            return lifetime != ZephyrHostHandleLifetime::Frame && lifetime != ZephyrHostHandleLifetime::Tick &&
                   policy.allow_coroutine_capture;
        case HandleContainerKind::Serialize:
            return lifetime == ZephyrHostHandleLifetime::Stable && policy.allow_serialize;
    }
    return false;
}

bool Runtime::handle_can_cross_scene(ZephyrHostHandleLifetime lifetime, const ZephyrHostHandlePolicy& policy) const {
    return lifetime == ZephyrHostHandleLifetime::Stable && policy.allow_cross_scene;
}

UpvalueCellObject* Runtime::ensure_binding_cell(Environment* owner, const std::string& binding_name, Binding& binding,
                                                HandleContainerKind container) {
    if (binding.cell == nullptr) {
        auto* cell = allocate<UpvalueCellObject>(binding.value, binding.mutable_value, binding.type_name, container);
        binding.cell = cell;
        if (owner != nullptr) {
            note_environment_binding_write(owner, ensure_environment_binding_slot(owner, binding_name), Value::object(cell));
        }
    } else {
        binding.cell->mutable_value = binding.mutable_value;
        binding.cell->type_name = binding.type_name;
    }
    return binding.cell;
}

RuntimeResult<std::vector<UpvalueCellObject*>> Runtime::capture_upvalue_cells(Environment* environment,
                                                                              const std::vector<std::string>& upvalue_names,
                                                                              HandleContainerKind container,
                                                                              const Span& span,
                                                                              const std::string& module_name) {
    std::vector<UpvalueCellObject*> captured;
    captured.reserve(upvalue_names.size());
    const bool need_handle_validation = !host_handles_.empty();
    for (const auto& name : upvalue_names) {
        bool found = false;
        // Fast path: check immediate environment first (most common case for closures)
        if (environment != nullptr && environment->kind != EnvironmentKind::Root && environment->kind != EnvironmentKind::Module) {
            auto it = environment->values.find(name);
            if (it != environment->values.end()) {
                if (need_handle_validation) {
                    static const std::string capture_ctx = "closure capture";
                    ZEPHYR_TRY(validate_handle_store(read_binding_value(it->second), container, span, module_name, capture_ctx));
                }
                captured.push_back(ensure_binding_cell(environment, name, it->second, container));
                found = true;
            }
        }
        if (!found) {
            // Slow path: walk environment chain
            for (Environment* current = environment ? environment->parent : nullptr; current != nullptr; current = current->parent) {
                auto it = current->values.find(name);
                if (it == current->values.end()) continue;
                if (current->kind == EnvironmentKind::Root || current->kind == EnvironmentKind::Module) {
                    return make_loc_error<std::vector<UpvalueCellObject*>>(module_name, span,
                        "Internal compiler/runtime mismatch for captured upvalue '" + name + "'.");
                }
                if (need_handle_validation) {
                    static const std::string capture_ctx = "closure capture";
                    ZEPHYR_TRY(validate_handle_store(read_binding_value(it->second), container, span, module_name, capture_ctx));
                }
                captured.push_back(ensure_binding_cell(current, name, it->second, container));
                found = true;
                break;
            }
        }
        if (!found) {
            return make_loc_error<std::vector<UpvalueCellObject*>>(module_name, span,
                "Failed to resolve captured upvalue '" + name + "'.");
        }
    }
    return captured;
}

Environment* Runtime::module_or_root_environment(Environment* environment) const {
    Environment* fallback = root_environment_;
    Environment* found = nullptr;
    walk_environment_chain(environment, [&](Environment* current) {
        if (current->kind == EnvironmentKind::Module) {
            found = current;
            return false;
        }
        if (current->kind == EnvironmentKind::Root) {
            fallback = current;
        }
        return true;
    });
    return found != nullptr ? found : fallback;
}

Environment* Runtime::select_closure_environment(Environment* closure, const std::shared_ptr<BytecodeFunction>& bytecode) const {
    if (closure == nullptr || bytecode == nullptr || bytecode->requires_full_closure) {
        return closure;
    }
    return module_or_root_environment(closure);
}

VoidResult Runtime::ensure_ast_fallback_bytecode_supported(const BytecodeFunction* bytecode,
                                                           const Span& span,
                                                           const std::string& module_name,
                                                           const std::string& context) const {
#ifdef _DEBUG
    (void)bytecode;
    (void)span;
    (void)module_name;
    (void)context;
    return ok_result();
#else
    if (bytecode == nullptr || !bytecode->requires_full_closure) {
        return ok_result();
    }
    return make_loc_error<std::monostate>(module_name, span, ast_fallback_disabled_message(context));
#endif
}

void Runtime::install_upvalue_bindings(Environment* environment,
                                       const BytecodeFunction& bytecode,
                                       const std::vector<UpvalueCellObject*>& captured_upvalues) {
    if (environment == nullptr) {
        return;
    }

    const std::size_t count = std::min(bytecode.upvalue_names.size(), captured_upvalues.size());
    for (std::size_t index = 0; index < count; ++index) {
        auto* cell = captured_upvalues[index];
        if (cell == nullptr) {
            continue;
        }

        Binding binding;
        binding.value = cell->value;
        binding.cell = cell;
        binding.mutable_value = cell->mutable_value;
        binding.type_name = cell->type_name;
        const std::string& name = bytecode.upvalue_names[index];
        const std::size_t binding_index = ensure_environment_binding_slot(environment, name);
        environment->values[name] = std::move(binding);
        ++environment->version;
        note_environment_binding_write(environment, binding_index, Value::object(cell));
    }
}

RuntimeResult<ScriptFunctionObject*> Runtime::create_script_function(const std::string& name,
                                                                     const std::string& module_name,
                                                                     const std::vector<Param>& params,
                                                                     const std::optional<TypeRef>& return_type,
                                                                     BlockStmt* body,
                                                                     Environment* closure,
                                                                     std::shared_ptr<BytecodeFunction> bytecode,
                                                                     const Span& span,
                                                                     const std::vector<std::string>& generic_params,
                                                                     const std::vector<TraitBound>& where_clauses) {
    // Skip closure validation when no host handles are registered (common case).
    // validate_closure_capture walks the entire environment chain — expensive for hot closures.
    if (!host_handles_.empty()) {
        ZEPHYR_TRY(validate_closure_capture(closure, span, module_name.empty() ? name : module_name));
    }
    ZEPHYR_TRY(ensure_ast_fallback_bytecode_supported(bytecode.get(), span, module_name.empty() ? name : module_name, "Script function"));
    auto* function =
        allocate<ScriptFunctionObject>(name, module_name, params, return_type, body, select_closure_environment(closure, bytecode), span, bytecode, generic_params, where_clauses);
    if (bytecode != nullptr) {
        ZEPHYR_TRY_ASSIGN(captured_cells,
                          capture_upvalue_cells(closure, bytecode->upvalue_names, HandleContainerKind::ClosureCapture, span,
                                                module_name.empty() ? name : module_name));
        function->captured_upvalues = std::move(captured_cells);
    }
    return function;
}

VoidResult Runtime::ensure_capture_cells(Environment* environment, HandleContainerKind container, const Span& span,
                                         const std::string& module_name) {
    const std::string capture_context =
        container == HandleContainerKind::CoroutineFrame ? "coroutine capture" : "closure capture";
    VoidResult chain_result = ok_result();
    walk_environment_chain(environment, [&](Environment* current) {
        for (auto& entry : current->values) {
            const Value value = read_binding_value(entry.second);
            auto r = validate_handle_store(value, container, span, module_name, capture_context);
            if (!r) {
                chain_result = std::move(r);
                return false;
            }
            if (current->kind == EnvironmentKind::Root || current->kind == EnvironmentKind::Module) {
                continue;
            }
            ensure_binding_cell(current, entry.first, entry.second, container);
        }
        return true;
    });
    return chain_result;
}

ZephyrInvalidAccessMode Runtime::effective_invalid_access_mode(const HostHandleEntry& entry) const {
#ifdef _DEBUG
    return entry.policy.debug_mode;
#else
    return entry.policy.release_mode;
#endif
}

[[noreturn]] void Runtime::trap_invalid_handle(const std::string& message) const {
    fail(message);
}

RuntimeResult<std::uint32_t> Runtime::find_host_handle_slot(const ZephyrHostObjectRef& host_object) const {
    if (host_object.slot < host_handles_.size()) {
        const HostHandleEntry& entry = host_handles_[host_object.slot];
        if (host_object.generation == 0 || host_object.generation == entry.generation) {
            return host_object.slot;
        }
    }
    if (host_object.stable_guid.valid()) {
        const auto stable_it = stable_handle_lookup_.find(host_object.stable_guid);
        if (stable_it != stable_handle_lookup_.end()) {
            return stable_it->second;
        }
    }
    if (host_object.host_class == nullptr || host_object.instance == nullptr) {
        return make_error<std::uint32_t>("Host handle is missing native identity.");
    }
    const void* native_ptr = host_object.instance.get();
    for (std::uint32_t index = 0; index < host_handles_.size(); ++index) {
        const HostHandleEntry& entry = host_handles_[index];
        if (entry.host_class.get() != host_object.host_class.get()) {
            continue;
        }
        const auto instance = entry.residency_owner ? entry.residency_owner : entry.instance.lock();
        if (instance.get() != nullptr && instance.get() == native_ptr) {
            return index;
        }
    }
    return make_error<std::uint32_t>("Host handle slot not found.");
}

HostHandleToken Runtime::register_host_handle(const ZephyrHostObjectRef& host_object) {
    auto apply_metadata = [&](HostHandleEntry& entry, std::uint32_t slot) {
        entry.kind = host_object.kind;
        entry.lifetime = host_object.lifetime;
        entry.policy = host_object.has_explicit_policy ? host_object.policy : default_policy_for_kind(host_object.kind);
        if (entry.lifetime == ZephyrHostHandleLifetime::Stable && config_.handle.stable_handles_require_guid &&
            !host_object.stable_guid.valid()) {
            fail("Stable host handles require a valid GUID in this VM configuration.");
        }
        if (entry.lifetime == ZephyrHostHandleLifetime::Stable) {
            entry.policy.allow_serialize = true;
            entry.policy.allow_cross_scene = true;
        }
        entry.flags = 0;
        if (host_object.strong_residency && entry.policy.strong_residency_allowed) {
            entry.flags |= HostHandleStrongResidencyBit;
            entry.residency_owner = host_object.instance;
        } else {
            entry.residency_owner.reset();
        }
        if (entry.policy.allow_serialize || entry.lifetime == ZephyrHostHandleLifetime::Stable) {
            entry.flags |= HostHandleSerializableBit;
        }
        if (!host_object.valid) {
            entry.flags |= HostHandleInvalidBit;
            entry.invalid_reason = host_object.invalid_reason.empty() ? std::string("Host handle registered as invalid.")
                                                                      : host_object.invalid_reason;
        } else {
            entry.invalid_reason.clear();
        }
        entry.host_class = host_object.host_class;
        entry.instance = host_object.instance;
        entry.cached_instance = host_object.instance.get();
        entry.frame_epoch = current_frame_epoch_;
        entry.tick_epoch = current_tick_epoch_;
        entry.scene_epoch = current_scene_epoch_;
        entry.runtime_slot = slot;
        entry.stable_guid = host_object.stable_guid;
        if (entry.stable_guid.valid()) {
            stable_handle_lookup_[entry.stable_guid] = slot;
        }
    };

    if (host_object.slot < host_handles_.size()) {
        HostHandleEntry& entry = host_handles_[host_object.slot];
        if (host_object.generation == 0 || host_object.generation == entry.generation) {
            apply_metadata(entry, host_object.slot);
            return HostHandleToken{host_object.slot, entry.generation};
        }
    }

    auto existing_slot = find_host_handle_slot(host_object);
    if (existing_slot) {
        HostHandleEntry& entry = host_handles_[*existing_slot];
        if (host_object.slot >= host_handles_.size() && host_object.generation == 0) {
            return HostHandleToken{*existing_slot, entry.generation};
        }
        if (host_object.generation != 0) {
            entry.generation = host_object.generation;
        }
        apply_metadata(entry, *existing_slot);
        return HostHandleToken{*existing_slot, entry.generation};
    }

    HostHandleEntry entry;
    entry.generation = host_object.generation != 0 ? host_object.generation : 1;
    const std::uint32_t slot = static_cast<std::uint32_t>(host_handles_.size());
    // Assign stable handle_id for handle-table indirection (Item 7)
    entry.handle_id = next_handle_id_++;
    apply_metadata(entry, slot);
    host_handles_.push_back(std::move(entry));
    return HostHandleToken{slot, host_handles_.back().generation};
}

void Runtime::invalidate_host_handle_slot(std::uint32_t slot, const std::string& reason) {
    if (slot >= host_handles_.size()) {
        return;
    }
    HostHandleEntry& entry = host_handles_[slot];
    entry.flags |= HostHandleInvalidBit;
    entry.invalid_reason = reason;
    entry.residency_owner.reset();
    entry.cached_instance = nullptr;
    ++entry.generation;
    if (entry.generation == 0) {
        entry.generation = 1;
    }
}

void Runtime::invalidate_host_handle(const ZephyrHostObjectRef& handle) {
    if (handle.slot < host_handles_.size()) {
        invalidate_host_handle_slot(handle.slot, handle.invalid_reason.empty() ? "Host handle invalidated by host."
                                                                               : handle.invalid_reason);
        return;
    }
    auto slot = find_host_handle_slot(handle);
    if (slot) {
        invalidate_host_handle_slot(*slot, handle.invalid_reason.empty() ? "Host handle invalidated by host."
                                                                         : handle.invalid_reason);
    }
}

RuntimeResult<HostHandleEntry*> Runtime::lookup_host_handle_entry(HostHandleToken token, const Span& span, const std::string& module_name,
                                                                  std::string_view action) {
    ++handle_resolve_count_;
    auto invalid = [&](const std::string& message) -> RuntimeResult<HostHandleEntry*> {
        ++invalid_handle_faults_;
        ++handle_resolve_failures_;
        if (token.slot < host_handles_.size() && host_handles_[token.slot].stable_guid.valid()) {
            ++stable_resolve_misses_;
        }
        if (token.slot < host_handles_.size() && effective_invalid_access_mode(host_handles_[token.slot]) == ZephyrInvalidAccessMode::Trap) {
            trap_invalid_handle(message);
        }
        return make_loc_error<HostHandleEntry*>(module_name, span, message);
    };

    if (!token.valid() || token.slot >= host_handles_.size()) {
        return invalid("Invalid host handle during " + std::string(action) + ".");
    }

    HostHandleEntry& entry = host_handles_[token.slot];
    if (entry.generation != token.generation) {
        const std::string reason = entry.invalid_reason.empty() ? std::string("stale generation") : entry.invalid_reason;
        return invalid("Invalid host handle during " + std::string(action) + ": " + reason + ".");
    }
    if (entry.invalid()) {
        return invalid("Invalid host handle during " + std::string(action) + ": " +
                       (entry.invalid_reason.empty() ? std::string("handle was invalidated") : entry.invalid_reason) + ".");
    }
    if (entry.lifetime == ZephyrHostHandleLifetime::Frame && entry.frame_epoch != current_frame_epoch_) {
        return invalid("Frame host handle escaped its frame during " + std::string(action) + ".");
    }
    if (entry.lifetime == ZephyrHostHandleLifetime::Tick && entry.tick_epoch != current_tick_epoch_) {
        return invalid("Tick host handle escaped its tick during " + std::string(action) + ".");
    }
    if (entry.scene_epoch != current_scene_epoch_ && !handle_can_cross_scene(entry.lifetime, entry.policy)) {
        return invalid("Host handle crossed scene boundary during " + std::string(action) + ".");
    }
    if (entry.stable_guid.valid()) {
        ++stable_resolve_hits_;
    }
    return &entry;
}

RuntimeResult<HostHandleResolution> Runtime::resolve_host_handle(const Value& value, const Span& span, const std::string& module_name,
                                                                 std::string_view action) {
    if (!value.is_host_handle()) {
        return make_loc_error<HostHandleResolution>(module_name, span, "Expected host handle for " + std::string(action) + ".");
    }
    ZEPHYR_TRY_ASSIGN(entry, lookup_host_handle_entry(value.as_host_handle(), span, module_name, action));
    HostHandleResolution resolution;
    resolution.entry = entry;
    if (entry->residency_owner) {
        resolution.instance = entry->cached_instance;
    } else if (entry->cached_instance != nullptr && !entry->instance.expired()) {
        resolution.instance = entry->cached_instance;
    } else {
        resolution.keep_alive = entry->instance.lock();
        resolution.instance = resolution.keep_alive.get();
    }
    if (resolution.instance == nullptr) {
        ++invalid_handle_faults_;
        ++handle_resolve_failures_;
        const std::string message =
            "Invalid host handle during " + std::string(action) + ": " +
            (entry->invalid_reason.empty() ? std::string("native object expired") : entry->invalid_reason) + ".";
        if (effective_invalid_access_mode(*entry) == ZephyrInvalidAccessMode::Trap) {
            trap_invalid_handle(message);
        }
        return make_loc_error<HostHandleResolution>(module_name, span, message);
    }
    return resolution;
}

Runtime::PublicArgsBufferLease Runtime::acquire_public_args_buffer(std::size_t required_capacity) {
    std::size_t index = 0;
    if (!free_public_args_buffer_indices_.empty()) {
        index = free_public_args_buffer_indices_.back();
        free_public_args_buffer_indices_.pop_back();
    } else {
        index = public_args_buffers_.size();
        public_args_buffers_.emplace_back();
    }

    auto& buffer = public_args_buffers_[index];
    buffer.clear();
    if (buffer.capacity() < required_capacity) {
        buffer.reserve(required_capacity);
    }
    return PublicArgsBufferLease(*this, index);
}

std::vector<ZephyrValue>& Runtime::PublicArgsBufferLease::args() const { return runtime->public_args_buffers_[index]; }

void Runtime::PublicArgsBufferLease::release() {
    if (runtime == nullptr) {
        return;
    }
    runtime->free_public_args_buffer_indices_.push_back(index);
    runtime = nullptr;
}

void Runtime::set_breakpoint(const ZephyrBreakpoint& bp) {
    breakpoints_.push_back(bp);
    dap_active_ = !breakpoints_.empty();
}

void Runtime::clear_breakpoints() {
    breakpoints_.clear();
    dap_active_ = false;
    last_breakpoint_ip_ = 0;
    last_breakpoint_line_ = 0;
    last_breakpoint_module_.clear();
}

void Runtime::check_breakpoint(std::size_t ip, std::uint32_t current_line, const std::string& module_name) {
    for (const auto& breakpoint : breakpoints_) {
        if (breakpoint.line == current_line && (breakpoint.source_file.empty() || breakpoint.source_file == module_name)) {
            last_breakpoint_ip_ = ip;
            last_breakpoint_line_ = current_line;
            last_breakpoint_module_ = module_name;
            return;
        }
    }
}

VoidResult Runtime::validate_handle_store(const Value& value, HandleContainerKind container, const Span& span,
                                          const std::string& module_name, const std::string& context) {
    if (!value.is_host_handle()) {
        return ok_result();
    }
    ZEPHYR_TRY_ASSIGN(entry, lookup_host_handle_entry(value.as_host_handle(), span, module_name, context));
    if (!handle_store_allowed(entry->lifetime, entry->policy, container)) {
        return make_loc_error<std::monostate>(
            module_name,
            span,
            "Host handle lifetime '" +
                std::string(entry->lifetime == ZephyrHostHandleLifetime::Frame
                                ? "Frame"
                                : entry->lifetime == ZephyrHostHandleLifetime::Tick
                                      ? "Tick"
                                      : entry->lifetime == ZephyrHostHandleLifetime::Persistent ? "Persistent" : "Stable") +
                "' cannot be used in " + context + ".");
    }
    return ok_result();
}

VoidResult Runtime::validate_closure_capture(Environment* environment, const Span& span, const std::string& module_name) {
    return ensure_capture_cells(environment, HandleContainerKind::ClosureCapture, span, module_name);
}

// Young-collection sweep: single O(n) pass over the nursery_ linked list.
// Phase 5C policy: ALL surviving young objects are promoted unconditionally.
// Aging-in-place (age < threshold) is removed because NurserySpace bump
// allocation reclaims chunks wholesale after young collection — any object
// that stays in the nursery would be overwritten on the next chunk reset.
// After this function returns, nursery_.objects_ is always nullptr, making
// bump-pointer chunk reset safe and predictable.
void Runtime::sweep_young_objects() {
    // Phase 5C sweep: single O(n) pass over the nursery_ linked list.
    // ALL surviving young objects are promoted unconditionally.
    // Bump-allocated survivors stay in their chunk memory — the chunk is pinned
    // (retained) via note_promoted() so reset_chunks() won't recycle it.
    // No address change, no forwarding map, no reference patching needed.

    GcObject* current  = nursery_.objects_;
    GcObject* previous = nullptr;
    while (current != nullptr) {
        GcObject* next = current->header.next_all;
        if (current->header.color == GcColor::White) {
            // Dead young object — unlink from the nursery list.
            if (previous != nullptr) {
                previous->header.next_all = next;
            } else {
                nursery_.objects_ = next;
            }
            nursery_.live_bytes_own_ -= current->header.size_bytes;
            live_bytes_              -= current->header.size_bytes;
            if ((current->header.flags & GcFinalizableBit) != 0) {
                current->header.next_all = nullptr;
                detach_queue_.push_back(current);
            } else if ((current->header.flags & GcBumpAllocBit) != 0) {
                if (current->kind == ObjectKind::Coroutine) {
                    record_coroutine_destroyed(static_cast<CoroutineObject*>(current));
                }
                // bump object — destructor only; chunk memory is reclaimed
                // wholesale by reset_chunks() (if no promoted objects pin it).
                current->~GcObject();
            } else {
                if (current->kind == ObjectKind::Coroutine) {
                    record_coroutine_destroyed(static_cast<CoroutineObject*>(current));
                }
                delete current;
            }
            // previous stays: it's still the last surviving nursery node.
        } else {
            // Surviving young object — promote unconditionally.
            // O(1) unlink: we already have `previous`, so unlink directly
            // instead of letting promote_object() call nursery_.remove() which
            // would do a redundant O(n) list walk.
            if (previous != nullptr) {
                previous->header.next_all = next;
            } else {
                nursery_.objects_ = next;
            }
            nursery_.live_bytes_own_ -= current->header.size_bytes;
            // For bump objects, note_promoted() pins the chunk so reset_chunks()
            // won't recycle it.  The object's address doesn't change.
            if ((current->header.flags & GcBumpAllocBit) != 0) {
                nursery_.note_promoted(current);
            }
            promote_object(current);
            // `previous` remains valid (it was not promoted, just unlinked current).
        }
        current = next;
    }

    // Note: live_bytes_ and nursery_.live_bytes_own_ were already decremented
    // for each finalizable object before it was pushed to detach_queue_ above,
    // so we do NOT decrement again here.
    while (!detach_queue_.empty()) {
        GcObject* object = detach_queue_.back();
        detach_queue_.pop_back();
        if (object->kind == ObjectKind::Coroutine) {
            record_coroutine_destroyed(static_cast<CoroutineObject*>(object));
        }
        // Phase 5C: bump-allocated finalizable objects must use destructor only.
        if ((object->header.flags & GcBumpAllocBit) != 0) {
            object->~GcObject();
        } else {
            delete object;
        }
    }

    // Phase 5C: all nursery objects have been promoted or freed.
    // Reset the bump-pointer chunks so their memory can be reused in the next
    // nursery cycle without returning pages to the OS.
    nursery_.reset_chunks();
}

void Runtime::perform_young_collection() {
    const std::size_t heap_bytes_before = live_bytes_;
    record_gc_trace_event(GCTraceEvent::Type::YoungStart, heap_bytes_before, 0);
    const auto pause_start = std::chrono::high_resolution_clock::now();
    gray_stack_.clear();
    dirty_root_environments_.clear();
    dirty_objects_.clear();
    detach_queue_.clear();
    // Phase 5B: only reset nursery objects. Old objects (old_small_, los_, pinned_)
    // are never colored during young collection — mark_young_object() skips them —
    // so resetting them wastes work. nursery_.for_each_object covers all young objects.
    nursery_.for_each_object([](GcObject* object) {
        object->header.color = GcColor::White;
        object->header.next_gray = nullptr;
    });

    ++total_gc_cycles_;
    ++total_minor_gc_cycles_;
    ++total_young_collections_;
    gc_collection_kind_ = GcCollectionKind::Young;
    // Phase 3.5: snapshot nursery bytes before sweep for survival-rate computation.
    nursery_young_bytes_before_  = nursery_.live_bytes_own_;
    young_promoted_bytes_this_cycle_ = 0;
    visit_root_references(
        [this](GcObject* object) {
            if (object == nullptr) {
                return;
            }
            if (is_old_object(object)) {
                trace_young_references(object);
            } else {
                mark_young_object(object);
            }
        },
        [this](const Value& value) { mark_young_root_value(value); });
    for (auto* object : remembered_objects_) {
        if (object == nullptr || !is_old_object(object)) {
            continue;
        }
        if (owner_is_fully_card_tracked(object) && !owner_has_dirty_minor_cards(object)) {
            continue;
        }
        trace_young_references(object);
    }
    while (!gray_stack_.empty()) {
        GcObject* object = gray_stack_.back();
        gray_stack_.pop_back();
        if (object == nullptr || is_old_object(object) || object->header.color != GcColor::Gray) {
            continue;
        }
        trace_young_references(object);
        object->header.color = GcColor::Black;
    }
    sweep_young_objects();
    compact_minor_remembered_set();
    gray_stack_.clear();
    dirty_root_environments_.clear();
    dirty_objects_.clear();
    young_allocation_pressure_bytes_ = 0;
    gc_young_cycle_requested_ = false;
    gc_collection_kind_ = GcCollectionKind::Full;

    // Phase 3.5: Adaptive nursery trigger adjustment.
    // Survival rate > 50% → too many objects tenuring → grow trigger (max 1MB).
    // Survival rate < 10% → lots of short-lived objects → shrink trigger (min 64KB).
    if (config_.gc.adaptive_nursery && nursery_young_bytes_before_ > 0) {
        constexpr std::size_t kAdaptiveNurseryMin = 64  * 1024;
        constexpr std::size_t kAdaptiveNurseryMax = 1024 * 1024;
        const std::size_t survived = young_promoted_bytes_this_cycle_;
        const std::size_t total    = nursery_young_bytes_before_;
        if (survived * 2 > total) {
            // >50% survived — increase trigger to reduce GC frequency
            nursery_trigger_bytes_ = std::min(nursery_trigger_bytes_ * 2, kAdaptiveNurseryMax);
        } else if (survived * 10 < total) {
            // <10% survived — decrease trigger to reclaim memory sooner
            nursery_trigger_bytes_ = std::max(nursery_trigger_bytes_ / 2, kAdaptiveNurseryMin);
        }
    }
    const auto pause_end = std::chrono::high_resolution_clock::now();
    const auto duration_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(pause_end - pause_start).count());
    record_gc_pause(duration_ns, false);
    record_gc_trace_event(GCTraceEvent::Type::YoungEnd, heap_bytes_before, live_bytes_);
}

void Runtime::collect_young() {
    if (gc_phase_ != ZephyrGcPhase::Idle || gc_cycle_requested_) {
        collect_garbage();
    }
    perform_young_collection();
}

VoidResult Runtime::gc_verify_young() {
    collect_young();
    ++total_gc_verifications_;
    if (gc_phase_ != ZephyrGcPhase::Idle) {
        return make_error<std::monostate>("Young GC verification expected idle collector state.");
    }
    if (!gray_stack_.empty() || !dirty_root_environments_.empty() || !dirty_objects_.empty() || !detach_queue_.empty()) {
        return make_error<std::monostate>("Young GC verification found stale collector queues.");
    }
    std::unordered_set<GcObject*> seen;
    for (auto* object : remembered_objects_) {
        if (object == nullptr) {
            return make_error<std::monostate>("Young GC verification found a null remembered owner.");
        }
        if (!seen.insert(object).second) {
            return make_error<std::monostate>("Young GC verification found a duplicate remembered owner.");
        }
        if (!is_old_object(object)) {
            return make_error<std::monostate>("Young GC verification found a non-old remembered owner.");
        }
        if ((object->header.flags & GcMinorRememberedBit) == 0) {
            return make_error<std::monostate>("Young GC verification found a remembered owner without its bit set.");
        }
        const bool keep = owner_is_fully_card_tracked(object) ? owner_has_dirty_minor_cards(object) : has_direct_young_reference(object);
        if (!keep) {
            return make_error<std::monostate>("Young GC verification found a stale remembered owner of kind '" +
                                              std::string(object_kind_name(object->kind)) + "'.");
        }
    }
    // Per-object card-metadata consistency check.
    // Uses an error-flag pattern so the loop body can use 'return' for early-exit
    // while the outer for_each_object iterates to completion.
    std::string verify_error;
    for (auto* space : all_spaces_) {
        if (!verify_error.empty()) {
            break;
        }
        space->for_each_object([&](GcObject* object) {
            if (!verify_error.empty()) {
                return;
            }
            if (object->kind == ObjectKind::Environment) {
                const auto* environment = static_cast<const Environment*>(object);
                const bool needs_card_metadata = environment->kind == EnvironmentKind::Local &&
                                                 (!environment->remembered_cards.empty() || has_direct_young_reference(environment));
                if (is_old_object(environment) && needs_card_metadata &&
                    environment->remembered_cards.size() != value_card_count(environment->binding_names.size())) {
                    verify_error = "Young GC verification found an environment with mismatched remembered-card capacity.";
                }
                return;
            }
            if (object->kind == ObjectKind::Array) {
                const auto* array = static_cast<const ArrayObject*>(object);
                const bool needs_card_metadata = !array->remembered_cards.empty() || has_direct_young_reference(array);
                if (is_old_object(array) && needs_card_metadata &&
                    array->remembered_cards.size() != value_card_count(array->elements.size())) {
                    verify_error = "Young GC verification found an array with mismatched remembered-card capacity.";
                }
                return;
            }
            if (object->kind == ObjectKind::StructInstance) {
                const auto* instance = static_cast<const StructInstanceObject*>(object);
                const bool needs_card_metadata = !instance->remembered_cards.empty() || has_direct_young_reference(instance);
                if (is_old_object(instance) && needs_card_metadata &&
                    instance->remembered_cards.size() != value_card_count(instance->field_values.size())) {
                    verify_error = "Young GC verification found a struct instance with mismatched remembered-card capacity.";
                }
                return;
            }
            if (object->kind == ObjectKind::EnumInstance) {
                const auto* instance = static_cast<const EnumInstanceObject*>(object);
                const bool needs_card_metadata = !instance->remembered_cards.empty() || has_direct_young_reference(instance);
                if (is_old_object(instance) && needs_card_metadata &&
                    instance->remembered_cards.size() != value_card_count(instance->payload.size())) {
                    verify_error = "Young GC verification found an enum instance with mismatched remembered-card capacity.";
                }
                return;
            }
            if (object->kind != ObjectKind::Coroutine) {
                return;
            }
            const auto* coroutine = static_cast<const CoroutineObject*>(object);
            if (!coroutine->suspended) {
                return;
            }
            for (const auto& frame : coroutine->frames) {
                if (frame.stack_cards.size() != value_card_count(frame.stack.size())) {
                    verify_error = "Young GC verification found a coroutine stack with mismatched remembered-card capacity.";
                    return;
                }
                if (frame.local_cards.size() != value_card_count(frame.locals.size())) {
                    verify_error = "Young GC verification found a coroutine local set with mismatched remembered-card capacity.";
                    return;
                }
                if (frame.reg_cards.size() != value_card_count(frame.regs.size())) {
                    verify_error = "Young GC verification found a coroutine register set with mismatched remembered-card capacity.";
                    return;
                }
            }
        });
    }
    if (!verify_error.empty()) {
        return make_error<std::monostate>(verify_error);
    }
    return ok_result();
}

void Runtime::gc_step(std::size_t budget_work) {
    if (budget_work == 0) {
        budget_work = 1;
    }
    ++total_gc_steps_;

    if (gc_phase_ == ZephyrGcPhase::Idle && gc_young_cycle_requested_ && !gc_cycle_requested_) {
        perform_young_collection();
    }

    if (gc_phase_ == ZephyrGcPhase::Idle && gc_cycle_requested_) {
        gc_phase_ = ZephyrGcPhase::PrepareCycle;
    }

    while (budget_work > 0) {
        switch (gc_phase_) {
            case ZephyrGcPhase::Idle:
                return;
            case ZephyrGcPhase::PrepareCycle:
                begin_gc_cycle();
                --budget_work;
                break;
            case ZephyrGcPhase::SeedRoots:
                seed_roots();
                --budget_work;
                break;
            case ZephyrGcPhase::DrainGray:
                drain_gray(budget_work);
                break;
            case ZephyrGcPhase::RescanDirtyRoots:
                process_dirty_roots();
                --budget_work;
                break;
            case ZephyrGcPhase::SweepObjects:
                sweep(budget_work);
                break;
            case ZephyrGcPhase::DetachQueue:
                process_detach_queue(budget_work);
                break;
            case ZephyrGcPhase::Complete:
                rebuild_minor_remembered_set();
                allocation_pressure_bytes_ = 0;
                young_allocation_pressure_bytes_ = 0;
                gc_cycle_requested_ = false;
                gc_young_cycle_requested_ = false;
                if (full_gc_pause_active_) {
                    const auto pause_end = ProfileClock::now();
                    const auto duration_ns = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(pause_end - full_gc_pause_start_).count());
                    record_gc_pause(duration_ns, true);
                    record_gc_trace_event(GCTraceEvent::Type::FullEnd, full_gc_heap_before_, live_bytes_);
                    full_gc_pause_active_ = false;
                    full_gc_heap_before_ = 0;
                }
                gc_phase_ = ZephyrGcPhase::Idle;
                assert(accounting_consistent());
                --budget_work;
                break;
        }
    }
}

void Runtime::set_gc_stress(bool enabled, std::size_t budget_work) {
    gc_stress_enabled_ = enabled;
    gc_stress_budget_work_ = std::max<std::size_t>(budget_work, 1);
}

void Runtime::advance_frame(std::size_t gc_budget_work) {
    ++current_frame_epoch_;
    gc_step(gc_budget_work);
}

void Runtime::advance_tick(std::size_t gc_budget_work) {
    ++current_tick_epoch_;
    gc_step(gc_budget_work);
}

void Runtime::advance_scene() {
    ++current_scene_epoch_;
    request_gc_cycle();
    collect_garbage();
}

ZephyrVM::RuntimeStats Runtime::runtime_stats() const {
    ZephyrVM::RuntimeStats stats;
    stats.frame_epoch = current_frame_epoch_;
    stats.tick_epoch = current_tick_epoch_;
    stats.scene_epoch = current_scene_epoch_;
    stats.gc.phase = gc_phase_;
    stats.gc.live_bytes = live_bytes_;
    stats.gc.gray_objects = gray_stack_.size();
    stats.gc.dirty_root_environments = dirty_root_environments_.size();
    stats.gc.dirty_objects = dirty_objects_.size();
    stats.gc.detach_queue_objects = detach_queue_.size();
    stats.gc.total_allocations = total_allocations_;
    stats.gc.total_gc_steps = total_gc_steps_;
    stats.gc.total_gc_cycles = total_gc_cycles_;
    stats.gc.total_minor_gc_cycles = total_minor_gc_cycles_;
    stats.gc.total_major_gc_cycles = total_major_gc_cycles_;
    stats.gc.total_promotions = total_promotions_;
    stats.gc.total_compactions = total_compactions_;
    stats.gc.total_young_collections = total_young_collections_;
    stats.gc.total_full_collections = total_full_collections_;
    stats.gc.total_gc_verifications = total_gc_verifications_;
    stats.gc.total_gc_stress_safe_points = total_gc_stress_safe_points_;
    stats.gc.barrier_hits = barrier_hits_;
    stats.gc.gc_stress_enabled = gc_stress_enabled_;
    stats.gc.gc_stress_budget = gc_stress_budget_work_;
    stats.gc.remembered_objects = remembered_objects_.size();
    stats.gc.remembered_cards = count_remembered_cards();
    stats.gc.remembered_card_fast_prunes = total_remembered_card_fast_prunes_;

    stats.vm.opcode_count = opcode_execution_count_;
    stats.vm.ast_fallback_executions = ast_fallback_executions_;
    stats.vm.lightweight_calls = lightweight_calls_;
    stats.vm.string_intern_hits = string_intern_hits_;
    stats.vm.string_intern_misses = string_intern_misses_;
    stats.vm.interned_string_count = interned_strings_.size();
    stats.vm.local_binding_cache_hits = local_binding_cache_hits_;
    stats.vm.local_binding_cache_misses = local_binding_cache_misses_;
    stats.vm.global_binding_cache_hits = global_binding_cache_hits_;
    stats.vm.global_binding_cache_misses = global_binding_cache_misses_;
    stats.vm.callback_count = retained_callbacks_.size();
    stats.vm.callback_invocations = callback_invocations_;
    stats.vm.serialized_value_exports = serialized_value_exports_;
    stats.vm.deserialized_value_imports = deserialized_value_imports_;
    stats.vm.module_count = modules_.size();

    stats.coroutine.active_coroutines = active_coroutines_.size();
    stats.coroutine.coroutine_compactions = coroutine_compactions_;
    stats.coroutine.coroutine_compacted_frames = coroutine_compacted_frames_;
    stats.coroutine.coroutine_compacted_capacity = coroutine_compacted_capacity_;

    stats.handle.invalid_handle_faults = invalid_handle_faults_;
    stats.handle.host_handle_slots = host_handles_.size();
    stats.handle.resolve_count = handle_resolve_count_;
    stats.handle.resolve_failures = handle_resolve_failures_;
    stats.handle.stable_resolve_hits = stable_resolve_hits_;
    stats.handle.stable_resolve_misses = stable_resolve_misses_;

    std::unordered_set<const BytecodeFunction*> seen_chunks;
    auto record_chunk = [&](const std::shared_ptr<BytecodeFunction>& chunk) {
        if (!chunk || !seen_chunks.insert(chunk.get()).second) {
            return;
        }
        stats.vm.line_table_entries += chunk->line_table.size();
        stats.vm.constant_pool_entries += chunk->constants.size();
        stats.vm.superinstruction_fusions += chunk->superinstruction_fusions;
        stats.vm.total_original_opcode_count +=
            chunk->total_original_opcode_count > 0 ? chunk->total_original_opcode_count : chunk->instructions.size();
    };

    // Populate per-space live_bytes from each space's independent counter.
    stats.gc.nursery.live_bytes      = nursery_.live_bytes();
    stats.gc.old_small.live_bytes    = old_small_.live_bytes();
    stats.gc.large_object.live_bytes = los_.live_bytes();
    stats.gc.pinned.live_bytes       = pinned_.live_bytes();

    // Phase 4C: OldSmallSpace slab used/reserved bytes.
    // used_bytes = sum of (live_slots * slot_size) across all active spans.
    // reserved_bytes = sum of committed 4096-byte pages across all spans
    //                  (partial + full + empty warm pool).
    stats.gc.old_small.used_bytes     = old_small_.slab_used_bytes();
    stats.gc.old_small.reserved_bytes = old_small_.slab_reserved_bytes();

    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            ++stats.gc.live_objects;
            // Increment per-space object count.
            switch (object->header.space_kind) {
                case GcSpaceKind::Nursery:      ++stats.gc.nursery.object_count;      break;
                case GcSpaceKind::OldSmall:     ++stats.gc.old_small.object_count;    break;
                case GcSpaceKind::LargeObject:  ++stats.gc.large_object.object_count; break;
                case GcSpaceKind::Pinned:       ++stats.gc.pinned.object_count;       break;
                default: break;
            }
            if (is_old_object(object)) {
                ++stats.gc.old_objects;
                stats.gc.old_bytes += object->header.size_bytes;
            } else {
                ++stats.gc.young_objects;
                stats.gc.young_bytes += object->header.size_bytes;
            }
            if (is_large_object(object)) {
                ++stats.gc.large_objects;
                stats.gc.large_object_bytes += object->header.size_bytes;
            }
            if (object->kind == ObjectKind::ScriptFunction) {
                ++stats.vm.function_count;
                record_chunk(static_cast<const ScriptFunctionObject*>(object)->bytecode);
            }
            if (object->kind == ObjectKind::NativeFunction) {
                ++stats.vm.function_count;
            }
            if (object->kind != ObjectKind::Coroutine) {
                return;
            }
            ++stats.coroutine.coroutine_objects;
            const auto* coroutine = static_cast<const CoroutineObject*>(object);
            if (coroutine->suspended && !coroutine->completed) {
                ++stats.coroutine.suspended_coroutines;
            }
            if (coroutine->completed) {
                ++stats.coroutine.completed_coroutines;
            }
            stats.coroutine.total_coroutine_resume_calls += coroutine->resume_count;
            stats.coroutine.total_coroutine_yields += coroutine->yield_count;
            stats.coroutine.total_coroutine_steps += coroutine->total_step_count;
            stats.coroutine.max_coroutine_steps = std::max(stats.coroutine.max_coroutine_steps, coroutine->total_step_count);
            stats.coroutine.max_coroutine_resume_steps =
                std::max(stats.coroutine.max_coroutine_resume_steps, coroutine->max_resume_step_count);
            stats.coroutine.total_coroutine_frames += coroutine->frames.size();
            stats.coroutine.max_coroutine_frame_depth =
                std::max(stats.coroutine.max_coroutine_frame_depth, coroutine->frames.size());
            for (const auto& frame : coroutine->frames) {
                stats.coroutine.total_coroutine_stack_values += frame.stack.size();
                stats.coroutine.total_coroutine_stack_capacity += frame.stack.capacity();
                stats.coroutine.total_coroutine_local_slots += frame.locals.size() + frame.regs.size();
                stats.coroutine.total_coroutine_local_capacity += frame.locals.capacity() + frame.regs.capacity();
                stats.coroutine.max_coroutine_stack_values =
                    std::max(stats.coroutine.max_coroutine_stack_values, frame.stack.size());
                stats.coroutine.max_coroutine_local_slots =
                    std::max(stats.coroutine.max_coroutine_local_slots, frame.locals.size() + frame.regs.size());
            }
        });
    }

    for (const auto& [module_name, module] : modules_) {
        (void)module_name;
        record_chunk(module.bytecode);
    }

    for (const auto& entry : host_handles_) {
        if ((entry.flags & HostHandleStrongResidencyBit) != 0) {
            ++stats.handle.strong_residency_handles;
        }
        if (entry.stable_guid.valid()) {
            ++stats.handle.stable_handles;
        }
        switch (entry.lifetime) {
            case ZephyrHostHandleLifetime::Frame: ++stats.handle.frame_handles; break;
            case ZephyrHostHandleLifetime::Tick: ++stats.handle.tick_handles; break;
            case ZephyrHostHandleLifetime::Persistent: ++stats.handle.persistent_handles; break;
            case ZephyrHostHandleLifetime::Stable: ++stats.handle.stable_handles; break;
        }
    }

    stats.gc_phase = stats.gc.phase;
    stats.live_objects = stats.gc.live_objects;
    stats.live_bytes = stats.gc.live_bytes;
    stats.gray_objects = stats.gc.gray_objects;
    stats.dirty_root_environments = stats.gc.dirty_root_environments;
    stats.dirty_objects = stats.gc.dirty_objects;
    stats.detach_queue_objects = stats.gc.detach_queue_objects;
    stats.remembered_objects = stats.gc.remembered_objects;
    stats.total_allocations = stats.gc.total_allocations;
    stats.total_gc_steps = stats.gc.total_gc_steps;
    stats.total_gc_cycles = stats.gc.total_gc_cycles;
    stats.total_gc_verifications = stats.gc.total_gc_verifications;
    stats.total_gc_stress_safe_points = stats.gc.total_gc_stress_safe_points;
    stats.barrier_hits = stats.gc.barrier_hits;
    stats.invalid_handle_faults = stats.handle.invalid_handle_faults;
    stats.host_handle_slots = stats.handle.host_handle_slots;
    stats.coroutine_objects = stats.coroutine.coroutine_objects;
    stats.suspended_coroutines = stats.coroutine.suspended_coroutines;
    stats.completed_coroutines = stats.coroutine.completed_coroutines;
    stats.active_coroutines = stats.coroutine.active_coroutines;
    stats.total_coroutine_frames = stats.coroutine.total_coroutine_frames;
    stats.max_coroutine_frame_depth = stats.coroutine.max_coroutine_frame_depth;
    stats.total_coroutine_stack_values = stats.coroutine.total_coroutine_stack_values;
    stats.total_coroutine_stack_capacity = stats.coroutine.total_coroutine_stack_capacity;
    stats.total_coroutine_local_slots = stats.coroutine.total_coroutine_local_slots;
    stats.total_coroutine_local_capacity = stats.coroutine.total_coroutine_local_capacity;
    stats.max_coroutine_stack_values = stats.coroutine.max_coroutine_stack_values;
    stats.max_coroutine_local_slots = stats.coroutine.max_coroutine_local_slots;
    stats.coroutine_compactions = stats.coroutine.coroutine_compactions;
    stats.coroutine_compacted_frames = stats.coroutine.coroutine_compacted_frames;
    stats.coroutine_compacted_capacity = stats.coroutine.coroutine_compacted_capacity;
    stats.total_coroutine_resume_calls = stats.coroutine.total_coroutine_resume_calls;
    stats.total_coroutine_yields = stats.coroutine.total_coroutine_yields;
    stats.total_coroutine_steps = stats.coroutine.total_coroutine_steps;
    stats.max_coroutine_steps = stats.coroutine.max_coroutine_steps;
    stats.max_coroutine_resume_steps = stats.coroutine.max_coroutine_resume_steps;
    stats.gc_stress_enabled = stats.gc.gc_stress_enabled;
    stats.gc_stress_budget = stats.gc.gc_stress_budget;
    return stats;
}

void Runtime::start_profiling() {
    profiling_active_ = true;
    profile_entries_.clear();
    profile_stack_.clear();
    coroutine_trace_events_.clear();
    coroutine_trace_active_ = true;
}

ZephyrProfileReport Runtime::stop_profiling() {
    while (profiling_active_ && !profile_stack_.empty()) {
        end_profile_scope();
    }
    profiling_active_ = false;

    ZephyrProfileReport report;
    report.entries.reserve(profile_entries_.size());
    for (const auto& [_, entry] : profile_entries_) {
        report.entries.push_back(entry);
    }
    std::sort(report.entries.begin(), report.entries.end(),
              [](const ZephyrProfileEntry& left, const ZephyrProfileEntry& right) {
                  if (left.self_ns != right.self_ns) {
                      return left.self_ns > right.self_ns;
                  }
                  return left.function_name < right.function_name;
              });
    report.coroutine_trace = coroutine_trace_events_;
    coroutine_trace_active_ = false;
    return report;
}

void Runtime::start_gc_trace() {
    gc_trace_events_.clear();
    gc_trace_active_ = true;
}

void Runtime::stop_gc_trace() {
    gc_trace_active_ = false;
}

bool Runtime::is_gc_trace_active() const {
    return gc_trace_active_;
}

std::string Runtime::get_gc_trace_json() const {
    auto type_name = [](GCTraceEvent::Type type) {
        switch (type) {
            case GCTraceEvent::Type::YoungStart: return "YoungStart";
            case GCTraceEvent::Type::YoungEnd: return "YoungEnd";
            case GCTraceEvent::Type::FullStart: return "FullStart";
            case GCTraceEvent::Type::FullEnd: return "FullEnd";
        }
        return "unknown";
    };

    std::ostringstream out;
    out << "[";
    for (std::size_t index = 0; index < gc_trace_events_.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        const auto& event = gc_trace_events_[index];
        out << "{\"type\":\"" << type_name(event.type) << "\",\"ts_ns\":" << event.timestamp_ns
            << ",\"heap_before\":" << event.heap_bytes_before;
        if (event.type == GCTraceEvent::Type::YoungEnd || event.type == GCTraceEvent::Type::FullEnd) {
            out << ",\"heap_after\":" << event.heap_bytes_after;
        }
        out << "}";
    }
    out << "]";
    return out.str();
}

void Runtime::start_coroutine_trace() {
    coroutine_trace_events_.clear();
    coroutine_trace_active_ = true;
}

void Runtime::stop_coroutine_trace() {
    coroutine_trace_active_ = false;
}

void Runtime::enable_bytecode_cache(bool enabled) {
    bytecode_cache_enabled_ = enabled;
}

void Runtime::clear_bytecode_cache() {
    bytecode_cache_.clear();
}

std::size_t Runtime::bytecode_cache_size() const {
    return bytecode_cache_.size();
}

void Runtime::add_module_search_path(const std::string& path) {
    if (path.empty()) {
        return;
    }
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(std::filesystem::path(path));
    if (std::find(module_search_paths_.begin(), module_search_paths_.end(), candidate) == module_search_paths_.end()) {
        module_search_paths_.push_back(candidate);
    }
}

std::vector<std::string> Runtime::get_module_search_paths() const {
    std::vector<std::string> paths;
    paths.reserve(module_search_paths_.size());
    for (const auto& path : module_search_paths_) {
        paths.push_back(path.string());
    }
    return paths;
}

void Runtime::clear_module_search_paths() {
    module_search_paths_.clear();
}

void Runtime::set_package_root(const std::string& path) {
    struct PackageConfig {
        std::string name = "package";
        std::string version = "0.1.0";
        std::string entry = "src/lib.zph";
    };

    auto parse_manifest_value = [](const std::string& line) -> std::string {
        const auto equal_index = line.find('=');
        if (equal_index == std::string::npos) {
            return {};
        }
        std::string value = line.substr(equal_index + 1);
        const auto start = value.find_first_not_of(" \t\"");
        const auto end = value.find_last_not_of(" \t\"\r\n");
        if (start == std::string::npos || end == std::string::npos) {
            return {};
        }
        return value.substr(start, end - start + 1);
    };

    PackageConfig config;
    const std::filesystem::path root = std::filesystem::weakly_canonical(std::filesystem::path(path));
    const std::filesystem::path manifest_path = root / "package.toml";
    if (std::filesystem::exists(manifest_path)) {
        std::ifstream input(manifest_path);
        std::string line;
        bool in_package_section = false;
        while (std::getline(input, line)) {
            if (line == "[package]") {
                in_package_section = true;
                continue;
            }
            if (!line.empty() && line.front() == '[') {
                in_package_section = false;
                continue;
            }
            if (!in_package_section) {
                continue;
            }

            if (line.rfind("name", 0) == 0) {
                if (const auto value = parse_manifest_value(line); !value.empty()) {
                    config.name = value;
                }
                continue;
            }
            if (line.rfind("version", 0) == 0) {
                if (const auto value = parse_manifest_value(line); !value.empty()) {
                    config.version = value;
                }
                continue;
            }
            if (line.rfind("entry", 0) == 0) {
                if (const auto value = parse_manifest_value(line); !value.empty()) {
                    config.entry = value;
                }
            }
        }
    }

    clear_module_search_paths();
    add_module_search_path(root.string());
    add_module_search_path((root / std::filesystem::path(config.entry).parent_path()).string());
}

void Runtime::begin_profile_scope(const std::string& function_name) {
    if (!profiling_active_) {
        return;
    }
    profile_stack_.push_back(ActiveProfileFrame{function_name, ProfileClock::now(), 0});
}

void Runtime::end_profile_scope() {
    if (!profiling_active_ || profile_stack_.empty()) {
        return;
    }

    const auto end_time = ProfileClock::now();
    ActiveProfileFrame frame = std::move(profile_stack_.back());
    profile_stack_.pop_back();
    const auto elapsed_ns =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - frame.start_time).count());
    const std::uint64_t self_ns = elapsed_ns >= frame.child_ns ? elapsed_ns - frame.child_ns : 0;

    auto& entry = profile_entries_[frame.function_name];
    entry.function_name = frame.function_name;
    ++entry.call_count;
    entry.total_ns += elapsed_ns;
    entry.self_ns += self_ns;

    if (!profile_stack_.empty()) {
        profile_stack_.back().child_ns += elapsed_ns;
    }
}

std::string Runtime::debug_dump_coroutines() const {
    std::ostringstream out;
    const auto stats = runtime_stats();
    out << "coroutines=" << stats.coroutine_objects
        << " suspended=" << stats.suspended_coroutines
        << " completed=" << stats.completed_coroutines
        << " active=" << stats.active_coroutines
        << " total_frames=" << stats.total_coroutine_frames
        << " max_depth=" << stats.max_coroutine_frame_depth
        << " stack_values=" << stats.total_coroutine_stack_values
        << " stack_capacity=" << stats.total_coroutine_stack_capacity
        << " local_slots=" << stats.total_coroutine_local_slots
        << " local_capacity=" << stats.total_coroutine_local_capacity
        << " compactions=" << stats.coroutine_compactions
        << " compacted_frames=" << stats.coroutine_compacted_frames
        << " compacted_capacity=" << stats.coroutine_compacted_capacity
        << " resumes=" << stats.total_coroutine_resume_calls
        << " yields=" << stats.total_coroutine_yields
        << " steps=" << stats.total_coroutine_steps
        << " max_steps=" << stats.max_coroutine_steps
        << " max_resume_steps=" << stats.max_coroutine_resume_steps
        << "\n";

    std::size_t index = 0;
    for (auto* space : all_spaces_) {
        space->for_each_object([&](GcObject* object) {
            if (object->kind != ObjectKind::Coroutine) {
                return;
            }
            const auto* coroutine = static_cast<const CoroutineObject*>(object);
            const auto depth = coroutine->frames.size();
            const auto& root = coroutine->frames.empty() ? CoroutineFrameState{} : coroutine->frames.front();
            const auto& top = coroutine->frames.empty() ? CoroutineFrameState{} : coroutine->frames.back();
            out << "#" << index++
                << " started=" << (coroutine->started ? "true" : "false")
                << " suspended=" << (coroutine->suspended ? "true" : "false")
                << " completed=" << (coroutine->completed ? "true" : "false")
                << " resumes=" << coroutine->resume_count
                << " yields=" << coroutine->yield_count
                << " total_steps=" << coroutine->total_step_count
                << " last_steps=" << coroutine->last_resume_step_count
                << " max_resume_steps=" << coroutine->max_resume_step_count
                << " frames=" << depth
                << " root_module=" << (root.module_name.empty() ? "<anonymous>" : root.module_name)
                << " top_module=" << (top.module_name.empty() ? "<anonymous>" : top.module_name)
                << " top_ip=" << (top.uses_register_mode ? top.ip_index : top.ip)
                << " stack_size=" << top.stack.size()
                << " local_slots=" << (top.locals.size() + top.regs.size())
                << "\n";
            for (std::size_t frame_index = 0; frame_index < coroutine->frames.size(); ++frame_index) {
                const auto& frame = coroutine->frames[frame_index];
                out << "  frame[" << frame_index << "]"
                    << " module=" << (frame.module_name.empty() ? "<anonymous>" : frame.module_name)
                    << " ip=" << (frame.uses_register_mode ? frame.ip_index : frame.ip)
                    << " stack=" << frame.stack.size() << "/" << frame.stack.capacity()
                    << " locals=" << frame.locals.size() << "/" << frame.locals.capacity()
                    << " regs=" << frame.regs.size() << "/" << frame.regs.capacity()
                    << " register_mode=" << (frame.uses_register_mode ? "true" : "false")
                    << " scopes=" << frame.scope_stack.size()
                    << "\n";
            }
        });
    }
    return out.str();
}

std::string Runtime::describe_bytecode_constant(const BytecodeConstant& constant) const {
    return describe_bytecode_constant_literal(constant);
}

std::string Runtime::dump_bytecode(const std::string& module_name, const std::string& function_name) const {
    std::function<void(const BytecodeFunction&, const std::string&, std::ostringstream&)> dump_chunk;
    auto is_superinstruction = [](BytecodeOp op) {
        switch (op) {
            case BytecodeOp::SIAddStoreLocal:
            case BytecodeOp::SILoadLocalLoadLocal:
            case BytecodeOp::SILoadLocalAdd:
            case BytecodeOp::SICmpJumpIfFalse:
            case BytecodeOp::SILoadLocalAddStoreLocal:
            case BytecodeOp::SILoadLocalConstAddStoreLocal:
            case BytecodeOp::SILoadLocalLocalConstModulo:
                return true;
            default:
                return false;
        }
    };
    auto is_register_instruction = [](BytecodeOp op) {
        switch (op) {
            case BytecodeOp::R_ADD:
            case BytecodeOp::R_SUB:
            case BytecodeOp::R_MUL:
            case BytecodeOp::R_DIV:
            case BytecodeOp::R_MOD:
            case BytecodeOp::R_LOAD_CONST:
            case BytecodeOp::R_LOAD_INT:
            case BytecodeOp::R_ADDI:
            case BytecodeOp::R_MODI:
            case BytecodeOp::R_ADDI_JUMP:
            case BytecodeOp::R_SI_CMPI_JUMP_FALSE:
            case BytecodeOp::R_LOAD_GLOBAL:
            case BytecodeOp::R_STORE_GLOBAL:
            case BytecodeOp::R_MOVE:
            case BytecodeOp::R_CALL:
            case BytecodeOp::R_LOAD_MEMBER:
            case BytecodeOp::R_STORE_MEMBER:
            case BytecodeOp::R_CALL_MEMBER:
            case BytecodeOp::R_BUILD_STRUCT:
            case BytecodeOp::R_BUILD_ARRAY:
            case BytecodeOp::R_LOAD_INDEX:
            case BytecodeOp::R_RETURN:
            case BytecodeOp::R_JUMP:
            case BytecodeOp::R_JUMP_IF_FALSE:
            case BytecodeOp::R_JUMP_IF_TRUE:
            case BytecodeOp::R_LT:
            case BytecodeOp::R_LE:
            case BytecodeOp::R_GT:
            case BytecodeOp::R_GE:
            case BytecodeOp::R_EQ:
            case BytecodeOp::R_NE:
            case BytecodeOp::R_NOT:
            case BytecodeOp::R_NEG:
            case BytecodeOp::R_YIELD:
            case BytecodeOp::R_SI_ADD_STORE:
            case BytecodeOp::R_SI_SUB_STORE:
            case BytecodeOp::R_SI_MUL_STORE:
            case BytecodeOp::R_SI_CMP_JUMP_FALSE:
            case BytecodeOp::R_SI_LOAD_ADD_STORE:
            case BytecodeOp::R_SI_MODI_ADD_STORE:
            case BytecodeOp::R_SI_ADDI_CMPI_LT_JUMP:
            case BytecodeOp::R_SI_LOOP_STEP:
            case BytecodeOp::R_MAKE_FUNCTION:
                return true;
            default:
                return false;
        }
    };
    dump_chunk = [&](const BytecodeFunction& chunk, const std::string& label, std::ostringstream& out) {
        out << "chunk " << label << "\n";
        out << "locals=" << chunk.local_count << " upvalues=" << chunk.upvalue_names.size() << " constants=" << chunk.constants.size()
            << " code=" << chunk.instructions.size() << "\n";
        out << "register_mode=" << (chunk.uses_register_mode ? "true" : "false") << " max_regs=" << chunk.max_regs << "\n";
        if (!chunk.upvalue_names.empty()) {
            out << "captured";
            for (const auto& name : chunk.upvalue_names) {
                out << " " << name;
            }
            out << "\n";
        }
        if (!chunk.global_names.empty()) {
            out << "globals";
            for (const auto& name : chunk.global_names) {
                out << " " << name;
            }
            out << "\n";
        }
        if (!chunk.opcode_histogram.empty()) {
            out << "histogram";
            for (const auto& [name, count] : chunk.opcode_histogram) {
                out << " " << name << "=" << count;
            }
            out << "\n";
        }
        for (std::size_t index = 0; index < chunk.instructions.size(); ++index) {
            const auto& instruction = chunk.instructions[index];
            const auto& metadata = chunk.metadata[index];
            out << index << " line=" << instruction.span_line
                << " " << (is_superinstruction(instruction.op) ? "[SI] " : "") << "op=" << bytecode_op_name(instruction.op);
            const bool always_print_operand = instruction.op == BytecodeOp::LoadName || instruction.op == BytecodeOp::DefineName ||
                                              instruction.op == BytecodeOp::StoreName;
            const bool is_jump = instruction.op == BytecodeOp::Jump || instruction.op == BytecodeOp::JumpIfFalse ||
                                 instruction.op == BytecodeOp::JumpIfFalsePop || instruction.op == BytecodeOp::JumpIfTrue ||
                                 instruction.op == BytecodeOp::JumpIfNilKeep || instruction.op == BytecodeOp::SICmpJumpIfFalse;
            if (!is_jump && (always_print_operand || instruction.operand != 0)) {
                out << " operand=" << instruction.operand;
            }
            if (instruction.op == BytecodeOp::SICmpJumpIfFalse) {
                out << " cmp=" << bytecode_op_name(unpack_si_cmp_jump_compare_op(instruction.operand))
                    << " jump=" << unpack_si_cmp_jump_target(instruction.operand);
            } else if (is_jump && instruction.operand >= 0) {
                out << " jump=" << instruction.operand;
            }
            if (is_register_instruction(instruction.op)) {
                switch (instruction.op) {
                    case BytecodeOp::R_LOAD_CONST:
                        out << " dst=r" << static_cast<int>(unpack_r_dst_operand(instruction.operand))
                            << " const_idx=" << unpack_r_index_operand(instruction.operand);
                        break;
                    case BytecodeOp::R_LOAD_INT:
                        out << " dst=r" << static_cast<int>(unpack_r_load_int_dst(instruction.operand))
                            << " value=" << unpack_r_load_int_value(instruction.operand);
                        break;
                    case BytecodeOp::R_ADDI:
                        out << " dst=r" << static_cast<int>(unpack_r_addi_dst(instruction.operand))
                            << " src=r" << static_cast<int>(unpack_r_addi_src(instruction.operand))
                            << " imm=" << unpack_r_addi_imm(instruction.operand);
                        break;
                    case BytecodeOp::R_MODI:
                        out << " dst=r" << static_cast<int>(unpack_r_modi_dst(instruction.operand))
                            << " src=r" << static_cast<int>(unpack_r_modi_src(instruction.operand))
                            << " imm=" << unpack_r_modi_imm(instruction.operand);
                        break;
                    case BytecodeOp::R_ADDI_JUMP:
                        out << " dst=r" << static_cast<int>(unpack_r_addi_dst(instruction.operand))
                            << " src=r" << static_cast<int>(unpack_r_addi_src(instruction.operand))
                            << " imm=" << unpack_r_addi_imm(instruction.operand)
                            << " target=" << instruction.ic_slot;
                        break;
                    case BytecodeOp::R_SI_CMPI_JUMP_FALSE:
                        out << " src=r" << static_cast<int>(unpack_r_si_cmpi_jump_false_src1(instruction.operand))
                            << " imm=" << unpack_r_si_cmpi_jump_false_imm(instruction.operand)
                            << " kind=" << bytecode_op_name(register_bytecode_op_from_superinstruction_compare_kind(unpack_r_si_cmpi_jump_false_kind(instruction.operand)));
                        break;
                    case BytecodeOp::R_LOAD_GLOBAL:
                        out << " dst=r" << static_cast<int>(unpack_r_dst_operand(instruction.operand))
                            << " global_idx=" << unpack_r_index_operand(instruction.operand);
                        break;
                    case BytecodeOp::R_STORE_GLOBAL:
                        out << " global_idx=" << unpack_r_index_operand(instruction.operand)
                            << " src=r" << static_cast<int>(unpack_r_src_operand(instruction.operand));
                        break;
                    case BytecodeOp::R_MOVE:
                    case BytecodeOp::R_NOT:
                    case BytecodeOp::R_NEG:
                        out << " dst=r" << static_cast<int>(instruction.dst) << " src=r" << static_cast<int>(instruction.src1);
                        break;
                    case BytecodeOp::R_YIELD:
                        out << " src=r" << static_cast<int>(instruction.src1);
                        break;
                    case BytecodeOp::R_CALL:
                        out << " dst=r" << static_cast<int>(instruction.dst) << " callee=r" << static_cast<int>(instruction.src1)
                            << " args_start=r" << static_cast<int>(instruction.src2) << " argc=" << static_cast<int>(instruction.operand_a);
                        break;
                    case BytecodeOp::R_LOAD_MEMBER:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " obj=r" << static_cast<int>(instruction.src1)
                            << " member=" << metadata.string_operand;
                        break;
                    case BytecodeOp::R_STORE_MEMBER:
                        out << " obj=r" << static_cast<int>(instruction.src1)
                            << " val=r" << static_cast<int>(instruction.src2)
                            << " member=" << metadata.string_operand;
                        break;
                    case BytecodeOp::R_CALL_MEMBER:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " obj=r" << static_cast<int>(instruction.src1)
                            << " args_start=r" << static_cast<int>(instruction.src2)
                            << " argc=" << static_cast<int>(instruction.operand_a)
                            << " member=" << metadata.string_operand;
                        break;
                    case BytecodeOp::R_BUILD_STRUCT:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " base=r" << static_cast<int>(instruction.src1)
                            << " count=" << static_cast<int>(instruction.operand_a)
                            << " type=" << metadata.string_operand;
                        for (const auto& name : metadata.names) out << " field=" << name;
                        break;
                    case BytecodeOp::R_BUILD_ARRAY:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " base=r" << static_cast<int>(instruction.src1)
                            << " count=" << static_cast<int>(instruction.operand_a);
                        break;
                    case BytecodeOp::R_LOAD_INDEX:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " obj=r" << static_cast<int>(instruction.src1)
                            << " idx=r" << static_cast<int>(instruction.src2);
                        break;
                    case BytecodeOp::R_RETURN:
                        out << " src=r" << static_cast<int>(instruction.src1);
                        break;
                    case BytecodeOp::R_JUMP:
                        out << " target=" << instruction.operand;
                        break;
                    case BytecodeOp::R_JUMP_IF_FALSE:
                    case BytecodeOp::R_JUMP_IF_TRUE:
                        out << " src=r" << static_cast<int>(unpack_r_src_operand(instruction.operand))
                            << " target=" << unpack_r_jump_target_operand(instruction.operand);
                        break;
                    case BytecodeOp::R_ADD:
                    case BytecodeOp::R_SUB:
                    case BytecodeOp::R_MUL:
                    case BytecodeOp::R_DIV:
                    case BytecodeOp::R_MOD:
                    case BytecodeOp::R_LT:
                    case BytecodeOp::R_LE:
                    case BytecodeOp::R_GT:
                    case BytecodeOp::R_GE:
                    case BytecodeOp::R_EQ:
                    case BytecodeOp::R_NE:
                    case BytecodeOp::R_SI_ADD_STORE:
                    case BytecodeOp::R_SI_SUB_STORE:
                    case BytecodeOp::R_SI_MUL_STORE:
                        out << " dst=r" << static_cast<int>(instruction.dst) << " src1=r" << static_cast<int>(instruction.src1)
                            << " src2=r" << static_cast<int>(instruction.src2);
                        break;
                    case BytecodeOp::R_SI_CMP_JUMP_FALSE:
                        out << " src1=r" << static_cast<int>(unpack_r_si_cmp_jump_false_src1(instruction.operand))
                            << " src2=r" << static_cast<int>(unpack_r_si_cmp_jump_false_src2(instruction.operand))
                            << " cmp=" << bytecode_op_name(unpack_r_si_cmp_jump_false_compare_op(instruction.operand));
                        if (!metadata.jump_table.empty()) {
                            out << " target=" << metadata.jump_table.front();
                        }
                        break;
                    case BytecodeOp::R_SI_LOAD_ADD_STORE:
                        out << " dst=r" << static_cast<int>(unpack_r_si_load_add_store_dst(instruction.operand))
                            << " src=r" << static_cast<int>(unpack_r_si_load_add_store_local_src(instruction.operand))
                            << " const_idx=" << unpack_r_si_load_add_store_constant(instruction.operand);
                        break;
                    case BytecodeOp::R_SI_MODI_ADD_STORE:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " accum=r" << static_cast<int>(instruction.src1)
                            << " src=r" << static_cast<int>(instruction.src2)
                            << " div=" << static_cast<int>(instruction.operand_a);
                        break;
                    case BytecodeOp::R_SI_ADDI_CMPI_LT_JUMP:
                        out << " reg=r" << static_cast<int>(unpack_r_si_acj_reg(instruction.operand))
                            << " addi=" << unpack_r_si_acj_addi(instruction.operand)
                            << " limit=" << unpack_r_si_acj_limit(instruction.operand)
                            << " body=" << instruction.ic_slot;
                        break;
                    case BytecodeOp::R_SI_LOOP_STEP:
                        out << " accum=r" << static_cast<int>(instruction.dst)
                            << " iter=r" << static_cast<int>(instruction.src2)
                            << " div=" << static_cast<int>(instruction.operand_a)
                            << " step=" << static_cast<int>(static_cast<std::int8_t>(instruction.src1))
                            << " limit=" << unpack_r_si_ls_limit(instruction.ic_slot)
                            << " body=" << unpack_r_si_ls_body(instruction.ic_slot);
                        break;
                    case BytecodeOp::R_MAKE_FUNCTION:
                        out << " dst=r" << static_cast<int>(instruction.dst)
                            << " name=" << metadata.string_operand
                            << " captures=" << metadata.jump_table.size();
                        if (metadata.bytecode) {
                            out << " upvalues=[";
                            for (std::size_t ui = 0; ui < metadata.jump_table.size(); ++ui) {
                                if (ui > 0) out << ",";
                                out << "r" << metadata.jump_table[ui];
                            }
                            out << "]";
                        }
                        break;
                    default:
                        break;
                }
            }
            if (!metadata.string_operand.empty()) {
                out << " text=" << metadata.string_operand;
            }
            if (instruction.operand >= 0 && instruction.op == BytecodeOp::LoadConst &&
                static_cast<std::size_t>(instruction.operand) < chunk.constant_descriptions.size()) {
                out << " const=" << chunk.constant_descriptions[static_cast<std::size_t>(instruction.operand)];
            } else if (instruction.op == BytecodeOp::R_LOAD_CONST &&
                       static_cast<std::size_t>(unpack_r_index_operand(instruction.operand)) < chunk.constant_descriptions.size()) {
                out << " const=" << chunk.constant_descriptions[static_cast<std::size_t>(unpack_r_index_operand(instruction.operand))];
            } else if (instruction.op == BytecodeOp::R_SI_LOAD_ADD_STORE &&
                       static_cast<std::size_t>(unpack_r_si_load_add_store_constant(instruction.operand)) < chunk.constant_descriptions.size()) {
                out << " const=" << chunk.constant_descriptions[static_cast<std::size_t>(unpack_r_si_load_add_store_constant(instruction.operand))];
            }
            out << "\n";
            if (metadata.bytecode != nullptr) {
                const std::string nested_label = label + "::" + (metadata.string_operand.empty() ? "<anonymous>" : metadata.string_operand) + "#" +
                                                 std::to_string(index);
                dump_chunk(*metadata.bytecode, nested_label, out);
            }
        }
    };

    const auto module_it = modules_.find(module_name);
    if (module_it == modules_.end()) {
        return "unknown module: " + module_name;
    }
    if (function_name.empty()) {
        if (!module_it->second.bytecode) {
            return "module bytecode is unavailable for " + module_name;
        }
        std::ostringstream out;
        dump_chunk(*module_it->second.bytecode, module_name + "::<module>", out);
        return out.str();
    }

    const auto binding_it = module_it->second.environment->values.find(function_name);
    if (binding_it == module_it->second.environment->values.end() || !read_binding_value(binding_it->second).is_object()) {
        return "unknown function: " + module_name + "::" + function_name;
    }
    const Value function_value = read_binding_value(binding_it->second);
    if (function_value.as_object()->kind == ObjectKind::ScriptFunction) {
        const auto* function = static_cast<const ScriptFunctionObject*>(function_value.as_object());
        if (!function->bytecode) {
            return "script function has no bytecode: " + module_name + "::" + function_name;
        }
        std::ostringstream out;
        dump_chunk(*function->bytecode, module_name + "::" + function_name, out);
        return out.str();
    }
    if (function_value.as_object()->kind == ObjectKind::NativeFunction) {
        return "native function: " + module_name + "::" + function_name;
    }
    return "value is not callable bytecode: " + module_name + "::" + function_name;
}

Value Runtime::from_public_value(const ZephyrValue& value) {
    switch (value.kind()) {
        case ZephyrValue::Kind::Nil:
            return Value::nil();
        case ZephyrValue::Kind::Bool:
            return Value::boolean(value.as_bool());
        case ZephyrValue::Kind::Int:
            return Value::integer(value.as_int());
        case ZephyrValue::Kind::Float:
            return Value::floating(value.as_float());
        case ZephyrValue::Kind::String:
            return make_string(value.as_string());
        case ZephyrValue::Kind::Array: {
            auto* array = allocate<ArrayObject>();
            for (const auto& element : value.as_array()) {
                Value internal = from_public_value(element);
                const std::size_t index = array->elements.size();
                array->elements.push_back(internal);
                note_array_element_write(array, index, internal);
            }
            return Value::object(array);
        }
        case ZephyrValue::Kind::Record: {
            const auto& record = value.as_record();
            auto* type = allocate<StructTypeObject>(record.type_name);
            auto* instance = allocate<StructInstanceObject>(type);
            std::vector<std::pair<std::string, Value>> converted_fields;
            converted_fields.reserve(record.fields.size());
            for (const auto& field : record.fields) {
                type->fields.push_back(StructFieldSpec{field.first, "any"});
                converted_fields.emplace_back(field.first, from_public_value(field.second));
            }
            initialize_struct_instance(instance);
            for (std::size_t field_index = 0; field_index < converted_fields.size(); ++field_index) {
                instance->field_values[field_index] = converted_fields[field_index].second;
                note_struct_field_write(instance, field_index, converted_fields[field_index].second);
            }
            return Value::object(instance);
        }
        case ZephyrValue::Kind::Enum: {
            const auto& enum_value = value.as_enum();
            auto* type = allocate<EnumTypeObject>(enum_value.type_name);
            EnumVariantSpec spec;
            spec.name = enum_value.variant_name;
            spec.payload_types.resize(enum_value.payload.size(), "any");
            type->variants.push_back(spec);
            auto* instance = allocate<EnumInstanceObject>(type, enum_value.variant_name);
            for (const auto& payload : enum_value.payload) {
                Value internal = from_public_value(payload);
                instance->payload.push_back(internal);
                note_enum_payload_write(instance, instance->payload.size() - 1, internal);
            }
            return Value::object(instance);
        }
        case ZephyrValue::Kind::HostObject: {
            const auto& host_object = value.as_host_object();
            return Value::host_handle(register_host_handle(host_object));
        }
    }
    return Value::nil();
}

ZephyrValue Runtime::to_public_value(const Value& value) {
    if (value.is_nil()) {
        return ZephyrValue();
    }
    if (value.is_bool()) {
        return ZephyrValue(value.as_bool());
    }
    if (value.is_int()) {
        return ZephyrValue(value.as_int());
    }
    if (value.is_float()) {
        return ZephyrValue(value.as_float());
    }
    if (value.is_host_handle()) {
        const HostHandleToken token = value.as_host_handle();
        ZephyrHostObjectRef handle;
        handle.slot = token.slot;
        handle.generation = token.generation;
        if (token.slot < host_handles_.size()) {
            const HostHandleEntry& entry = host_handles_[token.slot];
            handle.host_class = entry.host_class;
            handle.instance = entry.residency_owner ? entry.residency_owner : entry.instance.lock();
            handle.kind = entry.kind;
            handle.lifetime = entry.lifetime;
            handle.strong_residency = (entry.flags & HostHandleStrongResidencyBit) != 0;
            handle.stable_guid = entry.stable_guid;
            handle.policy = entry.policy;
            handle.has_explicit_policy = true;
            handle.valid = !entry.invalid() && token.generation == entry.generation && handle.instance != nullptr;
            handle.invalid_reason =
                handle.valid ? std::string{} : (entry.invalid_reason.empty() ? std::string("Host handle is invalid.") : entry.invalid_reason);
        } else {
            handle.valid = false;
            handle.invalid_reason = "Unknown host handle slot.";
        }
        return ZephyrValue(handle);
    }
    if (!value.is_object()) {
        return ZephyrValue();
    }

    switch (value.as_object()->kind) {
        case ObjectKind::String:
            return ZephyrValue(static_cast<StringObject*>(value.as_object())->value);
        case ObjectKind::Array: {
            ZephyrValue::Array array;
            for (const auto& element : static_cast<ArrayObject*>(value.as_object())->elements) {
                array.push_back(to_public_value(element));
            }
            return ZephyrValue(array);
        }
        case ObjectKind::StructInstance: {
            const auto* instance = static_cast<StructInstanceObject*>(value.as_object());
            ZephyrRecord record;
            record.type_name = instance->type->name;
            for (std::size_t field_index = 0; field_index < instance->field_values.size() && field_index < instance->type->fields.size(); ++field_index) {
                record.fields[instance->type->fields[field_index].name] = to_public_value(instance->field_values[field_index]);
            }
            return ZephyrValue(record);
        }
        case ObjectKind::EnumInstance: {
            const auto* instance = static_cast<EnumInstanceObject*>(value.as_object());
            ZephyrEnumValue enum_value;
            enum_value.type_name = instance->type->name;
            enum_value.variant_name = instance->variant;
            for (const auto& payload : instance->payload) {
                enum_value.payload.push_back(to_public_value(payload));
            }
            return ZephyrValue(enum_value);
        }
        default:
            fail("Unsupported runtime value crossing host boundary.");
    }
}

RuntimeResult<ZephyrValue> Runtime::serialize_public_value(const ZephyrValue& value) {
    ++serialized_value_exports_;
    Value internal = from_public_value(value);
    return serialize_runtime_value(internal, Span{}, "<serialize>");
}

RuntimeResult<ZephyrValue> Runtime::deserialize_public_value(const ZephyrValue& value) {
    ++deserialized_value_imports_;
    ZEPHYR_TRY_ASSIGN(internal, deserialize_public_payload(value, Span{}, "<deserialize>"));
    return to_public_value(internal);
}

namespace {

constexpr std::uint8_t kSnapshotKindNil = 0;
constexpr std::uint8_t kSnapshotKindBool = 1;
constexpr std::uint8_t kSnapshotKindInt = 2;
constexpr std::uint8_t kSnapshotKindFloat = 3;
constexpr std::uint8_t kSnapshotKindString = 4;
constexpr std::uint8_t kSnapshotKindArray = 5;
constexpr std::uint8_t kSnapshotKindRecord = 6;
constexpr std::uint8_t kSnapshotKindEnum = 7;
constexpr std::string_view kSnapshotMagic = "ZEPHYR-SNAPSHOT-1";
constexpr std::string_view kSnapshotVersion = "wave-e2-v1";

void snapshot_append_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void snapshot_append_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void snapshot_append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

bool snapshot_read_u8(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint8_t& value) {
    if (offset >= data.size()) {
        return false;
    }
    value = data[offset++];
    return true;
}

bool snapshot_read_u32(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint32_t& value) {
    if (offset + 4 > data.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(data[offset++]) << shift;
    }
    return true;
}

bool snapshot_read_u64(const std::vector<std::uint8_t>& data, std::size_t& offset, std::uint64_t& value) {
    if (offset + 8 > data.size()) {
        return false;
    }
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(data[offset++]) << shift;
    }
    return true;
}

void snapshot_encode_string(std::vector<std::uint8_t>& out, const std::string& value) {
    snapshot_append_u32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool snapshot_decode_string(const std::vector<std::uint8_t>& data, std::size_t& offset, std::string& value) {
    std::uint32_t size = 0;
    if (!snapshot_read_u32(data, offset, size) || offset + size > data.size()) {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(data.data() + offset), size);
    offset += size;
    return true;
}

void snapshot_encode_value(std::vector<std::uint8_t>& out, const ZephyrValue& value) {
    switch (value.kind()) {
        case ZephyrValue::Kind::Nil:
            snapshot_append_u8(out, kSnapshotKindNil);
            return;
        case ZephyrValue::Kind::Bool:
            snapshot_append_u8(out, kSnapshotKindBool);
            snapshot_append_u8(out, value.as_bool() ? 1 : 0);
            return;
        case ZephyrValue::Kind::Int:
            snapshot_append_u8(out, kSnapshotKindInt);
            snapshot_append_u64(out, static_cast<std::uint64_t>(value.as_int()));
            return;
        case ZephyrValue::Kind::Float:
            snapshot_append_u8(out, kSnapshotKindFloat);
            snapshot_append_u64(out, std::bit_cast<std::uint64_t>(value.as_float()));
            return;
        case ZephyrValue::Kind::String:
            snapshot_append_u8(out, kSnapshotKindString);
            snapshot_encode_string(out, value.as_string());
            return;
        case ZephyrValue::Kind::Array: {
            snapshot_append_u8(out, kSnapshotKindArray);
            const auto& items = value.as_array();
            snapshot_append_u32(out, static_cast<std::uint32_t>(items.size()));
            for (const auto& item : items) {
                snapshot_encode_value(out, item);
            }
            return;
        }
        case ZephyrValue::Kind::Record: {
            snapshot_append_u8(out, kSnapshotKindRecord);
            const auto& record = value.as_record();
            snapshot_encode_string(out, record.type_name);
            snapshot_append_u32(out, static_cast<std::uint32_t>(record.fields.size()));
            for (const auto& [field_name, field_value] : record.fields) {
                snapshot_encode_string(out, field_name);
                snapshot_encode_value(out, field_value);
            }
            return;
        }
        case ZephyrValue::Kind::Enum: {
            snapshot_append_u8(out, kSnapshotKindEnum);
            const auto& enum_value = value.as_enum();
            snapshot_encode_string(out, enum_value.type_name);
            snapshot_encode_string(out, enum_value.variant_name);
            snapshot_append_u32(out, static_cast<std::uint32_t>(enum_value.payload.size()));
            for (const auto& payload : enum_value.payload) {
                snapshot_encode_value(out, payload);
            }
            return;
        }
        case ZephyrValue::Kind::HostObject:
            snapshot_append_u8(out, kSnapshotKindNil);
            return;
    }
}

bool snapshot_decode_value(const std::vector<std::uint8_t>& data, std::size_t& offset, ZephyrValue& value) {
    std::uint8_t kind = 0;
    if (!snapshot_read_u8(data, offset, kind)) {
        return false;
    }
    switch (kind) {
        case kSnapshotKindNil:
            value = ZephyrValue();
            return true;
        case kSnapshotKindBool: {
            std::uint8_t encoded = 0;
            if (!snapshot_read_u8(data, offset, encoded)) {
                return false;
            }
            value = ZephyrValue(encoded != 0);
            return true;
        }
        case kSnapshotKindInt: {
            std::uint64_t encoded = 0;
            if (!snapshot_read_u64(data, offset, encoded)) {
                return false;
            }
            value = ZephyrValue(static_cast<std::int64_t>(encoded));
            return true;
        }
        case kSnapshotKindFloat: {
            std::uint64_t encoded = 0;
            if (!snapshot_read_u64(data, offset, encoded)) {
                return false;
            }
            value = ZephyrValue(std::bit_cast<double>(encoded));
            return true;
        }
        case kSnapshotKindString: {
            std::string text;
            if (!snapshot_decode_string(data, offset, text)) {
                return false;
            }
            value = ZephyrValue(std::move(text));
            return true;
        }
        case kSnapshotKindArray: {
            std::uint32_t count = 0;
            if (!snapshot_read_u32(data, offset, count)) {
                return false;
            }
            ZephyrValue::Array items;
            items.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                ZephyrValue item;
                if (!snapshot_decode_value(data, offset, item)) {
                    return false;
                }
                items.push_back(std::move(item));
            }
            value = ZephyrValue(std::move(items));
            return true;
        }
        case kSnapshotKindRecord: {
            ZephyrRecord record;
            if (!snapshot_decode_string(data, offset, record.type_name)) {
                return false;
            }
            std::uint32_t count = 0;
            if (!snapshot_read_u32(data, offset, count)) {
                return false;
            }
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string field_name;
                ZephyrValue field_value;
                if (!snapshot_decode_string(data, offset, field_name) || !snapshot_decode_value(data, offset, field_value)) {
                    return false;
                }
                record.fields.emplace(std::move(field_name), std::move(field_value));
            }
            value = ZephyrValue(record);
            return true;
        }
        case kSnapshotKindEnum: {
            ZephyrEnumValue enum_value;
            if (!snapshot_decode_string(data, offset, enum_value.type_name) ||
                !snapshot_decode_string(data, offset, enum_value.variant_name)) {
                return false;
            }
            std::uint32_t count = 0;
            if (!snapshot_read_u32(data, offset, count)) {
                return false;
            }
            enum_value.payload.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                ZephyrValue item;
                if (!snapshot_decode_value(data, offset, item)) {
                    return false;
                }
                enum_value.payload.push_back(std::move(item));
            }
            value = ZephyrValue(enum_value);
            return true;
        }
        default:
            return false;
    }
}

bool snapshot_should_capture_value(const Value& value) {
    if (value.is_host_handle()) {
        return true;
    }
    if (!value.is_object()) {
        return true;
    }
    switch (value.as_object()->kind) {
        case ObjectKind::ScriptFunction:
        case ObjectKind::NativeFunction:
        case ObjectKind::Coroutine:
        case ObjectKind::ModuleNamespace:
        case ObjectKind::StructType:
        case ObjectKind::EnumType:
            return false;
        default:
            return true;
    }
}

}  // namespace

ZephyrSnapshot Runtime::snapshot_state() const {
    struct SnapshotBindingRecord {
        std::string name;
        bool mutable_value = false;
        std::optional<std::string> type_name;
        ZephyrValue value;
    };

    auto collect_bindings = [this](Environment* environment) {
        std::vector<SnapshotBindingRecord> bindings;
        Runtime* self = const_cast<Runtime*>(this);
        for (const auto& [name, binding] : environment->values) {
            if (!snapshot_should_capture_value(binding.value)) {
                continue;
            }
            SnapshotBindingRecord record;
            record.name = name;
            record.mutable_value = binding.mutable_value;
            record.type_name = binding.type_name;
            if (binding.value.is_host_handle()) {
                record.value = ZephyrValue();
            } else {
                auto serialized = self->serialize_runtime_value(binding.value, Span{}, "<snapshot>");
                if (!serialized) {
                    continue;
                }
                record.value = std::move(*serialized);
            }
            bindings.push_back(std::move(record));
        }
        std::sort(bindings.begin(), bindings.end(), [](const auto& left, const auto& right) { return left.name < right.name; });
        return bindings;
    };

    ZephyrSnapshot snapshot;
    snapshot.version = std::string(kSnapshotVersion);
    snapshot.timestamp_utc = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());

    auto& out = snapshot.data;
    out.insert(out.end(), kSnapshotMagic.begin(), kSnapshotMagic.end());
    snapshot_append_u32(out, static_cast<std::uint32_t>(snapshot.version.size()));
    out.insert(out.end(), snapshot.version.begin(), snapshot.version.end());
    snapshot_append_u64(out, snapshot.timestamp_utc);

    const auto root_bindings = collect_bindings(root_environment_);
    snapshot_append_u32(out, static_cast<std::uint32_t>(root_bindings.size()));
    for (const auto& binding : root_bindings) {
        snapshot_encode_string(out, binding.name);
        snapshot_append_u8(out, binding.mutable_value ? 1 : 0);
        snapshot_append_u8(out, binding.type_name.has_value() ? 1 : 0);
        if (binding.type_name.has_value()) {
            snapshot_encode_string(out, *binding.type_name);
        }
        snapshot_encode_value(out, binding.value);
    }

    std::vector<std::string> module_names;
    module_names.reserve(modules_.size());
    for (const auto& [module_name, _] : modules_) {
        module_names.push_back(module_name);
    }
    std::sort(module_names.begin(), module_names.end());
    snapshot_append_u32(out, static_cast<std::uint32_t>(module_names.size()));
    for (const auto& module_name : module_names) {
        snapshot_encode_string(out, module_name);
        const auto& module = modules_.at(module_name);
        const auto bindings = collect_bindings(module.environment);
        snapshot_append_u32(out, static_cast<std::uint32_t>(bindings.size()));
        for (const auto& binding : bindings) {
            snapshot_encode_string(out, binding.name);
            snapshot_append_u8(out, binding.mutable_value ? 1 : 0);
            snapshot_append_u8(out, binding.type_name.has_value() ? 1 : 0);
            if (binding.type_name.has_value()) {
                snapshot_encode_string(out, *binding.type_name);
            }
            snapshot_encode_value(out, binding.value);
        }
    }

    return snapshot;
}

bool Runtime::restore_snapshot_state(const ZephyrSnapshot& snapshot) {
    const auto clear_environment = [](Environment* environment) {
        std::vector<std::string> to_remove;
        for (const auto& [name, binding] : environment->values) {
            if (snapshot_should_capture_value(binding.value)) {
                to_remove.push_back(name);
            }
        }
        for (const auto& name : to_remove) {
            environment->values.erase(name);
        }
        ++environment->version;
    };

    if (snapshot.version != kSnapshotVersion) {
        return false;
    }
    if (snapshot.data.size() < kSnapshotMagic.size() || !std::equal(kSnapshotMagic.begin(), kSnapshotMagic.end(), snapshot.data.begin())) {
        return false;
    }

    clear_environment(root_environment_);
    for (auto& [_, module] : modules_) {
        clear_environment(module.environment);
    }

    std::size_t offset = kSnapshotMagic.size();
    std::uint32_t version_size = 0;
    if (!snapshot_read_u32(snapshot.data, offset, version_size) || offset + version_size > snapshot.data.size()) {
        return false;
    }
    offset += version_size;
    std::uint64_t timestamp = 0;
    if (!snapshot_read_u64(snapshot.data, offset, timestamp)) {
        return false;
    }

    auto restore_bindings = [this, &snapshot, &offset](Environment* environment) -> bool {
        std::uint32_t count = 0;
        if (!snapshot_read_u32(snapshot.data, offset, count)) {
            return false;
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            std::string name;
            std::uint8_t mutable_flag = 0;
            std::uint8_t has_type = 0;
            std::optional<std::string> type_name;
            ZephyrValue serialized;
            if (!snapshot_decode_string(snapshot.data, offset, name) || !snapshot_read_u8(snapshot.data, offset, mutable_flag) ||
                !snapshot_read_u8(snapshot.data, offset, has_type)) {
                return false;
            }
            if (has_type != 0) {
                std::string type_text;
                if (!snapshot_decode_string(snapshot.data, offset, type_text)) {
                    return false;
                }
                type_name = std::move(type_text);
            }
            if (!snapshot_decode_value(snapshot.data, offset, serialized)) {
                return false;
            }
            auto internal = deserialize_public_payload(serialized, Span{}, "<snapshot>");
            if (!internal) {
                return false;
            }
            define_value(environment, name, *internal, mutable_flag != 0, type_name);
        }
        return true;
    };

    if (!restore_bindings(root_environment_)) {
        return false;
    }

    std::uint32_t module_count = 0;
    if (!snapshot_read_u32(snapshot.data, offset, module_count)) {
        return false;
    }
    for (std::uint32_t module_index = 0; module_index < module_count; ++module_index) {
        std::string module_name;
        if (!snapshot_decode_string(snapshot.data, offset, module_name)) {
            return false;
        }
        const auto module_it = modules_.find(module_name);
        if (module_it == modules_.end()) {
            return false;
        }
        if (!restore_bindings(module_it->second.environment)) {
            return false;
        }
    }

    return offset == snapshot.data.size();
}

RuntimeResult<ZephyrValue> Runtime::serialize_runtime_value(const Value& value, const Span& span, const std::string& module_name) {
    ZEPHYR_TRY_ASSIGN(payload, serialize_runtime_node(value, span, module_name));
    ZephyrRecord envelope;
    envelope.type_name = kSerializedEnvelopeType;
    envelope.fields["schema"] = ZephyrValue(std::string(kSerializedSchemaName));
    envelope.fields["version"] = ZephyrValue(kSerializedSchemaVersion);
    envelope.fields["payload"] = std::move(payload);
    return ZephyrValue(envelope);
}

RuntimeResult<ZephyrValue> Runtime::serialize_runtime_node(const Value& value, const Span& span, const std::string& module_name) {
    auto make_node = [](std::string kind) {
        ZephyrRecord node;
        node.type_name = kSerializedNodeType;
        node.fields["kind"] = ZephyrValue(std::move(kind));
        return node;
    };

    if (value.is_nil()) {
        return ZephyrValue(make_node("nil"));
    }
    if (value.is_bool()) {
        ZephyrRecord node = make_node("bool");
        node.fields["value"] = ZephyrValue(value.as_bool());
        return ZephyrValue(node);
    }
    if (value.is_int()) {
        ZephyrRecord node = make_node("int");
        node.fields["value"] = ZephyrValue(value.as_int());
        return ZephyrValue(node);
    }
    if (value.is_float()) {
        ZephyrRecord node = make_node("float");
        node.fields["value"] = ZephyrValue(value.as_float());
        return ZephyrValue(node);
    }
    if (value.is_object() && value.as_object()->kind == ObjectKind::String) {
        ZephyrRecord node = make_node("string");
        node.fields["value"] = ZephyrValue(static_cast<StringObject*>(value.as_object())->value);
        return ZephyrValue(node);
    }
    if (value.is_host_handle()) {
        ZEPHYR_TRY(validate_handle_store(value, HandleContainerKind::Serialize, span, module_name, "serialization"));
        const auto public_handle = to_public_value(value);
        const auto& handle = public_handle.as_host_object();
        ZephyrRecord node = make_node("stable_handle");
        if (handle.host_class != nullptr) {
            node.fields["class_name"] = ZephyrValue(handle.host_class->name());
        } else {
            node.fields["class_name"] = ZephyrValue(std::string());
        }
        node.fields["host_kind"] = ZephyrValue(static_cast<std::int64_t>(handle.kind));
        node.fields["guid_high"] = ZephyrValue(static_cast<std::int64_t>(handle.stable_guid.high));
        node.fields["guid_low"] = ZephyrValue(static_cast<std::int64_t>(handle.stable_guid.low));
        node.fields["strong_residency"] = ZephyrValue(handle.strong_residency);
        return ZephyrValue(node);
    }
    if (!value.is_object()) {
        return make_error<ZephyrValue>("Unsupported value during serialization.");
    }

    switch (value.as_object()->kind) {
        case ObjectKind::Array: {
            ZephyrValue::Array items;
            for (const auto& element : static_cast<ArrayObject*>(value.as_object())->elements) {
                ZEPHYR_TRY_ASSIGN(serialized_element, serialize_runtime_node(element, span, module_name));
                items.push_back(std::move(serialized_element));
            }
            ZephyrRecord node = make_node("array");
            node.fields["items"] = ZephyrValue(std::move(items));
            return ZephyrValue(node);
        }
        case ObjectKind::StructInstance: {
            const auto* instance = static_cast<StructInstanceObject*>(value.as_object());
            ZephyrRecord field_map;
            field_map.type_name = kSerializedFieldMapType;
            for (std::size_t field_index = 0; field_index < instance->field_values.size() && field_index < instance->type->fields.size(); ++field_index) {
                ZEPHYR_TRY_ASSIGN(serialized_field, serialize_runtime_node(instance->field_values[field_index], span, module_name));
                field_map.fields[instance->type->fields[field_index].name] = std::move(serialized_field);
            }
            ZephyrRecord node = make_node("record");
            node.fields["type_name"] = ZephyrValue(instance->type->name);
            node.fields["fields"] = ZephyrValue(field_map);
            return ZephyrValue(node);
        }
        case ObjectKind::EnumInstance: {
            const auto* instance = static_cast<EnumInstanceObject*>(value.as_object());
            ZephyrValue::Array payload_items;
            for (const auto& payload_value : instance->payload) {
                ZEPHYR_TRY_ASSIGN(serialized_payload, serialize_runtime_node(payload_value, span, module_name));
                payload_items.push_back(std::move(serialized_payload));
            }
            ZephyrRecord node = make_node("enum");
            node.fields["type_name"] = ZephyrValue(instance->type->name);
            node.fields["variant_name"] = ZephyrValue(instance->variant);
            node.fields["payload"] = ZephyrValue(std::move(payload_items));
            return ZephyrValue(node);
        }
        default:
            return make_loc_error<ZephyrValue>(module_name, span, "Only plain data, arrays, records, enums, and Stable host handles can be serialized.");
    }
}

RuntimeResult<Value> Runtime::deserialize_public_payload(const ZephyrValue& value, const Span& span, const std::string& module_name) {
    if (value.is_record()) {
        const auto& record = value.as_record();
        if (record.type_name == kSerializedEnvelopeType) {
            const auto schema_it = record.fields.find("schema");
            const auto version_it = record.fields.find("version");
            const auto payload_it = record.fields.find("payload");
            if (schema_it == record.fields.end() || version_it == record.fields.end() || payload_it == record.fields.end()) {
                return make_loc_error<Value>(module_name, span, "Serialized save envelope is missing one of: schema, version, payload.");
            }
            if (!schema_it->second.is_string() || schema_it->second.as_string() != kSerializedSchemaName) {
                return make_loc_error<Value>(module_name, span, "Serialized save envelope has an unknown schema identifier.");
            }
            if (!version_it->second.is_int()) {
                return make_loc_error<Value>(module_name, span, "Serialized save envelope version must be an Int.");
            }
            if (version_it->second.as_int() != kSerializedSchemaVersion) {
                return make_loc_error<Value>(
                    module_name,
                    span,
                    "Serialized save envelope version " + std::to_string(version_it->second.as_int()) +
                        " is unsupported. Expected version " + std::to_string(kSerializedSchemaVersion) + ".");
            }
            return deserialize_serialized_node(payload_it->second, span, module_name);
        }
    }

    // Backward-compatible path for older plain public values.
    return from_public_value(value);
}

RuntimeResult<Value> Runtime::deserialize_serialized_node(const ZephyrValue& value, const Span& span, const std::string& module_name) {
    if (!value.is_record()) {
        return make_loc_error<Value>(module_name, span, "Serialized payload node must be a Record.");
    }

    const auto& node = value.as_record();
    if (node.type_name != kSerializedNodeType) {
        return make_loc_error<Value>(module_name, span, "Serialized payload node must use the ZephyrSaveNode record type.");
    }

    const auto kind_it = node.fields.find("kind");
    if (kind_it == node.fields.end() || !kind_it->second.is_string()) {
        return make_loc_error<Value>(module_name, span, "Serialized payload node is missing its string kind field.");
    }
    const std::string& kind = kind_it->second.as_string();

    const auto require_field = [&](const char* name) -> RuntimeResult<const ZephyrValue*> {
        const auto it = node.fields.find(name);
        if (it == node.fields.end()) {
            return make_loc_error<const ZephyrValue*>(module_name, span, "Serialized payload node is missing field '" + std::string(name) + "'.");
        }
        return &it->second;
    };

    if (kind == "nil") {
        return Value::nil();
    }
    if (kind == "bool") {
        ZEPHYR_TRY_ASSIGN(value_field, require_field("value"));
        if (!value_field->is_bool()) {
            return make_loc_error<Value>(module_name, span, "Serialized bool node must carry a Bool value.");
        }
        return Value::boolean(value_field->as_bool());
    }
    if (kind == "int") {
        ZEPHYR_TRY_ASSIGN(value_field, require_field("value"));
        if (!value_field->is_int()) {
            return make_loc_error<Value>(module_name, span, "Serialized int node must carry an Int value.");
        }
        return Value::integer(value_field->as_int());
    }
    if (kind == "float") {
        ZEPHYR_TRY_ASSIGN(value_field, require_field("value"));
        if (!value_field->is_float()) {
            return make_loc_error<Value>(module_name, span, "Serialized float node must carry a Float value.");
        }
        return Value::floating(value_field->as_float());
    }
    if (kind == "string") {
        ZEPHYR_TRY_ASSIGN(value_field, require_field("value"));
        if (!value_field->is_string()) {
            return make_loc_error<Value>(module_name, span, "Serialized string node must carry a String value.");
        }
        return make_string(value_field->as_string());
    }
    if (kind == "array") {
        ZEPHYR_TRY_ASSIGN(items_field, require_field("items"));
        if (!items_field->is_array()) {
            return make_loc_error<Value>(module_name, span, "Serialized array node must carry an Array of child nodes.");
        }
        auto* array = allocate<ArrayObject>();
        for (const auto& item : items_field->as_array()) {
            ZEPHYR_TRY_ASSIGN(internal, deserialize_serialized_node(item, span, module_name));
            const std::size_t index = array->elements.size();
            array->elements.push_back(internal);
            note_array_element_write(array, index, internal);
        }
        return Value::object(array);
    }
    if (kind == "record") {
        ZEPHYR_TRY_ASSIGN(type_name_field, require_field("type_name"));
        ZEPHYR_TRY_ASSIGN(fields_field, require_field("fields"));
        if (!type_name_field->is_string()) {
            return make_loc_error<Value>(module_name, span, "Serialized record node type_name must be a String.");
        }
        if (!fields_field->is_record()) {
            return make_loc_error<Value>(module_name, span, "Serialized record node fields must be a Record map.");
        }
        const auto& field_map = fields_field->as_record();
        auto* type = allocate<StructTypeObject>(type_name_field->as_string());
        auto* instance = allocate<StructInstanceObject>(type);
        std::vector<Value> deserialized_fields;
        deserialized_fields.reserve(field_map.fields.size());
        for (const auto& field : field_map.fields) {
            type->fields.push_back(StructFieldSpec{field.first, "any"});
            ZEPHYR_TRY_ASSIGN(internal, deserialize_serialized_node(field.second, span, module_name));
            deserialized_fields.push_back(internal);
        }
        initialize_struct_instance(instance);
        for (std::size_t field_index = 0; field_index < deserialized_fields.size(); ++field_index) {
            instance->field_values[field_index] = deserialized_fields[field_index];
            note_struct_field_write(instance, field_index, deserialized_fields[field_index]);
        }
        return Value::object(instance);
    }
    if (kind == "enum") {
        ZEPHYR_TRY_ASSIGN(type_name_field, require_field("type_name"));
        ZEPHYR_TRY_ASSIGN(variant_field, require_field("variant_name"));
        ZEPHYR_TRY_ASSIGN(payload_field, require_field("payload"));
        if (!type_name_field->is_string() || !variant_field->is_string()) {
            return make_loc_error<Value>(module_name, span, "Serialized enum node must carry string type_name and variant_name fields.");
        }
        if (!payload_field->is_array()) {
            return make_loc_error<Value>(module_name, span, "Serialized enum node payload must be an Array.");
        }
        auto* type = allocate<EnumTypeObject>(type_name_field->as_string());
        EnumVariantSpec spec;
        spec.name = variant_field->as_string();
        spec.payload_types.resize(payload_field->as_array().size(), "any");
        type->variants.push_back(spec);
        auto* instance = allocate<EnumInstanceObject>(type, variant_field->as_string());
        for (const auto& payload : payload_field->as_array()) {
            ZEPHYR_TRY_ASSIGN(internal, deserialize_serialized_node(payload, span, module_name));
            instance->payload.push_back(internal);
            note_enum_payload_write(instance, instance->payload.size() - 1, internal);
        }
        return Value::object(instance);
    }
    if (kind == "stable_handle") {
        ZEPHYR_TRY_ASSIGN(kind_field, require_field("host_kind"));
        ZEPHYR_TRY_ASSIGN(guid_high_field, require_field("guid_high"));
        ZEPHYR_TRY_ASSIGN(guid_low_field, require_field("guid_low"));
        if (!kind_field->is_int() || !guid_high_field->is_int() || !guid_low_field->is_int()) {
            return make_loc_error<Value>(module_name, span, "Serialized stable_handle node must carry integer host_kind, guid_high, and guid_low fields.");
        }

        ZephyrHostObjectRef handle;
        handle.kind = static_cast<ZephyrHostHandleKind>(kind_field->as_int());
        handle.lifetime = ZephyrHostHandleLifetime::Stable;
        handle.stable_guid.high = static_cast<std::uint64_t>(guid_high_field->as_int());
        handle.stable_guid.low = static_cast<std::uint64_t>(guid_low_field->as_int());
        handle.policy = default_policy_for_kind(handle.kind);
        handle.policy.allow_serialize = true;
        handle.policy.allow_cross_scene = true;
        handle.has_explicit_policy = true;

        if (const auto class_it = node.fields.find("class_name"); class_it != node.fields.end() && class_it->second.is_string() &&
                                                          !class_it->second.as_string().empty()) {
            handle.host_class = std::make_shared<ZephyrHostClass>(class_it->second.as_string());
        }
        if (const auto residency_it = node.fields.find("strong_residency");
            residency_it != node.fields.end() && residency_it->second.is_bool()) {
            handle.strong_residency = residency_it->second.as_bool();
            if (handle.strong_residency) {
                handle.policy.strong_residency_allowed = true;
                handle.policy.weak_by_default = false;
            }
        }

        return Value::host_handle(register_host_handle(handle));
    }

    return make_loc_error<Value>(module_name, span, "Unsupported serialized payload node kind '" + kind + "'.");
}

std::optional<ZephyrFunctionHandle> Runtime::find_function(const std::string& module_name, const std::string& function_name) {
    auto module_it = modules_.find(module_name);
    if (module_it == modules_.end()) {
        return std::nullopt;
    }
    Binding* binding = lookup_binding(module_it->second.environment, function_name);
    if (binding == nullptr || !read_binding_value(*binding).is_object()) {
        return std::nullopt;
    }
    const Value function_value = read_binding_value(*binding);
    if (function_value.as_object()->kind != ObjectKind::ScriptFunction &&
        function_value.as_object()->kind != ObjectKind::NativeFunction) {
        return std::nullopt;
    }
    return ZephyrFunctionHandle{module_name, function_name};
}

RuntimeResult<Value> Runtime::call_handle(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args) {
    auto module_it = modules_.find(handle.module_name);
    if (module_it == modules_.end()) {
        return make_error<Value>("Unknown module in handle: " + handle.module_name);
    }
    Binding* binding = lookup_binding(module_it->second.environment, handle.function_name);
    if (binding == nullptr) {
        return make_error<Value>("Unknown function in handle: " + handle.function_name);
    }
    std::vector<Value> internal_args;
    internal_args.reserve(args.size());
    for (const auto& arg : args) {
        internal_args.push_back(from_public_value(arg));
    }
    return call_value(read_binding_value(*binding), internal_args, Span{}, handle.module_name);
}

ZephyrScriptCallbackHandle Runtime::capture_callback(const ZephyrFunctionHandle& handle) {
    if (!handle.valid()) {
        fail("Cannot capture an invalid script callback handle.");
    }
    const auto module_it = modules_.find(handle.module_name);
    if (module_it == modules_.end()) {
        fail("Cannot capture callback from unknown module: " + handle.module_name);
    }
    Binding* binding = lookup_binding(module_it->second.environment, handle.function_name);
    if (binding == nullptr) {
        fail("Cannot capture callback from unknown function: " + handle.function_name);
    }
    const std::uint64_t id = next_callback_id_++;
    retained_callbacks_[id] = RetainedCallbackRecord{handle};
    return ZephyrScriptCallbackHandle{id, handle.module_name, handle.function_name};
}

void Runtime::release_callback(const ZephyrScriptCallbackHandle& handle) {
    if (!handle.valid()) {
        return;
    }
    retained_callbacks_.erase(handle.id);
}

RuntimeResult<Value> Runtime::call_callback_handle(const ZephyrScriptCallbackHandle& handle, const std::vector<ZephyrValue>& args) {
    const auto it = retained_callbacks_.find(handle.id);
    if (it == retained_callbacks_.end()) {
        return make_error<Value>("Unknown retained callback handle.");
    }
    return call_handle(it->second.function, args);
}

RuntimeResult<ZephyrCoroutineHandle> Runtime::spawn_coroutine_handle(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args) {
    ZEPHYR_TRY_ASSIGN(value, call_handle(handle, args));
    if (!value.is_object() || value.as_object()->kind != ObjectKind::Coroutine) {
        return make_error<ZephyrCoroutineHandle>("Function did not return a coroutine value.");
    }
    auto* coroutine = static_cast<CoroutineObject*>(value.as_object());
    const std::uint64_t id = next_coroutine_handle_id_++;
    coroutine->handle_retained = true;
    retained_coroutines_[id] = coroutine;
    return ZephyrCoroutineHandle{id};
}

RuntimeResult<Value> Runtime::resume_coroutine_handle(const ZephyrCoroutineHandle& handle) {
    const auto it = retained_coroutines_.find(handle.id);
    if (it == retained_coroutines_.end() || it->second == nullptr) {
        return make_error<Value>("Unknown retained coroutine handle.");
    }
    return resume_coroutine_value(Value::object(it->second), Span{}, "<coroutine-handle>");
}

void Runtime::cancel_coroutine_handle(const ZephyrCoroutineHandle& handle) {
    const auto it = retained_coroutines_.find(handle.id);
    if (it == retained_coroutines_.end() || it->second == nullptr) {
        return;
    }
    unregister_suspended_coroutine(it->second);
    it->second->handle_retained = false;
    it->second->completed = true;
    it->second->suspended = false;
    record_coroutine_completed(it->second);
    retained_coroutines_.erase(it);
}

std::optional<ZephyrCoroutineInfo> Runtime::query_coroutine_handle(const ZephyrCoroutineHandle& handle) const {
    const auto it = retained_coroutines_.find(handle.id);
    if (it == retained_coroutines_.end() || it->second == nullptr) {
        return std::nullopt;
    }
    const auto* coroutine = it->second;
    ZephyrCoroutineInfo info;
    info.started = coroutine->started;
    info.suspended = coroutine->suspended;
    info.completed = coroutine->completed;
    info.frame_depth = coroutine->frames.size();
    info.resume_count = coroutine->resume_count;
    info.yield_count = coroutine->yield_count;
    for (const auto& frame : coroutine->frames) {
        info.stack_values += frame.stack.size();
        info.local_slots += frame.locals.size();
    }
    return info;
}

bool ZephyrGuid128::valid() const { return high != 0 || low != 0; }
bool operator==(const ZephyrGuid128& left, const ZephyrGuid128& right) {
    return left.high == right.high && left.low == right.low;
}

ZephyrValue::ZephyrValue() : storage_(std::monostate{}) {}
ZephyrValue::ZephyrValue(std::nullptr_t) : storage_(std::monostate{}) {}
ZephyrValue::ZephyrValue(bool value) : storage_(value) {}
ZephyrValue::ZephyrValue(std::int64_t value) : storage_(value) {}
ZephyrValue::ZephyrValue(int value) : storage_(static_cast<std::int64_t>(value)) {}
ZephyrValue::ZephyrValue(double value) : storage_(value) {}
ZephyrValue::ZephyrValue(std::string value) : storage_(std::move(value)) {}
ZephyrValue::ZephyrValue(const char* value) : storage_(std::string(value)) {}
ZephyrValue::ZephyrValue(Array value) : storage_(std::move(value)) {}
ZephyrValue::ZephyrValue(const ZephyrRecord& value) : storage_(std::make_shared<ZephyrRecord>(value)) {}
ZephyrValue::ZephyrValue(const ZephyrEnumValue& value) : storage_(std::make_shared<ZephyrEnumValue>(value)) {}
ZephyrValue::ZephyrValue(const ZephyrHostObjectRef& value) : storage_(std::make_shared<ZephyrHostObjectRef>(value)) {}

ZephyrValue::Kind ZephyrValue::kind() const {
    switch (storage_.index()) {
        case 0: return Kind::Nil;
        case 1: return Kind::Bool;
        case 2: return Kind::Int;
        case 3: return Kind::Float;
        case 4: return Kind::String;
        case 5: return Kind::Array;
        case 6: return Kind::Record;
        case 7: return Kind::Enum;
        case 8: return Kind::HostObject;
        default: return Kind::Nil;
    }
}

bool ZephyrValue::is_nil() const { return kind() == Kind::Nil; }
bool ZephyrValue::is_bool() const { return kind() == Kind::Bool; }
bool ZephyrValue::is_int() const { return kind() == Kind::Int; }
bool ZephyrValue::is_float() const { return kind() == Kind::Float; }
bool ZephyrValue::is_string() const { return kind() == Kind::String; }
bool ZephyrValue::is_array() const { return kind() == Kind::Array; }
bool ZephyrValue::is_record() const { return kind() == Kind::Record; }
bool ZephyrValue::is_enum() const { return kind() == Kind::Enum; }
bool ZephyrValue::is_host_object() const { return kind() == Kind::HostObject; }

bool ZephyrValue::as_bool() const { return expect_variant<bool>(storage_, "bool"); }
std::int64_t ZephyrValue::as_int() const { return expect_variant<std::int64_t>(storage_, "int"); }
double ZephyrValue::as_float() const {
    if (const auto* floating = std::get_if<double>(&storage_)) {
        return *floating;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&storage_)) {
        return static_cast<double>(*integer);
    }
    fail("ZephyrValue is not numeric.");
}
const std::string& ZephyrValue::as_string() const { return expect_variant<std::string>(storage_, "string"); }
const ZephyrValue::Array& ZephyrValue::as_array() const { return expect_variant<Array>(storage_, "Array"); }
const ZephyrRecord& ZephyrValue::as_record() const { return *expect_variant<std::shared_ptr<ZephyrRecord>>(storage_, "Record"); }
const ZephyrEnumValue& ZephyrValue::as_enum() const { return *expect_variant<std::shared_ptr<ZephyrEnumValue>>(storage_, "Enum"); }
const ZephyrHostObjectRef& ZephyrValue::as_host_object() const { return *expect_variant<std::shared_ptr<ZephyrHostObjectRef>>(storage_, "HostObject"); }
std::string ZephyrValue::describe() const { return to_string(*this); }

ZephyrHostClass::ZephyrHostClass(std::string name) : name_(std::move(name)) {}
const std::string& ZephyrHostClass::name() const { return name_; }
void ZephyrHostClass::add_method(std::string name, Method method) {
    const auto it = methods_index_.find(name);
    if (it != methods_index_.end()) {
        methods_list_[it->second].fn = std::move(method);
        return;
    }
    methods_index_[name] = static_cast<std::uint32_t>(methods_list_.size());
    methods_list_.push_back({std::move(name), std::move(method)});
}
void ZephyrHostClass::add_property(std::string name, Getter getter, Setter setter) {
    const auto it = properties_index_.find(name);
    if (it != properties_index_.end()) {
        properties_list_[it->second].getter = std::move(getter);
        properties_list_[it->second].setter = std::move(setter);
        return;
    }
    properties_index_[name] = static_cast<std::uint32_t>(properties_list_.size());
    properties_list_.push_back({std::move(name), std::move(getter), std::move(setter)});
}
bool ZephyrHostClass::has_method(const std::string& name) const { return methods_index_.contains(name); }
bool ZephyrHostClass::has_property(const std::string& name) const { return properties_index_.contains(name); }
const ZephyrHostClass::Method* ZephyrHostClass::find_method(const std::string& name) const {
    const auto it = methods_index_.find(name);
    return it == methods_index_.end() ? nullptr : &methods_list_[it->second].fn;
}
const ZephyrHostClass::Getter* ZephyrHostClass::find_getter(const std::string& name) const {
    const auto it = properties_index_.find(name);
    if (it == properties_index_.end() || !properties_list_[it->second].getter) return nullptr;
    return &properties_list_[it->second].getter;
}
const ZephyrHostClass::Setter* ZephyrHostClass::find_setter(const std::string& name) const {
    const auto it = properties_index_.find(name);
    if (it == properties_index_.end() || !properties_list_[it->second].setter) return nullptr;
    return &properties_list_[it->second].setter;
}
const ZephyrHostClass::Method* ZephyrHostClass::find_method_ic(const std::string& name, std::uint32_t& out_index) const {
    const auto it = methods_index_.find(name);
    if (it == methods_index_.end()) return nullptr;
    out_index = it->second;
    return &methods_list_[it->second].fn;
}
const ZephyrHostClass::Getter* ZephyrHostClass::find_getter_ic(const std::string& name, std::uint32_t& out_index) const {
    const auto it = properties_index_.find(name);
    if (it == properties_index_.end() || !properties_list_[it->second].getter) return nullptr;
    out_index = it->second;
    return &properties_list_[it->second].getter;
}
const ZephyrHostClass::Method* ZephyrHostClass::get_method_at(std::uint32_t index) const {
    if (index >= methods_list_.size()) return nullptr;
    return &methods_list_[index].fn;
}
const ZephyrHostClass::Getter* ZephyrHostClass::get_getter_at(std::uint32_t index) const {
    if (index >= properties_list_.size() || !properties_list_[index].getter) return nullptr;
    return &properties_list_[index].getter;
}
ZephyrValue ZephyrHostClass::invoke(void* instance, const std::string& method_name, const std::vector<ZephyrValue>& args) const {
    const auto it = methods_index_.find(method_name);
    if (it == methods_index_.end()) fail("Unknown host method: " + method_name);
    return methods_list_[it->second].fn(instance, args);
}
ZephyrValue ZephyrHostClass::get(void* instance, const std::string& property_name) const {
    const auto it = properties_index_.find(property_name);
    if (it == properties_index_.end() || !properties_list_[it->second].getter) {
        fail("Unknown host property getter: " + property_name);
    }
    return properties_list_[it->second].getter(instance);
}
void ZephyrHostClass::set(void* instance, const std::string& property_name, const ZephyrValue& value) const {
    const auto it = properties_index_.find(property_name);
    if (it == properties_index_.end() || !properties_list_[it->second].setter) {
        fail("Unknown host property setter: " + property_name);
    }
    properties_list_[it->second].setter(instance, value);
}

bool ZephyrFunctionHandle::valid() const { return !module_name.empty() && !function_name.empty(); }
bool ZephyrScriptCallbackHandle::valid() const { return id != 0; }
bool ZephyrCoroutineHandle::valid() const { return id != 0; }

ZephyrModuleBinder::ZephyrModuleBinder() = default;
ZephyrModuleBinder::ZephyrModuleBinder(void* runtime, void* environment, std::vector<std::string>* exports)
    : runtime_(runtime), environment_(environment), exports_(exports) {}
ZephyrModuleBinder::~ZephyrModuleBinder() = default;
ZephyrModuleBinder::ZephyrModuleBinder(ZephyrModuleBinder&& other) noexcept = default;
ZephyrModuleBinder& ZephyrModuleBinder::operator=(ZephyrModuleBinder&& other) noexcept = default;

void ZephyrModuleBinder::add_function(const std::string& name, ZephyrNativeFunction function, std::vector<std::string> param_types,
                                      std::string return_type) {
    auto* runtime = static_cast<Runtime*>(runtime_);
    auto* environment = static_cast<Environment*>(environment_);
    if (runtime == nullptr || environment == nullptr || exports_ == nullptr) {
        fail("ZephyrModuleBinder is not active.");
    }
    auto* native = runtime->allocate<NativeFunctionObject>(
        name, std::move(function), std::move(param_types),
        return_type.empty() ? std::optional<std::string>{} : std::optional<std::string>(return_type));
    runtime->native_callback_registry_.push_back(native);
    runtime->define_value(
        environment,
        name,
        Value::object(native),
        false,
        std::string("Function"));
    exports_->push_back(name);
}

void ZephyrModuleBinder::add_constant(const std::string& name, const ZephyrValue& value) {
    auto* runtime = static_cast<Runtime*>(runtime_);
    auto* environment = static_cast<Environment*>(environment_);
    if (runtime == nullptr || environment == nullptr || exports_ == nullptr) {
        fail("ZephyrModuleBinder is not active.");
    }
    runtime->define_value(environment, name, runtime->from_public_value(value), false);
    exports_->push_back(name);
}

struct ZephyrVM::Impl {
    explicit Impl(ZephyrVMConfig vm_config) : runtime(vm_config), config(std::move(vm_config)) {}

    Runtime runtime;
    ZephyrVMConfig config;
    std::unordered_map<std::type_index, std::shared_ptr<ZephyrHostClass>> bound_host_classes;
    std::mutex dap_mutex;
    std::thread dap_thread;
    std::vector<ZephyrBreakpoint> breakpoints;
    std::atomic<bool> dap_running{false};
    std::uint16_t dap_port = 0;
#ifdef _WIN32
    SOCKET dap_listen_socket = INVALID_SOCKET;
#endif
};

ZephyrVM::ZephyrVM() : ZephyrVM(ZephyrVMConfig{}) {}
ZephyrVM::ZephyrVM(ZephyrVMConfig config) : impl_(std::make_unique<Impl>(config)) { install_core(); }
ZephyrVM::~ZephyrVM() { stop_dap_server(); }
ZephyrVM::ZephyrVM(ZephyrVM&& other) noexcept = default;
ZephyrVM& ZephyrVM::operator=(ZephyrVM&& other) noexcept = default;

void ZephyrVM::register_module(const std::string& module_name, const std::function<void(ZephyrModuleBinder&)>& initializer) {
    impl_->runtime.register_module(module_name, initializer);
}

void ZephyrVM::register_global_function(const std::string& name, ZephyrNativeFunction function, std::vector<std::string> param_types,
                                        std::string return_type) {
    impl_->runtime.register_global_function(name, std::move(function), std::move(param_types), std::move(return_type));
}

std::shared_ptr<ZephyrHostClass> ZephyrVM::bind_host_class(std::type_index type, std::string_view class_name) {
    auto& entry = impl_->bound_host_classes[type];
    if (!entry) {
        entry = std::make_shared<ZephyrHostClass>(std::string(class_name));
    }
    return entry;
}

std::shared_ptr<ZephyrHostClass> ZephyrVM::lookup_bound_host_class(std::type_index type) const {
    const auto it = impl_->bound_host_classes.find(type);
    return it == impl_->bound_host_classes.end() ? nullptr : it->second;
}

namespace {

std::string dap_escape_json(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

#ifdef _WIN32
std::string dap_make_response(const std::string& body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string dap_make_breakpoints_body(const std::vector<ZephyrBreakpoint>& breakpoints) {
    std::ostringstream body;
    body << "{\"type\":\"response\",\"success\":true,\"body\":{\"breakpoints\":[";
    for (std::size_t index = 0; index < breakpoints.size(); ++index) {
        if (index != 0) {
            body << ',';
        }
        body << "{\"verified\":true,\"line\":" << breakpoints[index].line << "}";
    }
    body << "]}}";
    return body.str();
}

std::string dap_dispatch_request(const std::string& request, const std::vector<ZephyrBreakpoint>& breakpoints) {
    if (request.find("\"command\":\"initialize\"") != std::string::npos) {
        return dap_make_response(
            "{\"type\":\"response\",\"success\":true,\"body\":{\"supportsConfigurationDoneRequest\":true,\"supportsStepBack\":false}}");
    }
    if (request.find("\"command\":\"setBreakpoints\"") != std::string::npos) {
        return dap_make_response(dap_make_breakpoints_body(breakpoints));
    }
    if (request.find("\"command\":\"stackTrace\"") != std::string::npos) {
        return dap_make_response("{\"type\":\"response\",\"success\":true,\"body\":{\"stackFrames\":[],\"totalFrames\":0}}");
    }
    if (request.find("\"command\":\"scopes\"") != std::string::npos) {
        return dap_make_response("{\"type\":\"response\",\"success\":true,\"body\":{\"scopes\":[]}}");
    }
    if (request.find("\"command\":\"variables\"") != std::string::npos) {
        return dap_make_response("{\"type\":\"response\",\"success\":true,\"body\":{\"variables\":[]}}");
    }
    if (request.find("\"command\":\"continue\"") != std::string::npos || request.find("\"command\":\"next\"") != std::string::npos ||
        request.find("\"command\":\"launch\"") != std::string::npos || request.find("\"command\":\"attach\"") != std::string::npos ||
        request.find("\"command\":\"configurationDone\"") != std::string::npos) {
        return dap_make_response("{\"type\":\"response\",\"success\":true,\"body\":{}}");
    }
    return dap_make_response("{\"type\":\"response\",\"success\":true,\"body\":{}}");
}
#endif

}  // namespace

void ZephyrVM::install_core() { impl_->runtime.install_core(); }
void ZephyrVM::start_profiling() { impl_->runtime.start_profiling(); }
ZephyrProfileReport ZephyrVM::stop_profiling() { return impl_->runtime.stop_profiling(); }
void ZephyrVM::start_gc_trace() { impl_->runtime.start_gc_trace(); }
void ZephyrVM::stop_gc_trace() { impl_->runtime.stop_gc_trace(); }
bool ZephyrVM::is_gc_trace_active() const { return impl_->runtime.is_gc_trace_active(); }
std::string ZephyrVM::get_gc_trace_json() const { return impl_->runtime.get_gc_trace_json(); }
void ZephyrVM::start_coroutine_trace() { impl_->runtime.start_coroutine_trace(); }
void ZephyrVM::stop_coroutine_trace() { impl_->runtime.stop_coroutine_trace(); }
void ZephyrVM::enable_bytecode_cache(bool enabled) { impl_->runtime.enable_bytecode_cache(enabled); }
void ZephyrVM::clear_bytecode_cache() { impl_->runtime.clear_bytecode_cache(); }
std::size_t ZephyrVM::bytecode_cache_size() const { return impl_->runtime.bytecode_cache_size(); }
void ZephyrVM::add_module_search_path(const std::string& path) { impl_->runtime.add_module_search_path(path); }
std::vector<std::string> ZephyrVM::get_module_search_paths() const { return impl_->runtime.get_module_search_paths(); }
void ZephyrVM::clear_module_search_paths() { impl_->runtime.clear_module_search_paths(); }
void ZephyrVM::set_package_root(const std::string& path) { impl_->runtime.set_package_root(path); }
void ZephyrVM::start_dap_server(std::uint16_t port) {
#ifdef _WIN32
    stop_dap_server();

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        fail("Failed to initialize WinSock for DAP server.");
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        WSACleanup();
        fail("Failed to create DAP listen socket.");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(listen_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(listen_socket);
        WSACleanup();
        fail("Failed to bind DAP listen socket.");
    }
    if (listen(listen_socket, 1) == SOCKET_ERROR) {
        closesocket(listen_socket);
        WSACleanup();
        fail("Failed to listen on DAP socket.");
    }

    u_long non_blocking = 1;
    ioctlsocket(listen_socket, FIONBIO, &non_blocking);
    impl_->dap_listen_socket = listen_socket;
    impl_->dap_port = port;
    impl_->dap_running.store(true);
    impl_->dap_thread = std::thread([impl = impl_.get()]() {
        while (impl->dap_running.load()) {
            sockaddr_in client_addr{};
            int client_len = sizeof(client_addr);
            SOCKET client = accept(impl->dap_listen_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client == INVALID_SOCKET) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
                continue;
            }

            std::string request;
            char buffer[2048];
            const int received = recv(client, buffer, sizeof(buffer), 0);
            if (received > 0) {
                request.assign(buffer, buffer + received);
                std::vector<ZephyrBreakpoint> breakpoints;
                {
                    std::lock_guard<std::mutex> lock(impl->dap_mutex);
                    breakpoints = impl->breakpoints;
                }
                const std::string response = dap_dispatch_request(request, breakpoints);
                send(client, response.data(), static_cast<int>(response.size()), 0);
            }
            shutdown(client, SD_BOTH);
            closesocket(client);
        }
    });
#else
    (void)port;
    fail("DAP server is only supported on Windows in this build.");
#endif
}

void ZephyrVM::stop_dap_server() {
#ifdef _WIN32
    impl_->dap_running.store(false);
    if (impl_->dap_listen_socket != INVALID_SOCKET) {
        shutdown(impl_->dap_listen_socket, SD_BOTH);
        closesocket(impl_->dap_listen_socket);
        impl_->dap_listen_socket = INVALID_SOCKET;
    }
    if (impl_->dap_thread.joinable()) {
        impl_->dap_thread.join();
    }
    WSACleanup();
#endif
}

void ZephyrVM::set_breakpoint(const ZephyrBreakpoint& bp) {
    std::lock_guard<std::mutex> lock(impl_->dap_mutex);
    impl_->runtime.set_breakpoint(bp);
    impl_->breakpoints.push_back(bp);
}

void ZephyrVM::clear_breakpoints() {
    std::lock_guard<std::mutex> lock(impl_->dap_mutex);
    impl_->runtime.clear_breakpoints();
    impl_->breakpoints.clear();
}

ZephyrSnapshot ZephyrVM::snapshot() const { return impl_->runtime.snapshot_state(); }

bool ZephyrVM::restore_snapshot(const ZephyrSnapshot& snap) { return impl_->runtime.restore_snapshot_state(snap); }

void ZephyrVM::check_string(const std::string& source, const std::string& module_name, const std::filesystem::path& base_dir) {
    auto result = impl_->runtime.check_source(source, module_name, base_dir);
    if (!result) {
        fail_runtime_error(result.error());
    }
}
void ZephyrVM::check_file(const std::filesystem::path& path) {
    auto result = impl_->runtime.check_file(path);
    if (!result) {
        fail_runtime_error(result.error());
    }
}
ZephyrValue ZephyrVM::execute_string(const std::string& source, const std::string& module_name, const std::filesystem::path& base_dir) {
    auto result = impl_->runtime.execute_string_module(source, module_name, base_dir);
    if (!result) {
        fail_runtime_error(result.error());
    }
    return impl_->runtime.to_public_value(*result);
}
ZephyrValue ZephyrVM::execute_file(const std::filesystem::path& path) {
    auto result = impl_->runtime.execute_file_module(path);
    if (!result) {
        fail_runtime_error(result.error());
    }
    return impl_->runtime.to_public_value(*result);
}
std::optional<ZephyrFunctionHandle> ZephyrVM::get_function(const std::string& module_name, const std::string& function_name) {
    return impl_->runtime.find_function(module_name, function_name);
}
ZephyrValue ZephyrVM::call(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args) {
    auto result = impl_->runtime.call_handle(handle, args);
    if (!result) {
        fail_runtime_error(result.error());
    }
    return impl_->runtime.to_public_value(*result);
}
ZephyrValue ZephyrVM::call(const ZephyrScriptCallbackHandle& handle, const std::vector<ZephyrValue>& args) {
    auto result = impl_->runtime.call_callback_handle(handle, args);
    if (!result) {
        fail_runtime_error(result.error());
    }
    return impl_->runtime.to_public_value(*result);
}
ZephyrValue ZephyrVM::call_serialized(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args) {
    auto result = impl_->runtime.call_handle(handle, args);
    if (!result) {
        fail_runtime_error(result.error());
    }
    auto serialized = impl_->runtime.serialize_runtime_value(*result, Span{}, handle.module_name.empty() ? "<serialize>" : handle.module_name);
    if (!serialized) {
        fail_runtime_error(serialized.error());
    }
    return std::move(*serialized);
}
ZephyrValue ZephyrVM::serialize_value(const ZephyrValue& value) {
    auto serialized = impl_->runtime.serialize_public_value(value);
    if (!serialized) {
        fail_runtime_error(serialized.error());
    }
    return std::move(*serialized);
}
ZephyrValue ZephyrVM::deserialize_value(const ZephyrValue& value) {
    auto deserialized = impl_->runtime.deserialize_public_value(value);
    if (!deserialized) {
        fail_runtime_error(deserialized.error());
    }
    return std::move(*deserialized);
}
std::string ZephyrVM::dump_bytecode(const std::string& module_name, const std::string& function_name) const {
    return impl_->runtime.dump_bytecode(module_name, function_name);
}
ZephyrValue ZephyrVM::make_host_object(std::shared_ptr<ZephyrHostClass> host_class, std::shared_ptr<void> instance) {
    return impl_->runtime.make_host_object(std::move(host_class), std::move(instance));
}
void ZephyrVM::invalidate_host_handle(const ZephyrHostObjectRef& handle) { impl_->runtime.invalidate_host_handle(handle); }

// ── Item 6: RAII value pinning ─────────────────────────────────────────────
std::uint32_t Runtime::pin_value(const ZephyrValue& value) {
    const std::uint32_t id = next_pin_id_++;
    pinned_values_[id] = value;
    return id;
}
void Runtime::unpin_value(std::uint32_t pin_id) {
    pinned_values_.erase(pin_id);
}

std::uint32_t ZephyrVM::pin_value(const ZephyrValue& value)  { return impl_->runtime.pin_value(value); }
void          ZephyrVM::unpin_value(std::uint32_t pin_id)    { impl_->runtime.unpin_value(pin_id); }

// ── Item 7: Handle Table Indirection ─────────────────────────────────────
ZephyrHostObjectRef* Runtime::resolve_host_handle(std::uint64_t handle_id) {
    if (handle_id == 0) return nullptr;
    for (auto& entry : host_handles_) {
        if (entry.handle_id == handle_id && !entry.invalid()) {
            auto inst = entry.instance.lock();
            if (!inst) return nullptr;
            static thread_local ZephyrHostObjectRef tl_ref;
            tl_ref.host_class       = entry.host_class;
            tl_ref.instance         = std::move(inst);
            tl_ref.kind             = entry.kind;
            tl_ref.lifetime         = entry.lifetime;
            tl_ref.strong_residency = (entry.flags & HostHandleStrongResidencyBit) != 0;
            tl_ref.stable_guid      = entry.stable_guid;
            tl_ref.policy           = entry.policy;
            tl_ref.has_explicit_policy = true;
            tl_ref.valid            = true;
            tl_ref.invalid_reason.clear();
            tl_ref.slot             = entry.runtime_slot;
            tl_ref.generation       = entry.generation;
            tl_ref.handle_id        = entry.handle_id;
            return &tl_ref;
        }
    }
    return nullptr;
}
ZephyrHostObjectRef* ZephyrVM::resolve_host_handle(std::uint64_t handle_id) {
    return impl_->runtime.resolve_host_handle(handle_id);
}

ZephyrScriptCallbackHandle ZephyrVM::capture_callback(const ZephyrFunctionHandle& handle) { return impl_->runtime.capture_callback(handle); }
void ZephyrVM::release_callback(const ZephyrScriptCallbackHandle& handle) { impl_->runtime.release_callback(handle); }
std::optional<ZephyrCoroutineHandle> ZephyrVM::spawn_coroutine(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args) {
    auto result = impl_->runtime.spawn_coroutine_handle(handle, args);
    if (!result) {
        fail_runtime_error(result.error());
    }
    return *result;
}
ZephyrValue ZephyrVM::resume(const ZephyrCoroutineHandle& handle) {
    auto result = impl_->runtime.resume_coroutine_handle(handle);
    if (!result) {
        fail_runtime_error(result.error());
    }
    return impl_->runtime.to_public_value(*result);
}
void ZephyrVM::cancel(const ZephyrCoroutineHandle& handle) { impl_->runtime.cancel_coroutine_handle(handle); }
std::optional<ZephyrCoroutineInfo> ZephyrVM::query_coroutine(const ZephyrCoroutineHandle& handle) const {
    return impl_->runtime.query_coroutine_handle(handle);
}
void ZephyrVM::gc_step(std::size_t budget_work) { impl_->runtime.gc_step(budget_work); }
void ZephyrVM::set_gc_stress(bool enabled, std::size_t budget_work) { impl_->runtime.set_gc_stress(enabled, budget_work); }
void ZephyrVM::advance_frame(std::size_t gc_budget_work) { impl_->runtime.advance_frame(gc_budget_work); }
void ZephyrVM::advance_tick(std::size_t gc_budget_work) { impl_->runtime.advance_tick(gc_budget_work); }
void ZephyrVM::advance_scene() { impl_->runtime.advance_scene(); }
void ZephyrVM::collect_young() { impl_->runtime.collect_young(); }
void ZephyrVM::collect_garbage() { impl_->runtime.collect_garbage(); }
void ZephyrVM::compact_old_generation() { impl_->runtime.compact_old_generation(); }
void ZephyrVM::gc_verify_young() {
    auto result = impl_->runtime.gc_verify_young();
    if (!result) {
        fail(result.error());
    }
}
void ZephyrVM::gc_verify_full() {
    auto result = impl_->runtime.gc_verify_full();
    if (!result) {
        fail(result.error());
    }
}
GCPauseStats ZephyrVM::get_gc_pause_stats() const { return impl_->runtime.get_gc_pause_stats(); }
ZephyrVM::RuntimeStats ZephyrVM::runtime_stats() const { return impl_->runtime.runtime_stats(); }
std::string ZephyrVM::debug_dump_coroutines() const { return impl_->runtime.debug_dump_coroutines(); }
const ZephyrVMConfig& ZephyrVM::config() const { return impl_->config; }

#ifdef DEBUG_LEAK_CHECK
ZephyrDebugGcStats debug_gc_stats() {
    const auto& state = debug_gc_leak_state();
    return ZephyrDebugGcStats{
        state.live_objects,
        state.total_allocations,
        state.peak_live_objects,
    };
}
#endif

std::string to_string(const ZephyrValue& value) {
    switch (value.kind()) {
        case ZephyrValue::Kind::Nil:
            return "nil";
        case ZephyrValue::Kind::Bool:
            return value.as_bool() ? "true" : "false";
        case ZephyrValue::Kind::Int:
            return std::to_string(value.as_int());
        case ZephyrValue::Kind::Float: {
            std::ostringstream out;
            out << value.as_float();
            return out.str();
        }
        case ZephyrValue::Kind::String:
            return value.as_string();
        case ZephyrValue::Kind::Array: {
            std::ostringstream out;
            out << "[";
            const auto& array = value.as_array();
            for (std::size_t i = 0; i < array.size(); ++i) {
                if (i > 0) {
                    out << ", ";
                }
                out << to_string(array[i]);
            }
            out << "]";
            return out.str();
        }
        case ZephyrValue::Kind::Record: {
            const auto& record = value.as_record();
            std::ostringstream out;
            out << record.type_name << " { ";
            bool first = true;
            for (const auto& field : record.fields) {
                if (!first) {
                    out << ", ";
                }
                first = false;
                out << field.first << ": " << to_string(field.second);
            }
            out << " }";
            return out.str();
        }
        case ZephyrValue::Kind::Enum: {
            const auto& enum_value = value.as_enum();
            std::ostringstream out;
            out << enum_value.type_name << "::" << enum_value.variant_name;
            if (!enum_value.payload.empty()) {
                out << "(";
                for (std::size_t i = 0; i < enum_value.payload.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << to_string(enum_value.payload[i]);
                }
                out << ")";
            }
            return out.str();
        }
        case ZephyrValue::Kind::HostObject:
            return "<host:" + value.as_host_object().host_class->name() + ">";
    }
    return "<value>";
}

}  // namespace zephyr
