#include "test_common.hpp"

namespace zephyr_tests {

void test_dap_server_smoke() {
    zephyr::ZephyrVM vm;
    vm.set_breakpoint({"unit_dap", 3});
    vm.start_dap_server(4711);
    vm.clear_breakpoints();
    vm.stop_dap_server();
}

void test_snapshot_restore() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            let mut score = 7;
            let mut values = [1, 2, 3];

            fn read_score() -> int {
                return score + values[1];
            }

            fn mutate() -> int {
                score = 40;
                values[1] = 9;
                return read_score();
            }
        )",
        "unit_snapshot",
        std::filesystem::current_path());

    const auto read_handle = vm.get_function("unit_snapshot", "read_score");
    const auto mutate_handle = vm.get_function("unit_snapshot", "mutate");
    require(read_handle.has_value(), "missing snapshot read handle");
    require(mutate_handle.has_value(), "missing snapshot mutate handle");

    const auto baseline = vm.call(*read_handle);
    require(baseline.is_int() && baseline.as_int() == 9, "snapshot baseline read returned wrong result");

    const auto snap = vm.snapshot();
    require(!snap.data.empty(), "snapshot payload should not be empty");

    const auto mutated = vm.call(*mutate_handle);
    require(mutated.is_int() && mutated.as_int() == 49, "snapshot mutation should update globals");

    require(vm.restore_snapshot(snap), "snapshot restore should succeed");
    const auto restored = vm.call(*read_handle);
    require(restored.is_int() && restored.as_int() == 9, "snapshot restore should roll globals back");
}

void test_host_object() {
    struct Counter {
        int value = 0;
    };

    zephyr::ZephyrVM vm;
    auto klass = std::make_shared<zephyr::ZephyrHostClass>("Counter");
    klass->add_property(
        "value",
        [](void* instance) { return zephyr::ZephyrValue(static_cast<Counter*>(instance)->value); },
        [](void* instance, const zephyr::ZephyrValue& value) { static_cast<Counter*>(instance)->value = static_cast<int>(value.as_int()); });
    klass->add_method(
        "inc",
        [](void* instance, const std::vector<zephyr::ZephyrValue>&) {
            Counter* counter = static_cast<Counter*>(instance);
            counter->value += 1;
            return zephyr::ZephyrValue(counter->value);
        });

    const auto counter = std::make_shared<Counter>();
    vm.register_global_function(
        "counter",
        [klass, counter](const std::vector<zephyr::ZephyrValue>&) {
            return zephyr::ZephyrValue(zephyr::ZephyrHostObjectRef{klass, counter});
        },
        {},
        "HostObject");

    vm.execute_string(
        R"(
            fn touch() -> int {
                let c = counter();
                c.inc();
                c.value = c.value + 10;
                return c.value;
            }
        )",
        "unit_host",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_host", "touch");
    require(handle.has_value(), "missing touch handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 11, "unexpected host object result");
}

void test_host_object_identity_and_long_lived_handle() {
    struct Counter {
        int value = 0;
    };

    auto klass = std::make_shared<zephyr::ZephyrHostClass>("Counter");
    klass->add_property(
        "value",
        [](void* instance) { return zephyr::ZephyrValue(static_cast<Counter*>(instance)->value); },
        [](void* instance, const zephyr::ZephyrValue& value) { static_cast<Counter*>(instance)->value = static_cast<int>(value.as_int()); });
    klass->add_method(
        "inc",
        [](void* instance, const std::vector<zephyr::ZephyrValue>&) {
            Counter* counter = static_cast<Counter*>(instance);
            counter->value += 1;
            return zephyr::ZephyrValue(counter->value);
        });

    const auto counter = std::make_shared<Counter>();
    const zephyr::ZephyrValue retained_handle(zephyr::ZephyrHostObjectRef{klass, counter});

    zephyr::ZephyrVM vm;
    vm.register_global_function(
        "counter",
        [klass, counter](const std::vector<zephyr::ZephyrValue>&) {
            return zephyr::ZephyrValue(zephyr::ZephyrHostObjectRef{klass, counter});
        },
        {},
        "HostObject");
    vm.register_global_function(
        "force_gc",
        [&vm](const std::vector<zephyr::ZephyrValue>&) {
            vm.collect_garbage();
            return zephyr::ZephyrValue();
        },
        {}, "Nil");

    vm.execute_string(
        R"(
            fn same_counter() -> bool {
                let a = counter();
                force_gc();
                let b = counter();
                return a == b;
            }

            fn touch_retained(c: HostObject) -> int {
                force_gc();
                c.inc();
                return c.value;
            }
        )",
        "unit_host_retention",
        std::filesystem::current_path());

    const auto same_handle = vm.get_function("unit_host_retention", "same_counter");
    require(same_handle.has_value(), "missing same_counter handle");
    const auto same_result = vm.call(*same_handle);
    require(same_result.is_bool() && same_result.as_bool(), "same native host instance should compare equal");

    vm.collect_garbage();

    const auto retained_handle_fn = vm.get_function("unit_host_retention", "touch_retained");
    require(retained_handle_fn.has_value(), "missing touch_retained handle");
    const auto retained_result = vm.call(*retained_handle_fn, {retained_handle});
    require(retained_result.is_int() && retained_result.as_int() == 1, "retained host handle did not survive GC round-trip");
    require(counter->value == 1, "retained host handle did not mutate native instance");
}

void test_name_based_event_registration_and_error_propagation() {
    zephyr::ZephyrVM vm;
    std::string damage_handler_name;
    std::string failing_handler_name;

    vm.register_global_function(
        "register_damage_handler",
        [&](const std::vector<zephyr::ZephyrValue>& args) {
            damage_handler_name = args.at(0).as_string();
            return zephyr::ZephyrValue();
        },
        {"string"}, "Nil");
    vm.register_global_function(
        "register_failure_handler",
        [&](const std::vector<zephyr::ZephyrValue>& args) {
            failing_handler_name = args.at(0).as_string();
            return zephyr::ZephyrValue();
        },
        {"string"}, "Nil");

    vm.execute_string(
        R"(
            fn on_damage(amount: int) -> int {
                return amount + 3;
            }

            fn on_fault(amount: int) -> int {
                assert(amount < 0);
                return amount;
            }

            fn main() {
                register_damage_handler("on_damage");
                register_failure_handler("on_fault");
            }
        )",
        "unit_events",
        std::filesystem::current_path());

    const auto main_handle = vm.get_function("unit_events", "main");
    require(main_handle.has_value(), "missing main handle for event registration test");
    vm.call(*main_handle);

    require(damage_handler_name == "on_damage", "damage handler name should be registered");
    require(failing_handler_name == "on_fault", "failure handler name should be registered");

    const auto damage_handle = vm.get_function("unit_events", damage_handler_name);
    require(damage_handle.has_value(), "registered damage handler must be discoverable");
    const auto damage_result = vm.call(*damage_handle, {zephyr::ZephyrValue(7)});
    require(damage_result.is_int() && damage_result.as_int() == 10, "registered damage handler returned wrong result");

    const auto fault_handle = vm.get_function("unit_events", failing_handler_name);
    require(fault_handle.has_value(), "registered failure handler must be discoverable");
    bool fault_propagated = false;
    try {
        vm.call(*fault_handle, {zephyr::ZephyrValue(7)});
    } catch (const std::exception&) {
        fault_propagated = true;
    }
    require(fault_propagated, "failing event handler should propagate an error back to host");
}

void test_frame_handle_storage_and_capture_are_rejected() {
    struct Counter {
        int value = 7;
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

    bool global_rejected = false;
    try {
        vm.execute_string("let cached = make_frame_counter();", "unit_frame_global", std::filesystem::current_path());
    } catch (const std::exception&) {
        global_rejected = true;
    }
    require(global_rejected, "frame handle should not be storable in module/global scope");

    vm.execute_string(
        R"(
            fn build_bad_closure() -> int {
                let temp = make_frame_counter();
                let reader = fn() -> int {
                    return temp.value;
                };
                return reader();
            }
        )",
        "unit_frame_capture",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_frame_capture", "build_bad_closure");
    require(handle.has_value(), "missing build_bad_closure handle");

    bool closure_rejected = false;
    try {
        vm.call(*handle);
    } catch (const std::exception&) {
        closure_rejected = true;
    }
    require(closure_rejected, "frame handle should not be capturable by closure");
}

void test_handle_invalidation_and_epochs() {
    struct Counter {
        int value = 9;
    };

    auto klass = std::make_shared<zephyr::ZephyrHostClass>("Counter");
    klass->add_property("value", [](void* instance) { return zephyr::ZephyrValue(static_cast<Counter*>(instance)->value); });

    const auto persistent_counter = std::make_shared<Counter>();
    const auto frame_counter = std::make_shared<Counter>();
    const auto stable_counter = std::make_shared<Counter>();

    zephyr::ZephyrHostObjectRef persistent_ref{klass, persistent_counter};
    persistent_ref.kind = zephyr::ZephyrHostHandleKind::Entity;
    persistent_ref.lifetime = zephyr::ZephyrHostHandleLifetime::Persistent;

    zephyr::ZephyrHostObjectRef frame_ref{klass, frame_counter};
    frame_ref.kind = zephyr::ZephyrHostHandleKind::Entity;
    frame_ref.lifetime = zephyr::ZephyrHostHandleLifetime::Frame;

    zephyr::ZephyrHostObjectRef stable_ref{klass, stable_counter};
    stable_ref.kind = zephyr::ZephyrHostHandleKind::Asset;
    stable_ref.lifetime = zephyr::ZephyrHostHandleLifetime::Stable;
    stable_ref.stable_guid = zephyr::ZephyrGuid128{0x1234ULL, 0x5678ULL};

    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn read(counter: HostObject) -> int {
                return counter.value;
            }
        )",
        "unit_handle_policy",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_handle_policy", "read");
    require(handle.has_value(), "missing read handle");

    const auto persistent_value = zephyr::ZephyrValue(persistent_ref);
    const auto frame_value = zephyr::ZephyrValue(frame_ref);
    const auto stable_value = zephyr::ZephyrValue(stable_ref);

    const auto first_persistent = vm.call(*handle, {persistent_value});
    require(first_persistent.is_int() && first_persistent.as_int() == 9, "persistent handle should resolve before invalidation");

    vm.invalidate_host_handle(persistent_value.as_host_object());
    bool invalidation_rejected = false;
    try {
        vm.call(*handle, {persistent_value});
    } catch (const std::exception&) {
        invalidation_rejected = true;
    }
    require(invalidation_rejected, "invalidated persistent handle should fault");

    const auto first_frame = vm.call(*handle, {frame_value});
    require(first_frame.is_int() && first_frame.as_int() == 9, "frame handle should resolve within current frame");
    vm.advance_frame();
    bool frame_expired = false;
    try {
        vm.call(*handle, {frame_value});
    } catch (const std::exception&) {
        frame_expired = true;
    }
    require(frame_expired, "frame handle should expire after frame advance");

    const auto first_stable = vm.call(*handle, {stable_value});
    require(first_stable.is_int() && first_stable.as_int() == 9, "stable handle should resolve before scene advance");
    vm.advance_scene();
    const auto second_stable = vm.call(*handle, {stable_value});
    require(second_stable.is_int() && second_stable.as_int() == 9, "stable handle should survive scene advance");
}

void test_v2_callback_handle_and_dump_bytecode() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn add_one(value: int) -> int {
                return value + 1;
            }
        )",
        "unit_v2_callback",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_v2_callback", "add_one");
    require(handle.has_value(), "missing add_one handle");

    const auto callback = vm.capture_callback(*handle);
    require(callback.valid(), "callback handle should be valid");

    const auto result = vm.call(callback, {zephyr::ZephyrValue(41)});
    require(result.is_int() && result.as_int() == 42, "retained callback handle returned unexpected result");

    const auto dump = vm.dump_bytecode("unit_v2_callback", "add_one");
    require(dump.find("chunk unit_v2_callback::add_one") != std::string::npos, "dump-bytecode should mention the function chunk");
    require(dump.find("histogram") != std::string::npos, "dump-bytecode should include opcode histogram");

    vm.release_callback(callback);
}

void test_wave_e1_class_binder() {
    struct Player {
        int hp = 12;

        int damage(int amount) {
            hp -= amount;
            return hp;
        }
    };

    zephyr::ZephyrVM vm;
    vm.bind<Player>("Player").method("damage", &Player::damage).prop("hp", &Player::hp);

    const auto player = std::make_shared<Player>();
    const auto handle = vm.make_host_object(player);
    vm.register_global_function(
        "make_player",
        [handle](const std::vector<zephyr::ZephyrValue>&) { return handle; },
        {},
        "HostObject");

    vm.execute_string(
        R"(
            fn run() -> int {
                let player = make_player();
                player.damage(4);
                return player.hp;
            }
        )",
        "wave_e1_binder",
        std::filesystem::current_path());

    const auto run = vm.get_function("wave_e1_binder", "run");
    require(run.has_value(), "wave e1: binder run must exist");
    const auto result = vm.call(*run);
    require(result.is_int() && result.as_int() == 8, "wave e1: class binder method/property binding returned wrong result");
}

}  // namespace zephyr_tests
