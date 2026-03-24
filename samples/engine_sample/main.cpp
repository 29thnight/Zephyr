#include "zephyr/api.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

struct Actor {
    std::string name;
    int hp = 100;
};

int main() {
    try {
        zephyr::ZephyrVM vm;
        std::string damage_handler_name;
        std::string failing_handler_name;

        auto actor_class = std::make_shared<zephyr::ZephyrHostClass>("Actor");
        actor_class->add_property(
            "name",
            [](void* instance) {
                return zephyr::ZephyrValue(static_cast<Actor*>(instance)->name);
            });
        actor_class->add_property(
            "hp",
            [](void* instance) {
                return zephyr::ZephyrValue(static_cast<Actor*>(instance)->hp);
            },
            [](void* instance, const zephyr::ZephyrValue& value) {
                static_cast<Actor*>(instance)->hp = static_cast<int>(value.as_int());
            });
        actor_class->add_method(
            "damage",
            [](void* instance, const std::vector<zephyr::ZephyrValue>& args) {
                Actor* actor = static_cast<Actor*>(instance);
                actor->hp -= static_cast<int>(args.at(0).as_int());
                return zephyr::ZephyrValue(actor->hp);
            });

        vm.register_module(
            "engine",
            [&](zephyr::ZephyrModuleBinder& binder) {
                binder.add_function(
                    "log",
                    [](const std::vector<zephyr::ZephyrValue>& args) {
                        std::cout << "[engine] ";
                        for (const auto& value : args) {
                            std::cout << zephyr::to_string(value) << " ";
                        }
                        std::cout << std::endl;
                        return zephyr::ZephyrValue();
                    });
            });

        const auto actor = std::make_shared<Actor>(Actor{"Zephyr", 120});
        vm.register_global_function(
            "player",
            [&, actor_class, actor](const std::vector<zephyr::ZephyrValue>&) {
                return zephyr::ZephyrValue(zephyr::ZephyrHostObjectRef{actor_class, actor});
            },
            {},
            "HostObject");
        vm.register_global_function(
            "register_damage_handler",
            [&](const std::vector<zephyr::ZephyrValue>& args) {
                if (args.size() != 1 || !args[0].is_string()) {
                    throw std::runtime_error("register_damage_handler expects one String argument.");
                }
                damage_handler_name = args[0].as_string();
                return zephyr::ZephyrValue();
            },
            {"String"},
            "Nil");
        vm.register_global_function(
            "register_failure_handler",
            [&](const std::vector<zephyr::ZephyrValue>& args) {
                if (args.size() != 1 || !args[0].is_string()) {
                    throw std::runtime_error("register_failure_handler expects one String argument.");
                }
                failing_handler_name = args[0].as_string();
                return zephyr::ZephyrValue();
            },
            {"String"},
            "Nil");

        const auto script = std::filesystem::current_path() / "examples" / "engine_demo.zph";
        vm.execute_file(script);
        const auto script_key = std::filesystem::weakly_canonical(script).string();
        const auto handle = vm.get_function(script_key, "main");
        if (handle.has_value()) {
            vm.call(*handle);
        }

        const auto event_script = std::filesystem::current_path() / "examples" / "event_handling.zph";
        vm.execute_file(event_script);
        const auto event_key = std::filesystem::weakly_canonical(event_script).string();
        const auto event_main = vm.get_function(event_key, "main");
        if (event_main.has_value()) {
            vm.call(*event_main);
        }

        if (!damage_handler_name.empty()) {
            const auto damage_handler = vm.get_function(event_key, damage_handler_name);
            if (damage_handler.has_value()) {
                const auto event_result = vm.call(*damage_handler, {zephyr::ZephyrValue(7)});
                if (event_result.is_int()) {
                    std::cout << "damage handler result: " << event_result.as_int() << std::endl;
                }
            }
        }

        if (!failing_handler_name.empty()) {
            const auto failing_handler = vm.get_function(event_key, failing_handler_name);
            if (failing_handler.has_value()) {
                try {
                    vm.call(*failing_handler, {zephyr::ZephyrValue(7)});
                } catch (const std::exception& error) {
                    std::cout << "event handler error: " << error.what() << std::endl;
                }
            }
        }

        std::cout << "player hp after script: " << actor->hp << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
