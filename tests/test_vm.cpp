#include "test_common.hpp"

namespace zephyr_tests {

void test_execute_and_call() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn add(a: int, b: int) -> int { return a + b; }
        )",
        "unit_add",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_add", "add");
    require(handle.has_value(), "missing add handle");
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(2), zephyr::ZephyrValue(3)});
    require(result.is_int() && result.as_int() == 5, "unexpected add result");
}

void test_trait_impl_dispatch() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            struct Player { x: int, y: int }

            trait Drawable {
                fn draw(self) -> int;
                fn metric(self) -> int;
            }

            impl Drawable for Player {
                fn draw(self) -> int {
                    return self.x + self.y;
                }

                fn metric(self) -> int {
                    return self.x * self.y;
                }
            }

            fn render() -> int {
                let player = Player { x: 3, y: 4 };
                return player.draw() + player.metric();
            }
        )",
        "unit_traits",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_traits", "render");
    require(handle.has_value(), "missing trait render handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 19, "trait impl dispatch returned wrong result");
}

void test_struct_literal_field_shorthand() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            struct Vec2 { x: int, y: int }

            fn sum_coords(x: int, y: int) -> int {
                let point = Vec2 { x, y };
                return point.x + point.y;
            }
        )",
        "unit_struct_shorthand",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_struct_shorthand", "sum_coords");
    require(handle.has_value(), "missing sum_coords handle");
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(8), zephyr::ZephyrValue(13)});
    require(result.is_int() && result.as_int() == 21, "struct field shorthand should bind to same-name locals");
}

void test_core_stdlib_helpers() {
    zephyr::ZephyrVM vm;
    const auto path = std::filesystem::current_path() / ".zephyr_core_stdlib_test.zph";
    {
        std::ofstream out(path);
        out << R"(
            fn run() -> int {
                let mut words = ["guard"];
                words = push(words, "tower");
                words = concat(words, ["north"]);

                if len(range(2, 6)) != 4 {
                    return -1;
                }
                if !contains("damage_event", "event") {
                    return -2;
                }
                if !starts_with("enemy_idle", "enemy") {
                    return -3;
                }
                if !ends_with("enemy_idle", "idle") {
                    return -4;
                }
                if join(words, "-") != "guard-tower-north" {
                    return -5;
                }
                if str(12) != "12" {
                    return -6;
                }
                return len(words);
            }
        )";
    }

    vm.execute_file(path);

    const auto handle = vm.get_function(std::filesystem::weakly_canonical(path).string(), "run");
    require(handle.has_value(), "missing run handle for core stdlib test");
    const auto result = vm.call(*handle);
    std::filesystem::remove(path);
    require(result.is_int() && result.as_int() == 3, "core stdlib helpers returned unexpected result");
}

void test_stdlib_module_imports() {
    zephyr::ZephyrVM vm;
    vm.add_module_search_path(std::filesystem::current_path().string());

    vm.execute_string(
        R"(
            import "std/math" as math;
            import "std/string" as strings;
            import "std/collections" as collections;

            fn run() -> int {
                let values = collections.range(1, 4);
                let doubled = collections.map_array(values, fn(value: int) -> int {
                    return value * 2;
                });
                let sum = collections.fold_array(doubled, 0, fn(total: int, value: int) -> int {
                    return total + value;
                });
                let evens = collections.filter_array(doubled, fn(value: int) -> bool {
                    return value > 3;
                });

                if math.min(9, 4) != 4 {
                    return -1;
                }
                if math.max(9, 4) != 9 {
                    return -2;
                }
                if math.clamp(12, 1, 10) != 10 {
                    return -3;
                }
                if math.pow(3, 3) != 27 {
                    return -4;
                }
                if !strings.starts_with("zephyr", "zep") {
                    return -5;
                }
                if !strings.ends_with("zephyr", "hyr") {
                    return -6;
                }
                if strings.repeat("ha", 3) != "hahaha" {
                    return -7;
                }
                if len(values) != 3 {
                    return -8;
                }
                if doubled[0] != 2 || doubled[2] != 6 {
                    return -9;
                }
                if sum != 12 {
                    return -10;
                }
                if len(evens) != 2 || evens[0] != 4 || evens[1] != 6 {
                    return -11;
                }
                return 0;
            }
        )",
        "unit_stdlib_modules",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_stdlib_modules", "run");
    require(handle.has_value(), "stdlib import test should expose run");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 0, "stdlib modules should import and execute correctly");
}

void test_runtime_error_includes_stack_trace() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn foo(value: int) -> int {
                return value;
            }

            fn bar() -> int {
                return foo("oops");
            }

            fn run() -> int {
                return bar();
            }
        )",
        "unit_runtime_stack",
        std::filesystem::current_path());

    const auto run = vm.get_function("unit_runtime_stack", "run");
    require(run.has_value(), "runtime stack test should expose run");

    bool rejected = false;
    try {
        (void)vm.call(*run);
    } catch (const zephyr::ZephyrRuntimeError& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("function 'foo' argument 1 for parameter 'value' expects 'int', got 'string'") != std::string::npos,
                "runtime type error should explain expected and actual types");
        require(error.stack_trace.find("at foo (unit_runtime_stack:2:") != std::string::npos,
                "stack trace should include foo");
        require(error.stack_trace.find("at bar (unit_runtime_stack:6:") != std::string::npos,
                "stack trace should include bar");
        require(error.stack_trace.find("at run (unit_runtime_stack:10:") != std::string::npos,
                "stack trace should include run");
    }
    require(rejected, "runtime errors should surface as ZephyrRuntimeError");
}

void test_runtime_trait_method_not_found_includes_hint() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            trait Greet {
                fn greet(self) -> string;
            }

            struct Dog { name: string }

            fn run() -> string {
                let dog = Dog { name: "Pixel" };
                return dog.greet();
            }
        )",
        "unit_runtime_trait_hint",
        std::filesystem::current_path());

    const auto run = vm.get_function("unit_runtime_trait_hint", "run");
    require(run.has_value(), "trait hint test should expose run");

    bool rejected = false;
    try {
        (void)vm.call(*run);
    } catch (const zephyr::ZephyrRuntimeError& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("RuntimeError: 'Dog' does not implement method 'greet'") != std::string::npos,
                "trait dispatch failures should identify the missing method");
        require(message.find("hint: add 'impl Greet for Dog { fn greet(...) { ... } }'") != std::string::npos,
                "trait dispatch failures should include an impl hint");
    }
    require(rejected, "calling a missing trait method should fail");
}

void test_serialization_requires_stable_handles() {
    struct Counter {
        int value = 12;
    };

    auto klass = std::make_shared<zephyr::ZephyrHostClass>("Counter");
    klass->add_property("value", [](void* instance) { return zephyr::ZephyrValue(static_cast<Counter*>(instance)->value); });

    const auto persistent_counter = std::make_shared<Counter>();
    const auto stable_counter = std::make_shared<Counter>();

    zephyr::ZephyrHostObjectRef persistent_ref{klass, persistent_counter};
    persistent_ref.kind = zephyr::ZephyrHostHandleKind::Entity;
    persistent_ref.lifetime = zephyr::ZephyrHostHandleLifetime::Persistent;

    zephyr::ZephyrHostObjectRef stable_ref{klass, stable_counter};
    stable_ref.kind = zephyr::ZephyrHostHandleKind::Asset;
    stable_ref.lifetime = zephyr::ZephyrHostHandleLifetime::Stable;
    stable_ref.stable_guid = zephyr::ZephyrGuid128{0xAAULL, 0xBBULL};

    zephyr::ZephyrVM vm;
    bool persistent_rejected = false;
    try {
        vm.serialize_value(zephyr::ZephyrValue(persistent_ref));
    } catch (const std::exception&) {
        persistent_rejected = true;
    }
    require(persistent_rejected, "persistent handle should not be directly serializable");

    const auto serialized_handle = vm.serialize_value(zephyr::ZephyrValue(stable_ref));
    const auto& serialized_handle_envelope = require_serialized_envelope(serialized_handle, "stable handle serialization");
    const auto& serialized_handle_node =
        require_serialized_node(serialized_handle_envelope.fields.at("payload"), "stable handle serialization payload");
    require(serialized_handle_node.fields.at("kind").as_string() == "stable_handle",
            "stable handle serialization payload should use the stable_handle node kind");
    require(serialized_handle_node.fields.at("guid_high").is_int() && serialized_handle_node.fields.at("guid_high").as_int() == 0xAA,
            "stable handle serialization payload lost guid_high");
    require(serialized_handle_node.fields.at("guid_low").is_int() && serialized_handle_node.fields.at("guid_low").as_int() == 0xBB,
            "stable handle serialization payload lost guid_low");

    vm.execute_string(
        R"(
            struct SaveData { score: int, target: HostObject }

            fn make_save(target: HostObject) -> SaveData {
                return SaveData { score: 99, target: target };
            }
        )",
        "unit_serialize",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_serialize", "make_save");
    require(handle.has_value(), "missing make_save handle");

    const auto serialized_record = vm.call_serialized(*handle, {zephyr::ZephyrValue(stable_ref)});
    const auto& serialized_record_envelope = require_serialized_envelope(serialized_record, "call_serialized");
    const auto& serialized_record_node =
        require_serialized_node(serialized_record_envelope.fields.at("payload"), "call_serialized payload");
    require(serialized_record_node.fields.at("kind").as_string() == "record", "call_serialized payload should use the record node kind");
    require(serialized_record_node.fields.at("fields").is_record(), "serialized record payload should expose a field map");
    const auto& serialized_fields = serialized_record_node.fields.at("fields").as_record();
    const auto& score_node = require_serialized_node(serialized_fields.fields.at("score"), "serialized score field");
    require(score_node.fields.at("kind").as_string() == "int" && score_node.fields.at("value").is_int() &&
                score_node.fields.at("value").as_int() == 99,
            "serialized record lost score field");
    const auto& target_node = require_serialized_node(serialized_fields.fields.at("target"), "serialized target field");
    require(target_node.fields.at("kind").as_string() == "stable_handle",
            "serialized record lost stable host handle field");

    const auto round_trip_handle = vm.deserialize_value(serialized_handle);
    require(round_trip_handle.is_host_object(), "deserializing a serialized stable handle should reconstruct a host handle");
    require(round_trip_handle.as_host_object().lifetime == zephyr::ZephyrHostHandleLifetime::Stable,
            "deserialized stable handle should stay stable");
    require(round_trip_handle.as_host_object().stable_guid == stable_ref.stable_guid,
            "deserialized stable handle should preserve its GUID");

    zephyr::ZephyrValue::Array legacy_plain_array{zephyr::ZephyrValue(1), zephyr::ZephyrValue(2), zephyr::ZephyrValue(3)};
    const auto legacy_round_trip = vm.deserialize_value(zephyr::ZephyrValue(legacy_plain_array));
    require(legacy_round_trip.is_array() && legacy_round_trip.as_array().size() == 3,
            "deserialize_value should stay backward-compatible with legacy plain-array save payloads");

    bool call_rejected = false;
    try {
        vm.call_serialized(*handle, {zephyr::ZephyrValue(persistent_ref)});
    } catch (const std::exception&) {
        call_rejected = true;
    }
    require(call_rejected, "call_serialized should reject non-stable host handles");
}

void test_serialization_schema_rejects_unknown_version() {
    zephyr::ZephyrVM vm;
    const auto serialized = vm.serialize_value(zephyr::ZephyrValue(42));
    auto invalid = serialized.as_record();
    invalid.fields["version"] = zephyr::ZephyrValue(999);

    bool rejected = false;
    try {
        vm.deserialize_value(zephyr::ZephyrValue(invalid));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "deserialize_value should reject unsupported serialized envelope versions");
}

void test_closure_cell_capture_survives_outer_return_and_gc() {
    zephyr::ZephyrVM vm;
    vm.register_global_function(
        "force_gc",
        [&vm](const std::vector<zephyr::ZephyrValue>&) {
            vm.collect_garbage();
            return zephyr::ZephyrValue();
        },
        {}, "Nil");

    vm.execute_string(
        R"(
            fn drive() -> int {
                let counter = (fn() {
                    let mut current: int = 1;
                    return fn(step: int) -> int {
                        current = current + step;
                        return current;
                    };
                })();

                let first = counter(2);
                force_gc();
                let second = counter(3);
                return first + second;
            }
        )",
        "unit_closure_cells",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_closure_cells", "drive");
    require(handle.has_value(), "missing drive closure cell handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 9, "closure cell capture did not survive outer return + GC");

    const auto stats = vm.runtime_stats();
    require(stats.vm.ast_fallback_executions == 0, "anonymous function expressions should stay on native bytecode path");

    const auto dump = vm.dump_bytecode("unit_closure_cells", "drive");
    require(dump.find("op=MakeFunction") != std::string::npos, "bytecode dump should include MakeFunction");
    require(dump.find("op=LoadUpvalue") != std::string::npos, "bytecode dump should include LoadUpvalue");
    require(dump.find("op=StoreUpvalue") != std::string::npos, "bytecode dump should include StoreUpvalue");
}

void test_lightweight_coroutine_skips_local_binding_cache() {
    zephyr::ZephyrVM vm;
    vm.install_core();

    vm.execute_string(
        R"(
            fn make_counter(limit: int) -> Coroutine {
                return coroutine fn() -> int {
                    let mut i: int = 0;
                    while i < limit {
                        yield i;
                        i = i + 1;
                    }
                    return limit;
                };
            }
        )",
        "unit_lightweight_coroutine",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_lightweight_coroutine", "make_counter");
    require(handle.has_value(), "lightweight coroutine: missing make_counter handle");
    const auto coroutine = vm.spawn_coroutine(*handle, {zephyr::ZephyrValue(static_cast<std::int64_t>(4))});
    require(coroutine.has_value() && coroutine->valid(), "lightweight coroutine: spawn_coroutine should return a valid handle");

    for (std::int64_t expected = 0; expected < 4; ++expected) {
        const auto yielded = vm.resume(*coroutine);
        require(yielded.is_int() && yielded.as_int() == expected, "lightweight coroutine: unexpected yielded value");
    }

    const auto result = vm.resume(*coroutine);
    require(result.is_int() && result.as_int() == 4, "lightweight coroutine: unexpected final result");

    const auto stats = vm.runtime_stats();
    require(stats.vm.local_binding_cache_hits == 0, "lightweight coroutine: local binding cache should stay cold");
}

void test_coroutine_resume_and_done() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn drive() -> int {
                let worker = coroutine fn() -> int {
                    yield 1;
                    yield 2;
                    return 3;
                };

                let mut total: int = 0;
                if worker.done {
                    return -1;
                }

                total = total + resume worker;
                total = total + resume worker;

                if worker.done {
                    return -2;
                }

                total = total + resume worker;

                if !worker.done {
                    return -3;
                }

                return total;
            }
        )",
        "unit_coroutine",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine", "drive");
    require(handle.has_value(), "missing drive coroutine handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 6, "unexpected coroutine resume result");
}

void test_coroutine_is_lazy_and_preserves_state_across_gc() {
    int bumps = 0;

    zephyr::ZephyrVM vm;
    vm.register_global_function(
        "bump",
        [&bumps](const std::vector<zephyr::ZephyrValue>&) {
            ++bumps;
            return zephyr::ZephyrValue();
        },
        {}, "Nil");
    vm.register_global_function(
        "bump_count",
        [&bumps](const std::vector<zephyr::ZephyrValue>&) { return zephyr::ZephyrValue(bumps); },
        {}, "int");
    vm.register_global_function(
        "force_gc",
        [&vm](const std::vector<zephyr::ZephyrValue>&) {
            vm.collect_garbage();
            return zephyr::ZephyrValue();
        },
        {}, "Nil");

    vm.execute_string(
        R"(
            fn drive() -> int {
                let worker = coroutine fn() -> int {
                    bump();
                    let values = [7, 11, 13];
                    yield values[0];
                    return values[1];
                };

                if bump_count() != 0 {
                    return -10;
                }

                let first = resume worker;
                if bump_count() != 1 {
                    return -11;
                }
                if !worker.suspended {
                    return -12;
                }

                force_gc();

                let second = resume worker;
                if !worker.done {
                    return -13;
                }

                return first + second;
            }
        )",
        "unit_coroutine_lazy_gc",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine_lazy_gc", "drive");
    require(handle.has_value(), "missing drive lazy coroutine handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 18, "lazy coroutine state did not survive suspension + GC");
}

void test_nested_script_function_yield_inside_coroutine() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn helper(seed: int) -> int {
                yield seed + 1;
                return seed + 2;
            }

            fn drive() -> int {
                let worker = coroutine fn() -> int {
                    let final = helper(10);
                    return final + 100;
                };

                let first = resume worker;
                if worker.done {
                    return -1;
                }

                let second = resume worker;
                if !worker.done {
                    return -2;
                }

                return first + second;
            }
        )",
        "unit_coroutine_nested_yield",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine_nested_yield", "drive");
    require(handle.has_value(), "missing drive nested coroutine handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 123, "nested script function yield did not resume through coroutine");
}

void test_deeply_nested_script_function_yield_survives_gc() {
    zephyr::ZephyrVM vm;
    vm.register_global_function(
        "force_gc",
        [&vm](const std::vector<zephyr::ZephyrValue>&) {
            vm.collect_garbage();
            return zephyr::ZephyrValue();
        },
        {}, "Nil");

    vm.execute_string(
        R"(
            fn leaf(seed: int) -> int {
                yield seed + 1;
                return seed + 2;
            }

            fn middle(seed: int) -> int {
                let inner = leaf(seed + 10);
                return inner + 20;
            }

            fn outer(seed: int) -> int {
                let value = middle(seed);
                return value + 100;
            }

            fn drive() -> int {
                let worker = coroutine fn() -> int {
                    return outer(1);
                };

                let first = resume worker;
                if worker.done {
                    return -1;
                }

                force_gc();

                let second = resume worker;
                if !worker.done {
                    return -2;
                }

                return first + second;
            }
        )",
        "unit_coroutine_deep_nested_yield",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine_deep_nested_yield", "drive");
    require(handle.has_value(), "missing drive deep nested coroutine handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 145, "deep nested script function yield did not resume through full frame stack");
}

void test_coroutine_runtime_stats_and_dump() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn leaf(seed: int) -> int {
                yield seed + 1;
                return seed + 2;
            }

            fn middle(seed: int) -> int {
                return leaf(seed + 10) + 20;
            }

            let worker = coroutine fn() -> int {
                return middle(1) + 100;
            };

            fn step() -> int {
                return resume worker;
            }
        )",
        "unit_coroutine_stats",
        std::filesystem::current_path());

    const auto step = vm.get_function("unit_coroutine_stats", "step");
    require(step.has_value(), "missing step coroutine handle");

    auto stats = vm.runtime_stats();
    require(stats.coroutine_objects >= 1, "runtime stats should count coroutine objects");
    require(stats.max_coroutine_frame_depth == 1, "fresh coroutine should start with only the root frame");

    const auto first = vm.call(*step);
    require(first.is_int() && first.as_int() == 12, "unexpected first coroutine yield result");

    stats = vm.runtime_stats();
    require(stats.suspended_coroutines >= 1, "runtime stats should count suspended coroutines after yield");
    require(stats.total_coroutine_frames >= 3, "runtime stats should include nested coroutine helper frames");
    require(stats.max_coroutine_frame_depth >= 3, "runtime stats should record deep nested coroutine frame depth");
    require(stats.total_coroutine_stack_capacity >= stats.total_coroutine_stack_values,
            "stack capacity metric should be at least the live stack value count");
    require(stats.total_coroutine_local_capacity >= stats.total_coroutine_local_slots,
            "local slot capacity metric should be at least the live local slot count");
    require(stats.coroutine_compactions >= 1, "yielding coroutine should trigger suspended frame compaction");
    require(stats.coroutine_compacted_frames >= 3, "compaction stats should include each suspended coroutine frame");
    require(stats.total_coroutine_resume_calls >= 1, "runtime stats should track coroutine resume calls");
    require(stats.total_coroutine_yields >= 1, "runtime stats should track coroutine yields");
    require(stats.total_coroutine_steps > 0, "runtime stats should track executed coroutine step cost");
    require(stats.max_coroutine_resume_steps > 0, "runtime stats should record peak single-resume coroutine cost");

    const auto dump = vm.debug_dump_coroutines();
    require(dump.find("suspended=true") != std::string::npos, "debug dump should expose suspended coroutine state");
    require(dump.find("frames=3") != std::string::npos, "debug dump should expose nested coroutine frame depth");
    require(dump.find("frame[0]") != std::string::npos, "debug dump should list per-frame details");
    require(dump.find("locals=") != std::string::npos, "debug dump should expose frame slot usage");
    require(dump.find("compactions=") != std::string::npos, "debug dump should expose coroutine compaction totals");
    require(dump.find("resumes=") != std::string::npos, "debug dump should expose coroutine resume counts");
    require(dump.find("total_steps=") != std::string::npos, "debug dump should expose coroutine step costs");

    const auto second = vm.call(*step);
    require(second.is_int() && second.as_int() == 133, "unexpected resumed coroutine result");

    stats = vm.runtime_stats();
    require(stats.completed_coroutines >= 1, "runtime stats should count completed coroutines");
    require(stats.total_coroutine_resume_calls >= 2, "runtime stats should accumulate repeated coroutine resumes");

    const auto completed_dump = vm.debug_dump_coroutines();
    require(completed_dump.find("completed=true") != std::string::npos, "debug dump should expose completed coroutine state");
}

void test_yield_outside_coroutine_rejected() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn bad() -> int {
                yield 1;
                return 0;
            }
        )",
        "unit_bad_yield",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_bad_yield", "bad");
    require(handle.has_value(), "missing bad handle");

    bool rejected = false;
    try {
        vm.call(*handle);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "yield outside coroutine should be rejected at runtime");
}

void test_coroutine_rejects_frame_handle_yield() {
    struct Counter {
        int value = 3;
    };

    auto klass = std::make_shared<zephyr::ZephyrHostClass>("Counter");
    klass->add_property("value", [](void* instance) { return zephyr::ZephyrValue(static_cast<Counter*>(instance)->value); });

    const auto counter = std::make_shared<Counter>();
    zephyr::ZephyrVM vm;
    vm.register_global_function(
        "make_frame_counter",
        [klass, counter](const std::vector<zephyr::ZephyrValue>&) {
            zephyr::ZephyrHostObjectRef ref{klass, counter};
            ref.kind = zephyr::ZephyrHostHandleKind::Entity;
            ref.lifetime = zephyr::ZephyrHostHandleLifetime::Frame;
            return zephyr::ZephyrValue(ref);
        },
        {},
        "HostObject");

    vm.execute_string(
        R"(
            fn bad_worker() -> Coroutine {
                return coroutine fn() -> int {
                    yield make_frame_counter();
                    return 0;
                };
            }
        )",
        "unit_coroutine_frame_yield",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine_frame_yield", "bad_worker");
    require(handle.has_value(), "missing bad_worker handle");

    bool rejected = false;
    try {
        vm.call(*handle);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "frame handle should not survive coroutine yield storage");
}

void test_v2_coroutine_handles_young_gc_and_deserialize() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 128;
    zephyr::ZephyrVM vm(config);
    require(vm.config().gc.nursery_trigger_bytes == 64, "config constructor should preserve GC config");

    vm.execute_string(
        R"(
            fn make_counter(limit: int) -> Coroutine {
                return coroutine fn() -> int {
                    let mut i = 0;
                    while i < limit {
                        yield i;
                        i += 1;
                    }
                    return limit;
                };
            }

            fn values() -> Array {
                return [1, 2, 3];
            }
        )",
        "unit_v2_coroutines",
        std::filesystem::current_path());

    const auto values_handle = vm.get_function("unit_v2_coroutines", "values");
    require(values_handle.has_value(), "missing values handle");
    const auto values = vm.call(*values_handle);
    const auto round_trip = vm.deserialize_value(vm.serialize_value(values));
    require(round_trip.is_array() && round_trip.as_array().size() == 3, "deserialize_value should round-trip serialized arrays");

    const auto coroutine_fn = vm.get_function("unit_v2_coroutines", "make_counter");
    require(coroutine_fn.has_value(), "missing make_counter handle");
    const auto coroutine = vm.spawn_coroutine(*coroutine_fn, {zephyr::ZephyrValue(3)});
    require(coroutine.has_value() && coroutine->valid(), "spawn_coroutine should return a valid retained handle");

    const auto before = vm.query_coroutine(*coroutine);
    require(before.has_value() && !before->started, "fresh coroutine handle should not be started");

    const auto first = vm.resume(*coroutine);
    require(first.is_int() && first.as_int() == 0, "unexpected first coroutine yield");

    const auto mid = vm.query_coroutine(*coroutine);
    require(mid.has_value() && mid->started && mid->suspended && !mid->completed, "coroutine should suspend after yielding");

    vm.collect_young();
    vm.gc_verify_young();
    const auto stats = vm.runtime_stats();
    require(stats.gc.total_young_collections >= 1, "collect_young should increment young GC metrics");

    vm.cancel(*coroutine);
    require(!vm.query_coroutine(*coroutine).has_value(), "cancel should release retained coroutine handle");
}

void test_wave_a_coroutine_set_unordered() {
    // Verify that suspending and resuming many coroutines works correctly now
    // that suspended_coroutines_ is an unordered_set (O(1) insert/erase).
    // Uses multiple independent coroutines in flight simultaneously.
    zephyr::ZephyrVM vm;
    vm.install_core();

    vm.execute_string(R"(
        fn drive() -> int {
            let c0 = coroutine fn() -> int { yield 10; return 11; };
            let c1 = coroutine fn() -> int { yield 20; return 21; };
            let c2 = coroutine fn() -> int { yield 30; return 31; };
            let c3 = coroutine fn() -> int { yield 40; return 41; };
            let c4 = coroutine fn() -> int { yield 50; return 51; };

            // First resume: all 5 coroutines suspended simultaneously
            let a = resume c0;
            let b = resume c1;
            let c = resume c2;
            let d = resume c3;
            let e = resume c4;

            // Second resume: each finishes
            let a2 = resume c0;
            let b2 = resume c1;
            let c2r = resume c2;
            let d2 = resume c3;
            let e2 = resume c4;

            return a + b + c + d + e + a2 + b2 + c2r + d2 + e2;
        }
    )", "coro_set_test", std::filesystem::current_path());

    auto handle = vm.get_function("coro_set_test", "drive");
    require(handle.has_value(), "wave_a coro set: drive must exist");

    // 10+20+30+40+50 + 11+21+31+41+51 = 150 + 155 = 305
    auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 305,
            "wave_a coro set: expected 305 (got " + std::to_string(result.as_int()) + ")");
}

// ─── Wave A: 3.2 write barrier young-skip ─────────────────────────────────

}  // namespace zephyr_tests
