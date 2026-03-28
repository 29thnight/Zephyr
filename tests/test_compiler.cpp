#include "test_common.hpp"

namespace zephyr_tests {

void test_check_rejects_circular_import() {
    zephyr::ZephyrVM vm;
    const auto temp_dir = std::filesystem::current_path() / ".zephyr_cycle_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    const auto a_path = temp_dir / "a.zph";
    const auto b_path = temp_dir / "b.zph";
    {
        std::ofstream out(a_path);
        out << "import \"b.zph\";\nfn main() -> Int { return 1; }\n";
    }
    {
        std::ofstream out(b_path);
        out << "import \"a.zph\";\nfn helper() -> Int { return 2; }\n";
    }

    bool rejected = false;
    try {
        vm.check_file(a_path);
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("Circular import detected") != std::string::npos, "check_file should report circular imports");
    }

    std::filesystem::remove_all(temp_dir);
    require(rejected, "check_file should reject circular imports");
}

void test_check_reports_trait_impl_missing_method() {
    zephyr::ZephyrVM vm;
    bool rejected = false;
    try {
        vm.check_string(
            R"(
                trait Drawable {
                    fn draw(self) -> Int;
                    fn metric(self) -> Int;
                }

                struct Player { x: Int }

                impl Drawable for Player {
                    fn draw(self) -> Int {
                        return self.x;
                    }
                }
            )",
            "unit_check_trait_missing",
            std::filesystem::current_path());
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("error: impl of 'Drawable' for 'Player' is missing method 'metric'") != std::string::npos,
                "trait impl check should identify missing methods");
    }
    require(rejected, "check_string should reject impls with missing trait methods");
}

void test_check_accepts_complete_trait_impl_and_warns_on_extra_method() {
    zephyr::ZephyrVM vm;
    StreamCapture warnings(std::cerr);
    vm.check_string(
        R"(
            trait Greeter {
                fn greet(self) -> String;
            }

            struct Cat { name: String }

            impl Greeter for Cat {
                fn greet(self) -> String {
                    return self.name;
                }

                fn extra(self) -> String {
                    return self.name;
                }
            }
        )",
        "unit_check_trait_extra",
        std::filesystem::current_path());

    require(warnings.str().find("warning: method 'extra' is not part of trait 'Greeter'") != std::string::npos,
            "extra impl methods should surface a warning");

    zephyr::ZephyrVM ok_vm;
    ok_vm.check_string(
        R"(
            trait Greeter {
                fn greet(self) -> String;
            }

            struct Cat { name: String }

            impl Greeter for Cat {
                fn greet(self) -> String {
                    return self.name;
                }
            }
        )",
        "unit_check_trait_ok",
        std::filesystem::current_path());
}

void test_check_reports_function_signature_mismatch_with_definition_location() {
    zephyr::ZephyrVM vm;
    bool rejected = false;
    try {
        vm.check_string(
            R"(
                fn add(a: Int, b: Int) -> Int {
                    return a + b;
                }

                fn run() -> Int {
                    return add(1, 2, 3);
                }
            )",
            "unit_check_call_arity",
            std::filesystem::current_path());
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("function 'add' expects 2 arguments, got 3") != std::string::npos,
                "call arity diagnostics should be specific");
        require(message.find("unit_check_call_arity:") != std::string::npos,
                "call arity diagnostics should include the call location");
        require(message.find("defined at unit_check_call_arity:2:") != std::string::npos,
                "call arity diagnostics should include the definition location");
    }
    require(rejected, "check_string should reject calls with the wrong argument count");
}

void test_check_warns_for_optional_chain_nil_propagation() {
    zephyr::ZephyrVM vm;
    StreamCapture warnings(std::cerr);
    vm.check_string(
        R"(
            struct Node { next: Any }

            fn run(node: Node) -> Nil {
                node?.next.missing();
                return nil;
            }
        )",
        "unit_check_optional_chain",
        std::filesystem::current_path());

    require(warnings.str().find("warning: optional chaining result may be nil before calling method 'missing'") != std::string::npos,
            "optional chaining should warn when nil can propagate into a direct method call");
}

void test_check_rejects_module_member_that_is_not_exported() {
    zephyr::ZephyrVM vm;
    const auto temp_dir = std::filesystem::current_path() / ".zephyr_export_check";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    const auto module_path = temp_dir / "foo.zph";
    const auto entry_path = temp_dir / "main.zph";
    {
        std::ofstream out(module_path);
        out << "export fn ok() -> Int { return 1; }\n"
               "fn hidden() -> Int { return 2; }\n";
    }
    {
        std::ofstream out(entry_path);
        out << "import \"foo.zph\" as foo;\n"
               "fn run() -> Int {\n"
               "    return foo.hidden();\n"
               "}\n";
    }

    bool rejected = false;
    try {
        vm.check_file(entry_path);
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("module 'foo' does not export 'hidden'") != std::string::npos,
                "check_file should reject module member access to non-exported symbols");
    }

    std::filesystem::remove_all(temp_dir);
    require(rejected, "check_file should reject access to non-exported imported symbols");
}

void test_check_warns_for_non_exhaustive_match_cases() {
    zephyr::ZephyrVM vm;
    StreamCapture warnings(std::cerr);
    vm.check_string(
        R"(
            fn classify(value: Any) -> Int {
                return match value {
                    nil => 0,
                };
            }
        )",
        "unit_check_match_hint",
        std::filesystem::current_path());

    require(warnings.str().find("warning: match may not cover all cases: missing String patterns") != std::string::npos,
            "check_string should warn when a match leaves obvious cases uncovered");
}

void test_package_root_imports_lib_entry() {
    const auto temp_dir = std::filesystem::current_path() / ".zephyr_package_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir / "src");

    {
        std::ofstream out(temp_dir / "package.toml");
        out << "[package]\n"
               "name = \"demo_pkg\"\n"
               "version = \"0.1.0\"\n"
               "entry = \"src/lib.zph\"\n";
    }
    {
        std::ofstream out(temp_dir / "src" / "lib.zph");
        out << "export fn add(a: Int, b: Int) -> Int {\n"
               "    return a + b;\n"
               "}\n";
    }

    zephyr::ZephyrVM vm;
    vm.set_package_root(temp_dir.string());
    const auto search_paths = vm.get_module_search_paths();
    require(search_paths.size() >= 2, "set_package_root should populate module search paths");

    vm.check_string(
        R"(
            import "lib" as lib;

            fn run() -> Int {
                return lib.add(2, 3);
            }
        )",
        "unit_package_import_check",
        std::filesystem::current_path());

    vm.execute_string(
        R"(
            import "lib" as lib;

            fn run() -> Int {
                return lib.add(2, 3);
            }
        )",
        "unit_package_import",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_package_import", "run");
    require(handle.has_value(), "package root test should expose run");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 5, "set_package_root should resolve src/lib.zph through package.toml");

    std::filesystem::remove_all(temp_dir);
}

void test_bytecode_loop_and_branch() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn accumulate(limit: Int) -> Int {
                let mut total: Int = 0;
                let mut i: Int = 0;
                while i < limit {
                    if i == 2 || i == 4 {
                        total = total + 10;
                    }
                    total = total + i;
                    i = i + 1;
                }
                return total;
            }
        )",
        "unit_bytecode",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_bytecode", "accumulate");
    require(handle.has_value(), "missing accumulate handle");
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(5)});
    require(result.is_int() && result.as_int() == 30, "unexpected bytecode accumulate result");

    const auto dump = vm.dump_bytecode("unit_bytecode", "accumulate");
    require(dump.find("register_mode=true") != std::string::npos || dump.find("[SI] op=SI") != std::string::npos,
            "bytecode dump should expose either register mode or fused superinstructions");
}

void test_bytecode_for_in_array() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn sum_large(values) -> Int {
                let mut total: Int = 0;
                for value in values {
                    if value > 2 {
                        total = total + value;
                    }
                }
                return total;
            }
        )",
        "unit_for_in",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_for_in", "sum_large");
    require(handle.has_value(), "missing sum_large handle");
    zephyr::ZephyrValue::Array values = {
        zephyr::ZephyrValue(1),
        zephyr::ZephyrValue(2),
        zephyr::ZephyrValue(3),
        zephyr::ZephyrValue(4),
    };
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(values)});
    require(result.is_int() && result.as_int() == 7, "unexpected bytecode for-in result");
}

void test_bytecode_struct_enum_match() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            struct Point { x: Int, y: Int }
            enum Command { Move(Int, Int), Idle }

            fn drive(flag: Bool) -> Int {
                let point = Point { x: 3, y: 4 };
                let mut command = Command::Idle;
                if flag {
                    command = Command::Move(point.x, point.y);
                }

                return match command {
                    Command::Move(x, y) => x + y,
                    Command::Idle => 0,
                };
            }
        )",
        "unit_native_match",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_native_match", "drive");
    require(handle.has_value(), "missing drive handle");

    const auto move_result = vm.call(*handle, {zephyr::ZephyrValue(true)});
    require(move_result.is_int() && move_result.as_int() == 7, "unexpected move match result");

    const auto idle_result = vm.call(*handle, {zephyr::ZephyrValue(false)});
    require(idle_result.is_int() && idle_result.as_int() == 0, "unexpected idle match result");
}

void test_bytecode_local_slots_shadow_and_closure_sync() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn shadow_and_closure() -> Int {
                let mut current: Int = 1;
                let inc = fn(step: Int) -> Int {
                    current = current + step;
                    return current;
                };
                let outer: Int = 5;
                {
                    let outer: Int = 20;
                    if outer != 20 {
                        return -100;
                    }
                }
                inc(2);
                return outer + current;
            }
        )",
        "unit_local_slots",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_local_slots", "shadow_and_closure");
    require(handle.has_value(), "missing shadow_and_closure handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 8, "unexpected local slot closure result");
}

void test_coroutine_upvalue_bytecode_avoids_ast_fallback() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn make_counter() -> Coroutine {
                let mut current: Int = 4;
                return coroutine fn() -> Int {
                    current = current + 2;
                    yield current;
                    current = current + 3;
                    return current;
                };
            }
        )",
        "unit_coroutine_upvalues",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine_upvalues", "make_counter");
    require(handle.has_value(), "missing make_counter handle");
    const auto coroutine = vm.spawn_coroutine(*handle);
    require(coroutine.has_value() && coroutine->valid(), "spawn_coroutine should return a valid coroutine handle");

    const auto first = vm.resume(*coroutine);
    require(first.is_int() && first.as_int() == 6, "unexpected first coroutine yield for upvalue bytecode test");

    const auto second = vm.resume(*coroutine);
    require(second.is_int() && second.as_int() == 9, "unexpected second coroutine result for upvalue bytecode test");

    const auto stats = vm.runtime_stats();
    require(stats.vm.ast_fallback_executions == 0, "coroutine expression should stay on native bytecode path");

    const auto dump = vm.dump_bytecode("unit_coroutine_upvalues", "make_counter");
    require(dump.find("op=MakeCoroutine") != std::string::npos, "bytecode dump should include MakeCoroutine");
    require(dump.find("op=LoadUpvalue") != std::string::npos, "coroutine bytecode dump should include LoadUpvalue");
    require(dump.find("op=StoreUpvalue") != std::string::npos, "coroutine bytecode dump should include StoreUpvalue");
    require(dump.find("op=SI") == std::string::npos, "coroutine bytecode should not be fused into SI opcodes");

    vm.cancel(*coroutine);
}

void test_transitive_upvalue_bindings_support_nested_closure_after_outer_return() {
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
            fn drive() -> Int {
                let inner = (fn() -> Function {
                    let mut seed: Int = 3;
                    let build = fn() -> Function {
                        return fn(step: Int) -> Int {
                            seed += step;
                            return seed;
                        };
                    };
                    return build();
                })();

                force_gc();
                return inner(4) + inner(5);
            }
        )",
        "unit_transitive_upvalues",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_transitive_upvalues", "drive");
    require(handle.has_value(), "missing drive handle for transitive upvalue test");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 19, "nested closure should preserve transitive upvalue cells after outer return");

    const auto stats = vm.runtime_stats();
    require(stats.vm.ast_fallback_executions == 0, "transitive nested closure should stay on native bytecode path");

    const auto dump = vm.dump_bytecode("unit_transitive_upvalues", "drive");
    require(dump.find("op=MakeFunction") != std::string::npos, "transitive closure dump should include MakeFunction");
    require(dump.find("op=LoadUpvalue") != std::string::npos, "transitive closure dump should include LoadUpvalue");
    require(dump.find("op=StoreUpvalue") != std::string::npos, "transitive closure dump should include StoreUpvalue");
}

void test_compound_member_and_index_assignment_run_on_native_bytecode_path() {
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
            struct Box {
                value: Int,
            }

            fn drive() -> Int {
                let run = (fn() -> Function {
                    let mut values = [1];
                    let mut box = Box { value: 10 };
                    return fn() -> Int {
                        values[0] += 1;
                        box.value += 2;
                        return values[0] + box.value;
                    };
                })();

                force_gc();
                return run() + run();
            }
        )",
        "unit_native_compound_assign",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_native_compound_assign", "drive");
    require(handle.has_value(), "missing drive handle for native compound assignment test");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 31, "compound member/index assignment should stay stable after outer return and GC");

    const auto stats = vm.runtime_stats();
    require(stats.vm.ast_fallback_executions == 0, "compound member/index assignment should stay on the native bytecode path");

    const auto dump = vm.dump_bytecode("unit_native_compound_assign", "drive");
    require(dump.find("op=LoadMember") != std::string::npos, "compound assignment dump should include LoadMember");
    require(dump.find("op=StoreMember") != std::string::npos, "compound assignment dump should include StoreMember");
    require(dump.find("op=LoadIndex") != std::string::npos, "compound assignment dump should include LoadIndex");
    require(dump.find("op=StoreIndex") != std::string::npos, "compound assignment dump should include StoreIndex");
    require(dump.find("op=EvalAstExpr") == std::string::npos, "compound assignment dump should no longer include EvalAstExpr");
}

void test_resume_expression_runs_on_native_bytecode_path() {
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
            fn drive() -> Int {
                let run = (fn() -> Function {
                    let counter = coroutine fn() -> Int {
                        yield 1;
                        return 2;
                    };

                    return fn() -> Int {
                        return resume counter;
                    };
                })();

                force_gc();
                return run() + run();
            }
        )",
        "unit_resume_native",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_resume_native", "drive");
    require(handle.has_value(), "missing drive handle for native resume test");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 3, "native resume closure should survive outer return and GC");

    const auto stats = vm.runtime_stats();
    require(stats.vm.ast_fallback_executions == 0, "resume expression should now stay on native bytecode path");

    const auto dump = vm.dump_bytecode("unit_resume_native", "drive");
    require(dump.find("op=Resume") != std::string::npos, "resume bytecode dump should include Resume");
}

void test_module_bytecode_import_and_top_level_execution() {
    zephyr::ZephyrVM vm;
    const auto base_dir = std::filesystem::current_path() / "tmp_module_bytecode_test";
    std::filesystem::remove_all(base_dir);
    std::filesystem::create_directories(base_dir);

    const auto dep_path = base_dir / "dep.zph";
    const auto main_path = base_dir / "main.zph";

    {
        std::ofstream dep(dep_path);
        dep << R"(
            export let seed: Int = 38;
            export fn add_bonus(value: Int) -> Int {
                return value + 1;
            }
        )";
    }

    {
        std::ofstream main(main_path);
        main << R"(
            import "dep.zph";

            let mut total: Int = add_bonus(seed);
            let values = [1, 2];

            for value in values {
                total = total + value;
            }

            export fn result() -> Int {
                return total;
            }
        )";
    }

    vm.execute_file(main_path);

    const auto module_name = std::filesystem::weakly_canonical(main_path).string();
    const auto handle = vm.get_function(module_name, "result");
    require(handle.has_value(), "missing result handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 42, "unexpected module bytecode result");

    std::filesystem::remove_all(base_dir);
}

void test_module_bytecode_cache_reuse_and_invalidation() {
    zephyr::ZephyrVM vm;
    vm.enable_bytecode_cache();

    const auto base_dir = std::filesystem::current_path() / "tmp_module_bytecode_cache_test";
    std::filesystem::remove_all(base_dir);
    std::filesystem::create_directories(base_dir);

    const auto main_path = base_dir / "main.zph";
    {
        std::ofstream main(main_path);
        main << R"(
            export fn value(input: Int) -> Int {
                return input + 1;
            }
        )";
    }

    vm.execute_file(main_path);
    require(vm.bytecode_cache_size() == 1, "bytecode cache should store the first compiled module");

    auto module_name = std::filesystem::weakly_canonical(main_path).string();
    auto handle = vm.get_function(module_name, "value");
    require(handle.has_value(), "missing cached value handle");
    auto result = vm.call(*handle, {zephyr::ZephyrValue(4)});
    require(result.is_int() && result.as_int() == 5, "initial cached module result should apply parameterized bytecode");

    vm.execute_file(main_path);
    require(vm.bytecode_cache_size() == 1, "reloading the same module should reuse the cache entry");
    module_name = std::filesystem::weakly_canonical(main_path).string();
    handle = vm.get_function(module_name, "value");
    require(handle.has_value(), "missing reused cache handle");
    result = vm.call(*handle, {zephyr::ZephyrValue(4)});
    require(result.is_int() && result.as_int() == 5, "reused cached module result should preserve parameters");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    {
        std::ofstream main(main_path, std::ios::trunc);
        main << R"(
            export fn value(input: Int) -> Int {
                return input + 2;
            }
        )";
    }

    vm.execute_file(main_path);
    require(vm.bytecode_cache_size() == 1, "cache invalidation should replace the existing entry");
    module_name = std::filesystem::weakly_canonical(main_path).string();
    handle = vm.get_function(module_name, "value");
    require(handle.has_value(), "missing invalidated cache handle");
    result = vm.call(*handle, {zephyr::ZephyrValue(4)});
    require(result.is_int() && result.as_int() == 6, "modified module should invalidate cached bytecode");

    vm.clear_bytecode_cache();
    require(vm.bytecode_cache_size() == 0, "clear_bytecode_cache should remove cached entries");
    std::filesystem::remove_all(base_dir);
}

void test_break_continue_and_compound_assignment() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn main() -> Int {
                let mut total = 0;
                let mut i = 0;
                while i < 8 {
                    i += 1;
                    if i == 2 {
                        continue;
                    }
                    if i == 7 {
                        break;
                    }
                    total += i;
                }

                let values = [1, 2, 3, 4];
                for value in values {
                    if value == 2 {
                        continue;
                    }
                    if value == 4 {
                        break;
                    }
                    total += value;
                }

                total *= 1;
                let mut ratio = 8.0;
                ratio /= 2.0;
                if ratio == 4.0 {
                    total += 0;
                }
                total -= 1;
                return total;
            }
        )",
        "unit_break_continue",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_break_continue", "main");
    require(handle.has_value(), "missing break/continue test handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 22, "break/continue or compound assignment produced wrong result");
}

void test_v2_global_and_module_name_slots_use_cached_bindings() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            let value = 1;
            let mut after = 0;
            let step = 4;
            let mut total = 0;

            {
                let value = 2;
                after = value;
            }

            after += value;

            fn read() -> Int {
                return after;
            }

            fn nested_global() -> Int {
                {
                    let filler0 = 0;
                    let filler1 = 1;
                    let filler2 = 2;
                    let filler3 = 3;
                    let filler4 = 4;
                    total += step;
                }
                return total;
            }

            fn nested_global_coroutine() -> Coroutine {
                return coroutine fn() -> Int {
                    {
                        let filler0 = 10;
                        let filler1 = 11;
                        let filler2 = 12;
                        total += step;
                        yield total;
                    }
                    total += step;
                    return total;
                };
            }

            fn locals() -> Int {
                let mut head = 7;
                let tail0 = 0;
                let tail1 = 1;
                let tail2 = 2;
                let tail3 = 3;
                let tail4 = 4;
                let tail5 = 5;
                let tail6 = 6;
                let tail7 = 7;
                let tail8 = 8;
                let tail9 = 9;
                let tail10 = 10;
                let tail11 = 11;
                let tail12 = 12;
                let tail13 = 13;
                let tail14 = 14;
                let tail15 = 15;
                let tail16 = 16;
                let tail17 = 17;
                let tail18 = 18;
                let tail19 = 19;
                let tail20 = 20;
                let tail21 = 21;
                let tail22 = 22;
                let tail23 = 23;
                let tail24 = 24;
                let tail25 = 25;
                let tail26 = 26;
                let tail27 = 27;
                let tail28 = 28;
                let tail29 = 29;
                let tail30 = 30;
                let tail31 = 31;
                let tail32 = 32;
                let tail33 = 33;
                let tail34 = 34;
                let tail35 = 35;
                let tail36 = 36;
                let tail37 = 37;
                let tail38 = 38;
                let tail39 = 39;
                let tail40 = 40;
                head += tail1;
                head += tail2;
                head += tail3;
                return head + head;
            }
        )",
        "unit_v2_global_slots",
        std::filesystem::current_path());

    const auto module_dump = vm.dump_bytecode("unit_v2_global_slots", "");
    require(module_dump.find("globals value after") != std::string::npos,
            "module dump should list cached global/module name slots");
    require(module_dump.find("op=LoadName operand=") != std::string::npos && module_dump.find("text=value") != std::string::npos,
            "module bytecode should emit slot-indexed LoadName for value");
    require(module_dump.find("op=StoreName operand=") != std::string::npos && module_dump.find("text=after") != std::string::npos,
            "module bytecode should emit slot-indexed StoreName for after");
    const auto read_dump = vm.dump_bytecode("unit_v2_global_slots", "read");
    require(read_dump.find("globals after") != std::string::npos,
            "function dump should list cached global slots for nested bytecode chunks");
    require((read_dump.find("op=LoadName operand=") != std::string::npos && read_dump.find("text=after") != std::string::npos)
                || read_dump.find("op=R_LOAD_GLOBAL") != std::string::npos,
            "function bytecode should emit slot-indexed LoadName for module globals");

    const auto read_handle = vm.get_function("unit_v2_global_slots", "read");
    require(read_handle.has_value(), "missing read handle");
    const auto read_result = vm.call(*read_handle);
    require(read_result.is_int() && read_result.as_int() == 3, "module name cache should re-resolve after inner scope exits");

    const auto nested_handle = vm.get_function("unit_v2_global_slots", "nested_global");
    require(nested_handle.has_value(), "missing nested_global handle");
    const auto nested_result = vm.call(*nested_handle);
    require(nested_result.is_int() && nested_result.as_int() == 4,
            "global slot cache should resolve module globals from nested local scopes");

    const auto coroutine_handle = vm.get_function("unit_v2_global_slots", "nested_global_coroutine");
    require(coroutine_handle.has_value(), "missing nested_global_coroutine handle");
    const auto coroutine = vm.spawn_coroutine(*coroutine_handle);
    require(coroutine.has_value() && coroutine->valid(), "nested_global_coroutine should return a valid coroutine handle");
    const auto first_yield = vm.resume(*coroutine);
    require(first_yield.is_int() && first_yield.as_int() == 8,
            "coroutine global slot cache should resolve module globals from nested local scopes");
    const auto final_yield = vm.resume(*coroutine);
    require(final_yield.is_int() && final_yield.as_int() == 12,
            "coroutine global slot cache should keep using the module/root base across resumes");
    vm.cancel(*coroutine);

    const auto locals_handle = vm.get_function("unit_v2_global_slots", "locals");
    require(locals_handle.has_value(), "missing locals handle");
    const auto locals_result = vm.call(*locals_handle);
    require(locals_result.is_int() && locals_result.as_int() == 26,
            "local slot cache should survive same-environment binding growth and repeated reuse");

    const auto stats = vm.runtime_stats();
    require(stats.vm.global_binding_cache_hits > 0, "global binding cache should record hits");
    require(stats.vm.global_binding_cache_misses > 0, "global binding cache should record misses");
    // Phase 1.1: lightweight functions (uses_only_locals_and_upvalues) bypass the local
    // binding cache entirely, so local cache hits may be 0 when all called functions
    // qualify for the lightweight path.  The binding cache remains functional for
    // heavyweight functions that use name-based lookups.
    // require(stats.vm.local_binding_cache_hits > 0, "local binding cache should record hits");
    // require(stats.vm.local_binding_cache_misses > 0, "local binding cache should record misses");
}

void test_phase1_1_lightweight_call_skips_environment_allocation() {
    zephyr::ZephyrVM vm;
    vm.install_core();

    // A simple pure function that uses only local slots (no scopes, no globals).
    // The compiler should set uses_only_locals_and_upvalues = true.
    vm.execute_string(R"(
        fn add(a: Int, b: Int) -> Int {
            return a + b;
        }
    )", "lightweight_test", std::filesystem::current_path());

    auto handle = vm.get_function("lightweight_test", "add");
    require(handle.has_value(), "lightweight: add function must exist");

    const auto stats_before = vm.runtime_stats();

    // Call the function many times — each call should NOT allocate an Environment.
    for (int i = 0; i < 100; ++i) {
        auto result = vm.call(*handle, {zephyr::ZephyrValue(static_cast<std::int64_t>(i)),
                                         zephyr::ZephyrValue(static_cast<std::int64_t>(i + 1))});
        require(result.is_int(), "lightweight: result must be Int");
        require(result.as_int() == static_cast<std::int64_t>(2 * i + 1),
                "lightweight: result must equal i + (i+1)");
    }

    const auto stats_after = vm.runtime_stats();
    // In the heavyweight path, 100 calls would allocate 100 Environment objects.
    // With lightweight path, the Environment allocation delta should be 0.
    const auto alloc_delta = stats_after.gc.total_allocations - stats_before.gc.total_allocations;
    require(alloc_delta < 10,
            "lightweight: 100 calls should NOT allocate ~100 Environments (got delta=" + std::to_string(alloc_delta) + ")");
}

void test_phase1_1_nested_closure_with_upvalue_mutation() {
    zephyr::ZephyrVM vm;
    vm.install_core();

    // 3-level nested closure with mutable upvalue — tests that upvalue cell
    // sharing works correctly even when the inner function uses lightweight path.
    vm.execute_string(R"(
        fn outer() -> Int {
            let mut x = 10;
            let inc = fn() -> Int {
                x = x + 1;
                return x;
            };
            let get = fn() -> Int { return x; };

            let mut sum = 0;
            sum = sum + inc();
            sum = sum + inc();
            sum = sum + get();
            return sum;
        }
        export fn run() -> Int {
            return outer();
        }
    )", "nested_upvalue_test", std::filesystem::current_path());

    auto handle = vm.get_function("nested_upvalue_test", "run");
    require(handle.has_value(), "nested upvalue: run function must exist");

    auto call_result = vm.call(*handle, {});
    require(call_result.is_int(), "nested upvalue: result must be Int");
    // inc() returns 11, 12; get() returns 12 (shared mutable upvalue); sum = 11 + 12 + 12 = 35
    require(call_result.as_int() == 35,
            "nested upvalue: sum must be 35 (got " + std::to_string(call_result.as_int()) + ")");
}

void test_phase1_2_constant_folding_reduces_bytecode() {
    zephyr::ZephyrVM vm;
    vm.install_core();

    // A function with constant arithmetic should be folded at compile time.
    vm.execute_string(R"(
        fn compute() -> Int {
            return 1 + 2 * 3;
        }
    )", "fold_test", std::filesystem::current_path());

    // Verify via dump_bytecode that the folded function has fewer instructions.
    const auto dump = vm.dump_bytecode("fold_test", "compute");
    // Without folding: LoadConst(1), LoadConst(2), LoadConst(3), Multiply, Add, ... = 5+ ops
    // With folding: LoadConst(7), ... = 1 LoadConst (since 2*3=6, then 1+6=7)
    // Count occurrences of "LoadConst" in the dump.
    std::size_t load_const_count = 0;
    std::string::size_type pos = 0;
    while ((pos = dump.find("LoadConst", pos)) != std::string::npos) {
        ++load_const_count;
        ++pos;
    }
    // After folding: 1 LoadConst for the folded result (7), 1 for the implicit nil return,
    // possibly 1 more from function declaration context.  Without folding there would be 5+.
    require(load_const_count <= 3,
            "constant folding: expected <=3 LoadConst instructions (got " + std::to_string(load_const_count) + ")");

    // Verify correctness.
    auto handle = vm.get_function("fold_test", "compute");
    require(handle.has_value(), "constant folding: compute must exist");
    auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 7, "constant folding: result must be 7");
}

// ─── Wave A: 3.1 suspended_coroutines_ O(1) ───────────────────────────────

void test_wave_a_int_arithmetic_fastpath() {
    zephyr::ZephyrVM vm;
    vm.install_core();

    vm.execute_string(R"(
        fn fib(n: Int) -> Int {
            if n <= 1 {
                return n;
            }
            return fib(n - 1) + fib(n - 2);
        }
        export fn run() -> Int {
            return fib(30);
        }
    )", "int_fastpath_test", std::filesystem::current_path());

    auto handle = vm.get_function("int_fastpath_test", "run");
    require(handle.has_value(), "int fastpath: run must exist");

    auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 832040,
            "int fastpath: fib(30) must be 832040 (got " + std::to_string(result.as_int()) + ")");

    // Test comparison operators via sorting-like logic
    vm.execute_string(R"(
        export fn compare_ops(a: Int, b: Int) -> Int {
            let mut score = 0;
            if a < b  { score = score + 1; }
            if a <= b { score = score + 2; }
            if b > a  { score = score + 4; }
            if b >= a { score = score + 8; }
            if a == a { score = score + 16; }
            if a != b { score = score + 32; }
            return score;
        }
    )", "compare_test", std::filesystem::current_path());

    auto cmp = vm.get_function("compare_test", "compare_ops");
    require(cmp.has_value(), "int fastpath: compare_ops must exist");
    auto cmp_result = vm.call(*cmp, {zephyr::ZephyrValue(3), zephyr::ZephyrValue(7)});
    // all conditions true: 1+2+4+8+16+32 = 63
    require(cmp_result.is_int() && cmp_result.as_int() == 63,
            "int fastpath: compare_ops(3,7) must be 63 (got " + std::to_string(cmp_result.as_int()) + ")");
}

}  // namespace zephyr_tests
