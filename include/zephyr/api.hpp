#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace zephyr {

struct ZephyrRecord;
struct ZephyrEnumValue;
class ZephyrHostClass;
class ZephyrModuleBinder;
template <typename T>
class ZephyrClassBinder;

class ZephyrRuntimeError : public std::runtime_error {
public:
    explicit ZephyrRuntimeError(std::string message, std::string stack_trace = {})
        : std::runtime_error(stack_trace.empty() ? message : message + "\n" + stack_trace),
          message_(std::move(message)),
          stack_trace(std::move(stack_trace)) {}

    const std::string& message() const noexcept { return message_; }

    std::string stack_trace;

private:
    std::string message_;
};

enum class ZephyrInvalidAccessMode {
    SoftInvalid,
    Trap,
};

enum class ZephyrHostHandleLifetime {
    Frame,
    Tick,
    Persistent,
    Stable,
};

enum class ZephyrHostHandleKind : std::uint16_t {
    Generic,
    Entity,
    Component,
    Scene,
    Texture,
    Mesh,
    Material,
    Asset,
};

enum class ZephyrGcPhase {
    Idle,
    PrepareCycle,
    SeedRoots,
    DrainGray,
    RescanDirtyRoots,
    SweepObjects,
    DetachQueue,
    Complete,
};

struct ZephyrGuid128 {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    bool valid() const;
};

bool operator==(const ZephyrGuid128& left, const ZephyrGuid128& right);

struct ZephyrGcConfig {
    std::size_t incremental_budget_work = 128;
    std::size_t incremental_trigger_bytes = 64 * 1024;
    std::size_t nursery_trigger_bytes = 32 * 1024;
    std::size_t promotion_survival_threshold = 2;
    std::size_t large_object_threshold_bytes = 4 * 1024;
    std::size_t young_collection_frequency = 3;
    bool adaptive_nursery = true;  // Phase 3.5: auto-tune nursery_trigger_bytes based on survival rate
};

struct ZephyrHandleConfig {
    ZephyrInvalidAccessMode debug_invalid_access_mode = ZephyrInvalidAccessMode::Trap;
    ZephyrInvalidAccessMode release_invalid_access_mode = ZephyrInvalidAccessMode::SoftInvalid;
    bool stable_handles_require_guid = true;
    bool trap_invalid_gpu_resources = true;
    bool default_entity_handles_are_weak = true;
};

struct ZephyrDebugVerifyConfig {
    bool enable_gc_verify = true;
    bool enable_handle_audit = true;
    bool enable_barrier_audit = true;
    bool enable_root_audit = true;
};

struct ZephyrBenchmarkConfig {
    std::size_t warmup_iterations = 5;
    std::size_t measure_iterations = 20;
    bool emit_opcode_histogram = true;
    bool emit_gc_histogram = true;
};

struct ZephyrVMConfig {
    ZephyrGcConfig gc{};
    ZephyrHandleConfig handle{};
    ZephyrDebugVerifyConfig debug_verify{};
    ZephyrBenchmarkConfig benchmark{};
};

struct ZephyrHostHandlePolicy {
    ZephyrInvalidAccessMode debug_mode = ZephyrInvalidAccessMode::Trap;
    ZephyrInvalidAccessMode release_mode = ZephyrInvalidAccessMode::SoftInvalid;
    bool allow_field_store = true;
    bool allow_closure_capture = true;
    bool allow_coroutine_capture = true;
    bool allow_serialize = false;
    bool allow_cross_scene = false;
    bool strong_residency_allowed = false;
    bool weak_by_default = true;
};

struct ZephyrHostObjectRef {
    std::shared_ptr<ZephyrHostClass> host_class;
    std::shared_ptr<void> instance;
    ZephyrHostHandleKind kind = ZephyrHostHandleKind::Generic;
    ZephyrHostHandleLifetime lifetime = ZephyrHostHandleLifetime::Persistent;
    bool strong_residency = false;
    ZephyrGuid128 stable_guid{};
    ZephyrHostHandlePolicy policy{};
    bool has_explicit_policy = false;
    bool valid = true;
    std::string invalid_reason;
    std::uint32_t slot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;
};

class ZephyrValue {
public:
    enum class Kind {
        Nil,
        Bool,
        Int,
        Float,
        String,
        Array,
        Record,
        Enum,
        HostObject,
    };

    using Array = std::vector<ZephyrValue>;

    ZephyrValue();
    ZephyrValue(std::nullptr_t);
    ZephyrValue(bool value);
    ZephyrValue(std::int64_t value);
    ZephyrValue(int value);
    ZephyrValue(double value);
    ZephyrValue(std::string value);
    ZephyrValue(const char* value);
    ZephyrValue(Array value);
    ZephyrValue(const ZephyrRecord& value);
    ZephyrValue(const ZephyrEnumValue& value);
    ZephyrValue(const ZephyrHostObjectRef& value);

    Kind kind() const;
    bool is_nil() const;
    bool is_bool() const;
    bool is_int() const;
    bool is_float() const;
    bool is_string() const;
    bool is_array() const;
    bool is_record() const;
    bool is_enum() const;
    bool is_host_object() const;

    bool as_bool() const;
    std::int64_t as_int() const;
    double as_float() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const ZephyrRecord& as_record() const;
    const ZephyrEnumValue& as_enum() const;
    const ZephyrHostObjectRef& as_host_object() const;

    std::string describe() const;

private:
    using Storage = std::variant<
        std::monostate,
        bool,
        std::int64_t,
        double,
        std::string,
        Array,
        std::shared_ptr<ZephyrRecord>,
        std::shared_ptr<ZephyrEnumValue>,
        std::shared_ptr<ZephyrHostObjectRef>>;

    Storage storage_;
};

struct ZephyrRecord {
    std::string type_name;
    std::map<std::string, ZephyrValue> fields;
};

struct ZephyrEnumValue {
    std::string type_name;
    std::string variant_name;
    std::vector<ZephyrValue> payload;
};

using ZephyrNativeFunction = std::function<ZephyrValue(const std::vector<ZephyrValue>& args)>;

class ZephyrHostClass {
public:
    using Method = std::function<ZephyrValue(void* instance, const std::vector<ZephyrValue>& args)>;
    using Getter = std::function<ZephyrValue(void* instance)>;
    using Setter = std::function<void(void* instance, const ZephyrValue& value)>;

    explicit ZephyrHostClass(std::string name);

    const std::string& name() const;

    void add_method(std::string name, Method method);
    void add_property(std::string name, Getter getter, Setter setter = {});

    bool has_method(const std::string& name) const;
    bool has_property(const std::string& name) const;
    const Method* find_method(const std::string& name) const;
    const Getter* find_getter(const std::string& name) const;
    const Setter* find_setter(const std::string& name) const;

    ZephyrValue invoke(void* instance, const std::string& method_name, const std::vector<ZephyrValue>& args) const;
    ZephyrValue get(void* instance, const std::string& property_name) const;
    void set(void* instance, const std::string& property_name, const ZephyrValue& value) const;

private:
    struct Property {
        Getter getter;
        Setter setter;
    };

    std::string name_;
    std::unordered_map<std::string, Method> methods_;
    std::unordered_map<std::string, Property> properties_;
};

struct ZephyrFunctionHandle {
    std::string module_name;
    std::string function_name;

    bool valid() const;
};

struct ZephyrScriptCallbackHandle {
    std::uint64_t id = 0;
    std::string module_name;
    std::string function_name;

    bool valid() const;
};

struct ZephyrCoroutineHandle {
    std::uint64_t id = 0;

    bool valid() const;
};

struct ZephyrCoroutineInfo {
    bool started = false;
    bool suspended = false;
    bool completed = false;
    std::size_t frame_depth = 0;
    std::size_t stack_values = 0;
    std::size_t local_slots = 0;
    std::size_t resume_count = 0;
    std::size_t yield_count = 0;
};

struct ZephyrBreakpoint {
    std::string source_file;
    std::uint32_t line = 0;
};

struct ZephyrSnapshot {
    std::vector<std::uint8_t> data;
    std::string version;
    std::uint64_t timestamp_utc = 0;
};

struct ZephyrProfileEntry {
    std::string function_name;
    std::uint64_t call_count = 0;
    std::uint64_t total_ns = 0;
    std::uint64_t self_ns = 0;
};

struct CoroutineTraceEvent {
    enum class Type { Created, Resumed, Yielded, Completed, Destroyed };
    Type type = Type::Created;
    std::uint64_t coroutine_id = 0;
    std::uint64_t timestamp_ns = 0;
};

struct ZephyrProfileReport {
    std::vector<ZephyrProfileEntry> entries;
    std::vector<CoroutineTraceEvent> coroutine_trace;
};

struct GCPauseStats {
    std::uint64_t p50_ns = 0;
    std::uint64_t p95_ns = 0;
    std::uint64_t p99_ns = 0;
    std::uint64_t frame_budget_miss_count = 0;
};

struct GCTraceEvent {
    enum class Type { YoungStart, YoungEnd, FullStart, FullEnd };
    Type type = Type::YoungStart;
    std::uint64_t timestamp_ns = 0;
    std::size_t heap_bytes_before = 0;
    std::size_t heap_bytes_after = 0;
};

// Per-space statistics for the four-space GC heap.
// live_bytes and object_count are populated by runtime_stats().
struct ZephyrSpaceStats {
    std::size_t live_bytes     = 0;
    std::size_t used_bytes     = 0;
    std::size_t reserved_bytes = 0;
    std::size_t object_count   = 0;
};

struct ZephyrGcStats {
    ZephyrGcPhase phase = ZephyrGcPhase::Idle;
    std::size_t live_objects = 0;
    std::size_t live_bytes = 0;
    std::size_t young_objects = 0;
    std::size_t young_bytes = 0;
    std::size_t old_objects = 0;
    std::size_t old_bytes = 0;
    std::size_t large_objects = 0;
    std::size_t large_object_bytes = 0;
    std::size_t gray_objects = 0;
    std::size_t dirty_root_environments = 0;
    std::size_t dirty_objects = 0;
    std::size_t detach_queue_objects = 0;
    std::size_t remembered_objects = 0;
    std::size_t remembered_cards = 0;
    std::size_t remembered_card_fast_prunes = 0;
    std::size_t total_allocations = 0;
    std::size_t total_gc_steps = 0;
    std::size_t total_gc_cycles = 0;
    std::size_t total_minor_gc_cycles = 0;
    std::size_t total_major_gc_cycles = 0;
    std::size_t total_promotions = 0;
    std::size_t total_compactions = 0;
    std::size_t total_young_collections = 0;
    std::size_t total_full_collections = 0;
    std::size_t total_gc_verifications = 0;
    std::size_t total_gc_stress_safe_points = 0;
    std::size_t barrier_hits = 0;
    bool gc_stress_enabled = false;
    std::size_t gc_stress_budget = 0;
    // Per-space stats — live_bytes and object_count populated by runtime_stats().
    ZephyrSpaceStats nursery{};
    ZephyrSpaceStats old_small{};
    ZephyrSpaceStats large_object{};
    ZephyrSpaceStats pinned{};
    std::size_t heap_fragmentation_pct = 0;
};

struct ZephyrVmStats {
    std::size_t opcode_count = 0;
    std::uint64_t superinstruction_fusions = 0;
    std::size_t ast_fallback_executions = 0;
    std::size_t lightweight_calls = 0;       // calls via lightweight (no-env) path (Phase 1.1)
    std::size_t string_intern_hits = 0;
    std::size_t string_intern_misses = 0;
    std::size_t local_binding_cache_hits = 0;
    std::size_t local_binding_cache_misses = 0;
    std::size_t global_binding_cache_hits = 0;
    std::size_t global_binding_cache_misses = 0;
    std::size_t line_table_entries = 0;
    std::size_t constant_pool_entries = 0;
    std::size_t module_count = 0;
    std::size_t function_count = 0;
    std::size_t callback_count = 0;
    std::size_t callback_invocations = 0;
    std::size_t serialized_value_exports = 0;
    std::size_t deserialized_value_imports = 0;
    std::size_t total_original_opcode_count = 0;
};

struct ZephyrCoroutineStats {
    std::size_t coroutine_objects = 0;
    std::size_t suspended_coroutines = 0;
    std::size_t completed_coroutines = 0;
    std::size_t active_coroutines = 0;
    std::size_t total_coroutine_frames = 0;
    std::size_t max_coroutine_frame_depth = 0;
    std::size_t total_coroutine_stack_values = 0;
    std::size_t total_coroutine_stack_capacity = 0;
    std::size_t total_coroutine_local_slots = 0;
    std::size_t total_coroutine_local_capacity = 0;
    std::size_t max_coroutine_stack_values = 0;
    std::size_t max_coroutine_local_slots = 0;
    std::size_t coroutine_compactions = 0;
    std::size_t coroutine_compacted_frames = 0;
    std::size_t coroutine_compacted_capacity = 0;
    std::size_t total_coroutine_resume_calls = 0;
    std::size_t total_coroutine_yields = 0;
    std::size_t total_coroutine_steps = 0;
    std::size_t max_coroutine_steps = 0;
    std::size_t max_coroutine_resume_steps = 0;
};

struct ZephyrHandleStats {
    std::size_t invalid_handle_faults = 0;
    std::size_t host_handle_slots = 0;
    std::size_t frame_handles = 0;
    std::size_t tick_handles = 0;
    std::size_t persistent_handles = 0;
    std::size_t stable_handles = 0;
    std::size_t strong_residency_handles = 0;
    std::size_t resolve_count = 0;
    std::size_t resolve_failures = 0;
    std::size_t stable_resolve_hits = 0;
    std::size_t stable_resolve_misses = 0;
};

struct ZephyrRuntimeStats {
    ZephyrGcStats gc{};
    ZephyrVmStats vm{};
    ZephyrCoroutineStats coroutine{};
    ZephyrHandleStats handle{};
    std::uint32_t frame_epoch = 0;
    std::uint32_t tick_epoch = 0;
    std::uint32_t scene_epoch = 0;

    // Compatibility mirrors for existing v1-style tests and tooling.
    ZephyrGcPhase gc_phase = ZephyrGcPhase::Idle;
    std::size_t live_objects = 0;
    std::size_t live_bytes = 0;
    std::size_t gray_objects = 0;
    std::size_t dirty_root_environments = 0;
    std::size_t dirty_objects = 0;
    std::size_t detach_queue_objects = 0;
    std::size_t remembered_objects = 0;
    std::size_t total_allocations = 0;
    std::size_t total_gc_steps = 0;
    std::size_t total_gc_cycles = 0;
    std::size_t total_gc_verifications = 0;
    std::size_t total_gc_stress_safe_points = 0;
    std::size_t barrier_hits = 0;
    std::size_t invalid_handle_faults = 0;
    std::size_t host_handle_slots = 0;
    std::size_t coroutine_objects = 0;
    std::size_t suspended_coroutines = 0;
    std::size_t completed_coroutines = 0;
    std::size_t active_coroutines = 0;
    std::size_t total_coroutine_frames = 0;
    std::size_t max_coroutine_frame_depth = 0;
    std::size_t total_coroutine_stack_values = 0;
    std::size_t total_coroutine_stack_capacity = 0;
    std::size_t total_coroutine_local_slots = 0;
    std::size_t total_coroutine_local_capacity = 0;
    std::size_t max_coroutine_stack_values = 0;
    std::size_t max_coroutine_local_slots = 0;
    std::size_t coroutine_compactions = 0;
    std::size_t coroutine_compacted_frames = 0;
    std::size_t coroutine_compacted_capacity = 0;
    std::size_t total_coroutine_resume_calls = 0;
    std::size_t total_coroutine_yields = 0;
    std::size_t total_coroutine_steps = 0;
    std::size_t max_coroutine_steps = 0;
    std::size_t max_coroutine_resume_steps = 0;
    bool gc_stress_enabled = false;
    std::size_t gc_stress_budget = 0;
};

class ZephyrModuleBinder {
public:
    ZephyrModuleBinder();
    ZephyrModuleBinder(void* runtime, void* environment, std::vector<std::string>* exports);
    ~ZephyrModuleBinder();
    ZephyrModuleBinder(ZephyrModuleBinder&& other) noexcept;
    ZephyrModuleBinder& operator=(ZephyrModuleBinder&& other) noexcept;

    ZephyrModuleBinder(const ZephyrModuleBinder&) = delete;
    ZephyrModuleBinder& operator=(const ZephyrModuleBinder&) = delete;

    void add_function(
        const std::string& name,
        ZephyrNativeFunction function,
        std::vector<std::string> param_types = {},
        std::string return_type = {});

    void add_constant(const std::string& name, const ZephyrValue& value);

private:
    void* runtime_ = nullptr;
    void* environment_ = nullptr;
    std::vector<std::string>* exports_ = nullptr;

    friend class ZephyrVM;
};

class ZephyrVM {
public:
    using RuntimeStats = ZephyrRuntimeStats;

    ZephyrVM();
    explicit ZephyrVM(ZephyrVMConfig config);
    ~ZephyrVM();

    ZephyrVM(ZephyrVM&& other) noexcept;
    ZephyrVM& operator=(ZephyrVM&& other) noexcept;

    ZephyrVM(const ZephyrVM&) = delete;
    ZephyrVM& operator=(const ZephyrVM&) = delete;

    void register_module(const std::string& module_name, const std::function<void(ZephyrModuleBinder&)>& initializer);
    void register_global_function(
        const std::string& name,
        ZephyrNativeFunction function,
        std::vector<std::string> param_types = {},
        std::string return_type = {});

    void install_core();

    void check_string(
        const std::string& source,
        const std::string& module_name = "<script>",
        const std::filesystem::path& base_dir = ".");
    void check_file(const std::filesystem::path& path);

    ZephyrValue execute_string(
        const std::string& source,
        const std::string& module_name = "<script>",
        const std::filesystem::path& base_dir = ".");
    ZephyrValue execute_file(const std::filesystem::path& path);

    std::optional<ZephyrFunctionHandle> get_function(const std::string& module_name, const std::string& function_name);
    ZephyrValue call(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args = {});
    ZephyrValue call(const ZephyrScriptCallbackHandle& handle, const std::vector<ZephyrValue>& args = {});
    ZephyrValue call_serialized(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args = {});
    ZephyrValue serialize_value(const ZephyrValue& value);
    ZephyrValue deserialize_value(const ZephyrValue& value);
    std::string dump_bytecode(const std::string& module_name, const std::string& function_name = {}) const;

    ZephyrValue make_host_object(std::shared_ptr<ZephyrHostClass> host_class, std::shared_ptr<void> instance);

    template <typename T>
    ZephyrValue make_host_object(std::shared_ptr<T> instance) {
        auto host_class = lookup_bound_host_class(std::type_index(typeid(T)));
        if (!host_class) {
            throw std::runtime_error("Type is not bound. Call bind<T>() before make_host_object(instance).");
        }
        return make_host_object(std::move(host_class), std::static_pointer_cast<void>(std::move(instance)));
    }

    template <typename T>
    ZephyrClassBinder<T> bind(std::string_view class_name);

    void start_profiling();
    ZephyrProfileReport stop_profiling();
    void start_coroutine_trace();
    void stop_coroutine_trace();
    void start_dap_server(std::uint16_t port = 4711);
    void stop_dap_server();
    void set_breakpoint(const ZephyrBreakpoint& bp);
    void clear_breakpoints();
    ZephyrSnapshot snapshot() const;
    bool restore_snapshot(const ZephyrSnapshot& snap);
    GCPauseStats get_gc_pause_stats() const;
    void start_gc_trace();
    void stop_gc_trace();
    bool is_gc_trace_active() const;
    std::string get_gc_trace_json() const;
    void enable_bytecode_cache(bool enabled = true);
    void clear_bytecode_cache();
    std::size_t bytecode_cache_size() const;
    void add_module_search_path(const std::string& path);
    std::vector<std::string> get_module_search_paths() const;
    void clear_module_search_paths();
    void set_package_root(const std::string& path);

    void invalidate_host_handle(const ZephyrHostObjectRef& handle);
    ZephyrScriptCallbackHandle capture_callback(const ZephyrFunctionHandle& handle);
    void release_callback(const ZephyrScriptCallbackHandle& handle);
    std::optional<ZephyrCoroutineHandle> spawn_coroutine(const ZephyrFunctionHandle& handle, const std::vector<ZephyrValue>& args = {});
    ZephyrValue resume(const ZephyrCoroutineHandle& handle);
    void cancel(const ZephyrCoroutineHandle& handle);
    std::optional<ZephyrCoroutineInfo> query_coroutine(const ZephyrCoroutineHandle& handle) const;
    void gc_step(std::size_t budget_work = 128);
    void set_gc_stress(bool enabled, std::size_t budget_work = 1);
    void advance_frame(std::size_t gc_budget_work = 128);
    void advance_tick(std::size_t gc_budget_work = 128);
    void advance_scene();
    void collect_young();
    void collect_garbage();
    /// Phase 7: compact old-generation objects — evacuate promoted bump objects
    /// into slab slots, freeing retained nursery chunks.  Call at Idle phase
    /// (typically after collect_garbage() or between execute() calls).
    void compact_old_generation();
    void gc_verify_young();
    void gc_verify_full();

    RuntimeStats runtime_stats() const;
    std::string debug_dump_coroutines() const;
    const ZephyrVMConfig& config() const;

private:
    std::shared_ptr<ZephyrHostClass> bind_host_class(std::type_index type, std::string_view class_name);
    std::shared_ptr<ZephyrHostClass> lookup_bound_host_class(std::type_index type) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;

    template <typename T>
    friend class ZephyrClassBinder;
};

namespace detail {

template <typename>
inline constexpr bool always_false_v = false;

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
ZephyrValue to_zephyr_value(T&& value) {
    using ValueType = remove_cvref_t<T>;
    if constexpr (std::is_same_v<ValueType, ZephyrValue>) {
        return std::forward<T>(value);
    } else if constexpr (std::is_same_v<ValueType, bool>) {
        return ZephyrValue(value);
    } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
        return ZephyrValue(static_cast<std::int64_t>(value));
    } else if constexpr (std::is_floating_point_v<ValueType>) {
        return ZephyrValue(static_cast<double>(value));
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
        return ZephyrValue(std::forward<T>(value));
    } else if constexpr (std::is_same_v<ValueType, const char*>) {
        return ZephyrValue(value);
    } else {
        static_assert(always_false_v<ValueType>, "Unsupported Zephyr binder return/property type.");
    }
}

template <typename T>
remove_cvref_t<T> from_zephyr_value(const ZephyrValue& value) {
    using ValueType = remove_cvref_t<T>;
    static_assert(!std::is_lvalue_reference_v<T> || std::is_const_v<std::remove_reference_t<T>>,
                  "Zephyr binder does not support non-const lvalue reference parameters.");
    if constexpr (std::is_same_v<ValueType, ZephyrValue>) {
        return value;
    } else if constexpr (std::is_same_v<ValueType, bool>) {
        return value.as_bool();
    } else if constexpr (std::is_integral_v<ValueType> && !std::is_same_v<ValueType, bool>) {
        return static_cast<ValueType>(value.as_int());
    } else if constexpr (std::is_floating_point_v<ValueType>) {
        return static_cast<ValueType>(value.as_float());
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
        return value.as_string();
    } else {
        static_assert(always_false_v<ValueType>, "Unsupported Zephyr binder argument/property type.");
    }
}

template <typename... Args, std::size_t... I>
auto make_argument_tuple(const std::vector<ZephyrValue>& args, std::index_sequence<I...>) {
    if (args.size() != sizeof...(Args)) {
        throw std::runtime_error("Member function received wrong argument count.");
    }
    return std::make_tuple(from_zephyr_value<Args>(args[I])...);
}

template <typename Ret, typename Instance, typename Method, typename... Args>
ZephyrValue invoke_member(Instance* instance, Method fn, const std::vector<ZephyrValue>& args) {
    auto converted_args = make_argument_tuple<Args...>(args, std::index_sequence_for<Args...>{});
    if constexpr (std::is_void_v<Ret>) {
        std::apply(
            [&](auto&&... unpacked_args) {
                (instance->*fn)(std::forward<decltype(unpacked_args)>(unpacked_args)...);
            },
            converted_args);
        return ZephyrValue();
    } else {
        return std::apply(
            [&](auto&&... unpacked_args) {
                return to_zephyr_value((instance->*fn)(std::forward<decltype(unpacked_args)>(unpacked_args)...));
            },
            converted_args);
    }
}

}  // namespace detail

template <typename T>
class ZephyrClassBinder {
public:
    ZephyrClassBinder(ZephyrVM& vm, std::string_view class_name)
        : vm_(vm), host_class_(vm.bind_host_class(std::type_index(typeid(T)), class_name)) {}

    template <typename Ret, typename... Args>
    ZephyrClassBinder& method(std::string_view name, Ret (T::*fn)(Args...)) {
        host_class_->add_method(
            std::string(name),
            [fn](void* instance, const std::vector<ZephyrValue>& args) {
                return detail::invoke_member<Ret, T, decltype(fn), Args...>(static_cast<T*>(instance), fn, args);
            });
        return *this;
    }

    template <typename Ret, typename... Args>
    ZephyrClassBinder& method(std::string_view name, Ret (T::*fn)(Args...) const) {
        host_class_->add_method(
            std::string(name),
            [fn](void* instance, const std::vector<ZephyrValue>& args) {
                return detail::invoke_member<Ret, const T, decltype(fn), Args...>(static_cast<const T*>(instance), fn, args);
            });
        return *this;
    }

    template <typename Field>
    ZephyrClassBinder& prop(std::string_view name, Field T::*member) {
        host_class_->add_property(
            std::string(name),
            [member](void* instance) {
                return detail::to_zephyr_value(static_cast<T*>(instance)->*member);
            },
            [member](void* instance, const ZephyrValue& value) {
                using FieldType = detail::remove_cvref_t<Field>;
                static_assert(!std::is_const_v<FieldType>, "Cannot bind a const data member as a writable property.");
                static_cast<T*>(instance)->*member = detail::from_zephyr_value<FieldType>(value);
            });
        return *this;
    }

    std::shared_ptr<ZephyrHostClass> host_class() const { return host_class_; }

private:
    ZephyrVM& vm_;
    std::shared_ptr<ZephyrHostClass> host_class_;
};

// Example:
// vm.bind<Player>("Player").method("damage", &Player::damage).prop("hp", &Player::hp);
template <typename T>
ZephyrClassBinder<T> ZephyrVM::bind(std::string_view class_name) {
    return ZephyrClassBinder<T>(*this, class_name);
}

#ifdef DEBUG_LEAK_CHECK
struct ZephyrDebugGcStats {
    std::size_t live_objects = 0;
    std::size_t total_allocations = 0;
    std::size_t peak_live_objects = 0;
};

ZephyrDebugGcStats debug_gc_stats();
#endif

std::string to_string(const ZephyrValue& value);

}  // namespace zephyr
