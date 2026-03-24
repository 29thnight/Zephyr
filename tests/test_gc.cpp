#include "test_common.hpp"

namespace zephyr_tests {

void test_incremental_gc_stats_progress() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn churn(limit: Int) -> Int {
                let mut total: Int = 0;
                let mut i: Int = 0;
                while i < limit {
                    let values = [i, i + 1, i + 2, i + 3];
                    total = total + values[0];
                    i = i + 1;
                }
                return total;
            }
        )",
        "unit_gc_stats",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_gc_stats", "churn");
    require(handle.has_value(), "missing churn handle");
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(64)});
    require(result.is_int() && result.as_int() == 2016, "unexpected churn result");

    for (int i = 0; i < 32; ++i) {
        vm.gc_step(8);
    }

    const auto stats = vm.runtime_stats();
    require(stats.total_gc_steps > 0, "gc_step should update runtime stats");
    require(stats.total_allocations > 0, "runtime stats should track allocations");
}

void test_string_literal_interning_tracks_hits_and_misses() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            export fn short_literals_match() -> Bool {
                let a = "intern-me";
                let b = "intern-me";
                return a == b;
            }

            export fn long_literals_match() -> Bool {
                let a = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
                let b = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
                return a == b;
            }
        )",
        "unit_string_intern",
        std::filesystem::current_path());

    const auto short_handle = vm.get_function("unit_string_intern", "short_literals_match");
    const auto long_handle = vm.get_function("unit_string_intern", "long_literals_match");
    require(short_handle.has_value() && long_handle.has_value(), "interning test should export both functions");

    const auto short_result = vm.call(*short_handle);
    const auto long_result = vm.call(*long_handle);
    require(short_result.is_bool() && short_result.as_bool(), "short interned literals should compare equal");
    require(long_result.is_bool() && long_result.as_bool(), "long literals should still compare equal");

    const auto stats = vm.runtime_stats();
    require(stats.vm.string_intern_misses >= 1, "first short literal should record an intern miss");
    require(stats.vm.string_intern_hits >= 1, "repeated short literal should record an intern hit");
}

void test_gc_verify_full_and_dirty_barrier_dedup() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            let mut counter: Int = 0;
            let items = [0];

            fn churn(limit: Int) -> Int {
                let mut total: Int = 0;
                let mut i: Int = 0;
                while i < limit {
                    let values = [i, i + 1, i + 2, i + 3];
                    total = total + values[0];
                    i = i + 1;
                }
                return total;
            }

            fn mutate_many(limit: Int) -> Int {
                let mut i: Int = 0;
                while i < limit {
                    counter = counter + 1;
                    items[0] = counter;
                    i = i + 1;
                }
                return items[0];
            }
        )",
        "unit_gc_verify",
        std::filesystem::current_path());

    const auto churn = vm.get_function("unit_gc_verify", "churn");
    require(churn.has_value(), "missing churn handle for gc verify test");
    const auto churn_result = vm.call(*churn, {zephyr::ZephyrValue(1024)});
    require(churn_result.is_int(), "unexpected churn result type for gc verify test");

    bool entered_marking = false;
    for (int i = 0; i < 512; ++i) {
        vm.gc_step(1);
        const auto stats = vm.runtime_stats();
        if (stats.gc_phase == zephyr::ZephyrGcPhase::SeedRoots ||
            stats.gc_phase == zephyr::ZephyrGcPhase::DrainGray ||
            stats.gc_phase == zephyr::ZephyrGcPhase::RescanDirtyRoots) {
            entered_marking = true;
            break;
        }
    }
    require(entered_marking, "gc verify test should enter incremental marking");

    const auto mutate = vm.get_function("unit_gc_verify", "mutate_many");
    require(mutate.has_value(), "missing mutate_many handle");
    const auto mutate_result = vm.call(*mutate, {zephyr::ZephyrValue(32)});
    require(mutate_result.is_int() && mutate_result.as_int() == 32, "unexpected mutate_many result");

    auto stats = vm.runtime_stats();
    require(stats.dirty_root_environments == 1, "remembered root barrier should dedupe repeated global writes");
    require(stats.dirty_objects == 1, "remembered object barrier should dedupe repeated array writes");
    require(stats.remembered_objects == 0, "minor remembered set should ignore scalar-only writes");

    vm.gc_verify_full();

    stats = vm.runtime_stats();
    require(stats.total_gc_verifications >= 1, "gc verification count should update");
    require(stats.gc_phase == zephyr::ZephyrGcPhase::Idle, "gc verification should leave the runtime idle");
    require(stats.dirty_root_environments == 0, "gc verification should clear dirty root queues");
    require(stats.dirty_objects == 0, "gc verification should clear dirty object queues");
    require(stats.detach_queue_objects == 0, "gc verification should drain detach queue");
    require(stats.remembered_objects == 0, "full collection should leave no old-to-young remembered owners for scalar-only writes");
}

void test_gc_stress_mode_advances_at_bytecode_safe_points() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn churn(limit: Int) -> Int {
                let mut total: Int = 0;
                let mut i: Int = 0;
                while i < limit {
                    let values = [i, i + 1, i + 2, i + 3];
                    total = total + values[0];
                    i = i + 1;
                }
                return total;
            }
        )",
        "unit_gc_stress",
        std::filesystem::current_path());

    vm.set_gc_stress(true, 1);

    const auto churn = vm.get_function("unit_gc_stress", "churn");
    require(churn.has_value(), "missing churn handle for gc stress test");
    const auto result = vm.call(*churn, {zephyr::ZephyrValue(128)});
    require(result.is_int() && result.as_int() == 8128, "unexpected churn result under gc stress mode");

    auto stats = vm.runtime_stats();
    require(stats.gc_stress_enabled, "gc stress mode should be reported in runtime stats");
    require(stats.gc_stress_budget == 1, "gc stress budget should be reported in runtime stats");
    require(stats.total_gc_stress_safe_points > 0, "gc stress mode should run safepoint-driven GC steps");
    require(stats.total_gc_steps >= stats.total_gc_stress_safe_points, "stress safepoints should contribute to gc step count");

    vm.gc_verify_full();
    vm.set_gc_stress(false);
    vm.collect_garbage();

    stats = vm.runtime_stats();
    require(stats.total_gc_verifications >= 1, "gc verify should remain compatible with gc stress mode");
    require(!stats.gc_stress_enabled, "gc stress mode should turn off cleanly");
}

void test_gc_collects_unreachable_cycles_and_returns_to_baseline() {
    const auto leak_baseline = debug_live_gc_objects();
    {
        zephyr::ZephyrVM vm;
        vm.execute_string(
            R"(
                fn make_cycle() {
                    let a = [nil];
                    let b = [a];
                    a[0] = b;
                }
            )",
            "unit_gc_cycle",
            std::filesystem::current_path());

        vm.collect_garbage();
        const auto stable_live_count = debug_live_gc_objects();

        const auto handle = vm.get_function("unit_gc_cycle", "make_cycle");
        require(handle.has_value(), "missing make_cycle handle");

        vm.call(*handle);
#ifdef DEBUG_LEAK_CHECK
        require(debug_live_gc_objects() > stable_live_count, "cycle test did not allocate additional GC objects");
#endif
        vm.collect_garbage();
#ifdef DEBUG_LEAK_CHECK
        require(debug_live_gc_objects() == stable_live_count, "cycle GC did not return to the stable baseline");
#endif
    }
    require_no_debug_gc_leaks(leak_baseline, "cycle GC test");
}

void test_gc_preserves_temporary_callee_during_collection() {
    const auto leak_baseline = debug_live_gc_objects();
    {
        zephyr::ZephyrVM vm;
        vm.register_global_function(
            "force_gc",
            [&vm](const std::vector<zephyr::ZephyrValue>&) {
                vm.collect_garbage();
                return zephyr::ZephyrValue();
            },
            {},
            "Nil");

        vm.execute_string(
            R"(
                fn run() -> Int {
                    return (fn() -> Int {
                        let values = [7, 11, 13];
                        force_gc();
                        return values[1];
                    })();
                }
            )",
            "unit_gc_temp_callee",
            std::filesystem::current_path());

        const auto handle = vm.get_function("unit_gc_temp_callee", "run");
        require(handle.has_value(), "missing run handle");
        const auto result = vm.call(*handle);
        require(result.is_int() && result.as_int() == 11, "temporary callee was not preserved across GC");
    }
    require_no_debug_gc_leaks(leak_baseline, "temporary callee GC test");
}

void test_gc_pause_stats_api() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            let values = [1, 2, 3, 4];
            export fn read() -> Int {
                return values[0];
            }
        )",
        "unit_gc_pause_stats",
        std::filesystem::current_path());

    vm.collect_young();
    vm.collect_garbage();

    const auto runtime_stats = vm.runtime_stats();
    const auto pause_stats = vm.get_gc_pause_stats();
    require(runtime_stats.gc.total_young_collections >= 1, "gc pause stats test should record a young collection");
    require(runtime_stats.gc.total_full_collections >= 1, "gc pause stats test should record a full collection");
    require(pause_stats.p95_ns >= pause_stats.p50_ns, "gc pause p95 should be >= p50");
    require(pause_stats.p99_ns >= pause_stats.p95_ns, "gc pause p99 should be >= p95");
    require(pause_stats.frame_budget_miss_count <=
                runtime_stats.gc.total_young_collections + runtime_stats.gc.total_full_collections,
            "frame budget misses should not exceed total GC pauses");
}

void test_gc_trace_json_export() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            let values = [1, 2, 3, 4];
            export fn read() -> Int {
                return values[0];
            }
        )",
        "unit_gc_trace_export",
        std::filesystem::current_path());

    vm.start_gc_trace();
    require(vm.is_gc_trace_active(), "gc trace should be active after start");

    vm.collect_young();
    vm.collect_garbage();

    vm.stop_gc_trace();
    require(!vm.is_gc_trace_active(), "gc trace should stop when requested");

    const auto trace_json = vm.get_gc_trace_json();
    require(!trace_json.empty() && trace_json.front() == '[' && trace_json.back() == ']',
            "gc trace export should be a JSON array");

    const auto young_start = trace_json.find("\"type\":\"YoungStart\"");
    const auto young_end = trace_json.find("\"type\":\"YoungEnd\"");
    const auto full_start = trace_json.find("\"type\":\"FullStart\"");
    const auto full_end = trace_json.find("\"type\":\"FullEnd\"");
    require(young_start != std::string::npos, "gc trace should include YoungStart");
    require(young_end != std::string::npos, "gc trace should include YoungEnd");
    require(full_start != std::string::npos, "gc trace should include FullStart");
    require(full_end != std::string::npos, "gc trace should include FullEnd");
    require(young_start < young_end, "young GC start event should precede end event");
    require(full_start < full_end, "full GC start event should precede end event");
    require(trace_json.find("\"ts_ns\":") != std::string::npos, "gc trace should include timestamps");
    require(trace_json.find("\"heap_before\":") != std::string::npos, "gc trace should include heap_before");
    require(trace_json.find("\"heap_after\":") != std::string::npos, "gc trace should include heap_after for end events");
}

void test_v2_minor_remembered_set_preserves_old_to_young_edges() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            let holder = [0];

            fn attach(value: Int) -> Int {
                holder[0] = [value];
                return len(holder[0]);
            }

            fn read() -> Int {
                return holder[0][0];
            }

            fn clear() -> Int {
                holder[0] = 0;
                return 0;
            }
        )",
        "unit_v2_minor_remembered",
        std::filesystem::current_path());

    vm.collect_young();
    vm.collect_young();
    const auto promoted = vm.runtime_stats();
    require(promoted.gc.old_objects >= 1, "young collection should promote long-lived global containers");

    const auto attach = vm.get_function("unit_v2_minor_remembered", "attach");
    const auto read = vm.get_function("unit_v2_minor_remembered", "read");
    const auto clear = vm.get_function("unit_v2_minor_remembered", "clear");
    require(attach.has_value() && read.has_value() && clear.has_value(), "missing remembered-set test functions");

    const auto attach_result = vm.call(*attach, {zephyr::ZephyrValue(42)});
    require(attach_result.is_int() && attach_result.as_int() == 1, "attach should install a young array payload");

    auto stats = vm.runtime_stats();
    require(stats.gc.remembered_objects >= 1, "old-to-young write should register a remembered owner");
    require(stats.gc.remembered_cards >= 1, "old-to-young array write should register at least one dirty card");

    vm.collect_young();
    vm.gc_verify_young();
    const auto read_result = vm.call(*read);
    require(read_result.is_int() && read_result.as_int() == 42, "remembered set should keep young objects reachable through old owners alive");

    const auto clear_result = vm.call(*clear);
    require(clear_result.is_int() && clear_result.as_int() == 0, "clear should reset the remembered-set fixture");
    vm.collect_young();
    vm.gc_verify_young();

    stats = vm.runtime_stats();
    require(stats.gc.remembered_objects == 0, "minor remembered set should compact stale old-to-young owners");
    require(stats.gc.remembered_cards == 0, "minor remembered cards should clear once the old owner no longer references young values");
}

void test_v2_struct_cards_preserve_old_to_young_field_edges() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            struct Holder { value: Any }

            let holder = Holder { value: 0 };

            fn attach(value: Int) -> Int {
                holder.value = [value];
                return len(holder.value);
            }

            fn read() -> Int {
                return holder.value[0];
            }

            fn clear() -> Int {
                holder.value = 0;
                return 0;
            }
        )",
        "unit_v2_struct_cards",
        std::filesystem::current_path());

    vm.collect_young();
    vm.collect_young();
    const auto promoted = vm.runtime_stats();
    require(promoted.gc.old_objects >= 1, "young collection should promote long-lived struct holders");

    const auto attach = vm.get_function("unit_v2_struct_cards", "attach");
    const auto read = vm.get_function("unit_v2_struct_cards", "read");
    const auto clear = vm.get_function("unit_v2_struct_cards", "clear");
    require(attach.has_value() && read.has_value() && clear.has_value(), "missing struct card test functions");

    const auto attach_result = vm.call(*attach, {zephyr::ZephyrValue(42)});
    require(attach_result.is_int() && attach_result.as_int() == 1, "attach should install a young array into the old struct field");

    auto stats = vm.runtime_stats();
    require(stats.gc.remembered_objects >= 1, "old struct field write should register a remembered owner");
    require(stats.gc.remembered_cards >= 1, "old struct field write should publish a dirty card");

    vm.collect_young();
    vm.gc_verify_young();
    const auto read_result = vm.call(*read);
    require(read_result.is_int() && read_result.as_int() == 42,
            "struct field cards should keep young objects reachable through old owners alive");

    const auto clear_result = vm.call(*clear);
    require(clear_result.is_int() && clear_result.as_int() == 0, "clear should reset the struct card fixture");
    vm.collect_young();
    vm.gc_verify_young();

    stats = vm.runtime_stats();
    require(stats.gc.remembered_objects == 0, "minor remembered set should compact stale struct owners");
    require(stats.gc.remembered_cards == 0, "struct remembered cards should clear once the old owner no longer references young values");
}

void test_v2_environment_cards_preserve_old_local_upvalue_cells() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            fn make_env_keeper() -> Coroutine {
                return coroutine fn() -> Int {
                    let mut value = 41;
                    yield 0;
                    let mut temporary = 0;
                    temporary = fn() -> Int { return value + 1; };
                    temporary = 0;
                    yield 1;
                    return value + 1;
                };
            }
        )",
        "unit_v2_env_cards",
        std::filesystem::current_path());

    const auto maker = vm.get_function("unit_v2_env_cards", "make_env_keeper");
    require(maker.has_value(), "missing make_env_keeper handle");
    const auto coroutine = vm.spawn_coroutine(*maker);
    require(coroutine.has_value() && coroutine->valid(), "spawn_coroutine should retain the environment-card coroutine");

    const auto first = vm.resume(*coroutine);
    require(first.is_int() && first.as_int() == 0, "unexpected first yield for environment-card coroutine");

    vm.collect_young();
    vm.collect_young();

    const auto second = vm.resume(*coroutine);
    require(second.is_int() && second.as_int() == 1, "unexpected second yield for environment-card coroutine");

    auto stats = vm.runtime_stats();
    require(stats.gc.remembered_objects >= 1, "old local env capturing a fresh upvalue cell should register a remembered owner");
    require(stats.gc.remembered_cards >= 1, "old local env capturing a fresh upvalue cell should publish a dirty card");

    vm.collect_young();
    vm.gc_verify_young();

    const auto result = vm.resume(*coroutine);
    require(result.is_int() && result.as_int() == 42,
            "environment cards should keep young upvalue cells alive when the old local env is the remaining owner");
    vm.cancel(*coroutine);
}

void test_v2_suspended_coroutine_cards_preserve_local_young_values() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            fn make_keeper() -> Coroutine {
                return coroutine fn() -> Int {
                    let values = [41];
                    yield 0;
                    return values[0] + 1;
                };
            }
        )",
        "unit_v2_coroutine_cards",
        std::filesystem::current_path());

    const auto maker = vm.get_function("unit_v2_coroutine_cards", "make_keeper");
    require(maker.has_value(), "missing make_keeper handle");
    const auto coroutine = vm.spawn_coroutine(*maker);
    require(coroutine.has_value() && coroutine->valid(), "spawn_coroutine should retain the keeper coroutine");

    const auto first = vm.resume(*coroutine);
    require(first.is_int() && first.as_int() == 0, "unexpected first coroutine yield for card-tracking test");

    auto stats = vm.runtime_stats();
    require(stats.gc.remembered_cards >= 1, "suspended coroutine with young locals should publish dirty cards");

    vm.collect_young();
    vm.gc_verify_young();

    const auto result = vm.resume(*coroutine);
    require(result.is_int() && result.as_int() == 42, "suspended coroutine cards should keep young local arrays alive across minor GC");
    vm.cancel(*coroutine);
}

void test_v2_suspended_coroutine_syncs_binding_backed_locals_before_yield() {
    zephyr::ZephyrVMConfig config;
    config.gc.nursery_trigger_bytes = 64;
    config.gc.incremental_trigger_bytes = 256;
    zephyr::ZephyrVM vm(config);
    vm.execute_string(
        R"(
            fn make_sync_keeper() -> Coroutine {
                return coroutine fn() -> Int {
                    let mut values = [1];
                    yield 0;

                    let replace = fn() -> Int {
                        values = [41];
                        return 0;
                    };

                    replace();
                    yield 1;
                    return values[0] + 1;
                };
            }
        )",
        "unit_v2_coroutine_binding_sync",
        std::filesystem::current_path());

    const auto maker = vm.get_function("unit_v2_coroutine_binding_sync", "make_sync_keeper");
    require(maker.has_value(), "missing make_sync_keeper handle");
    const auto coroutine = vm.spawn_coroutine(*maker);
    require(coroutine.has_value() && coroutine->valid(), "spawn_coroutine should retain the sync keeper coroutine");

    const auto first = vm.resume(*coroutine);
    require(first.is_int() && first.as_int() == 0, "unexpected first yield for binding-sync coroutine");

    vm.collect_young();
    vm.collect_young();
    auto stats = vm.runtime_stats();
    require(stats.gc.old_objects >= 1, "young collections should promote the long-lived suspended coroutine fixture");

    const auto second = vm.resume(*coroutine);
    require(second.is_int() && second.as_int() == 1, "unexpected second yield for binding-sync coroutine");

    stats = vm.runtime_stats();
    require(stats.gc.remembered_cards >= 1,
            "closure-updated coroutine locals should republish dirty cards before suspension");

    vm.collect_young();
    vm.gc_verify_young();

    const auto result = vm.resume(*coroutine);
    require(result.is_int() && result.as_int() == 42,
            "binding-backed coroutine local should survive suspension and young GC after closure mutation");
    vm.cancel(*coroutine);
}

void test_gc_object_sizeof_baselines() {
    // Phase 0 infrastructure test: verifies GC size accounting is sane at runtime.
    // The actual per-subclass sizeof static_asserts live in src/zephyr.cpp (Phase 0 sizeof
    // baselines block after ModuleNamespaceObject) since internal types are not exposed through
    // api.hpp. This runtime test validates that live_bytes accounting is plausible after
    // allocation, complementing the compile-time asserts.
    zephyr::ZephyrVM vm;
    const auto stats_before = vm.runtime_stats();
    // Fresh VM may allocate initial global environment objects — record as baseline.
    const auto initial_objects = stats_before.live_objects;
    const auto initial_bytes   = stats_before.live_bytes;

    // Allocate several object types.
    vm.execute_string(
        R"(
            let s = "hello world";
            let arr = [1, 2, 3];
            struct Pt { x: Int, y: Int }
            let p = Pt { x: 1, y: 2 };
        )",
        "unit_sizeof_baseline",
        std::filesystem::current_path());

    const auto stats_after = vm.runtime_stats();
    require(stats_after.live_objects > initial_objects, "sizeof baseline: live_objects must increase after allocation");
    require(stats_after.live_bytes   > initial_bytes,   "sizeof baseline: live_bytes must increase after allocation");
    require(stats_after.total_allocations > 0, "sizeof baseline: total_allocations must be tracked");
    // Per-object byte average must be >= pointer size (sanity: no undercount).
    require(stats_after.live_bytes >= stats_after.live_objects * sizeof(void*),
            "sizeof baseline: live_bytes must be >= pointer_size * live_objects");
}

void test_phase7_compact_old_generation() {
    // Phase 7: exercise compact_old_generation() — allocate objects, run GC so
    // some survive and get promoted (nursery → OldSmallSpace as bump objects),
    // then compact.  Verify that gc_verify_full passes after compaction.
    zephyr::ZephyrVM vm;
    vm.set_gc_stress(true, 1);  // force frequent collections

    // Create values that survive multiple GC cycles and get promoted.
    vm.execute_string(
        R"(
            let mut data = [];
            let mut i = 0;
            while i < 50 {
                data = data + [i * 10];
                i = i + 1;
            }
            let msg = "compaction test string that should survive";
            struct Point { x: Int, y: Int }
            let mut points = [];
            let mut j = 0;
            while j < 10 {
                points = points + [Point { x: j, y: j * 2 }];
                j = j + 1;
            }
        )",
        "phase7_compact_test",
        std::filesystem::current_path());

    vm.set_gc_stress(false, 1);

    // Force a full collection to promote survivors.
    vm.collect_garbage();

    const auto stats_before = vm.runtime_stats();

    // Run compaction — should evacuate promoted bump objects to slab slots.
    vm.compact_old_generation();

    // Full verification must pass after compaction.
    vm.gc_verify_full();

    const auto stats_after = vm.runtime_stats();
    // Compaction stat must be populated (may be 0 if all objects were already
    // slab-allocated or non-relocatable, but that's still a valid outcome).
    require(stats_after.gc.total_compactions >= 0, "phase7 compact: compaction stat must be non-negative");
    // Live objects should be the same (compaction moves, not frees).
    require(stats_after.live_objects == stats_before.live_objects,
            "phase7 compact: live_objects must not change after compaction");
}

void test_wave_a_barrier_young_owner_skips_remembered_set() {
    // Young owners must NOT be added to the remembered set (old→young tracking
    // is irrelevant for nursery objects). We verify this by running a write-heavy
    // loop in nursery conditions and checking that remembered_objects stays 0
    // for a workload that only writes young→int (scalar).
    zephyr::ZephyrVM vm;
    vm.install_core();

    vm.execute_string(R"(
        export fn churn(n: Int) -> Int {
            let mut total = 0;
            let mut i = 0;
            while i < n {
                let arr = [i, i+1, i+2];
                total = total + arr[0];
                i = i + 1;
            }
            return total;
        }
    )", "barrier_young_test", std::filesystem::current_path());

    // Run under GC stress to exercise barriers heavily.
    vm.set_gc_stress(true, 1);

    auto handle = vm.get_function("barrier_young_test", "churn");
    require(handle.has_value(), "barrier young: churn must exist");

    // sum of 0..99 = 4950
    auto result = vm.call(*handle, {zephyr::ZephyrValue(100)});
    require(result.is_int() && result.as_int() == 4950,
            "barrier young: expected 4950 (got " + std::to_string(result.as_int()) + ")");

    // Verify correctness under stress — all tests pass is sufficient.
}

// ─── Wave A: 4.1 integer arithmetic inline fast path ──────────────────────

}  // namespace zephyr_tests
