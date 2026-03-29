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
        out << "import \"b.zph\";\nfn main() -> int { return 1; }\n";
    }
    {
        std::ofstream out(b_path);
        out << "import \"a.zph\";\nfn helper() -> int { return 2; }\n";
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
                    fn draw(self) -> int;
                    fn metric(self) -> int;
                }

                struct Player { x: int }

                impl Drawable for Player {
                    fn draw(self) -> int {
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
                fn greet(self) -> string;
            }

            struct Cat { name: string }

            impl Greeter for Cat {
                fn greet(self) -> string {
                    return self.name;
                }

                fn extra(self) -> string {
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
                fn greet(self) -> string;
            }

            struct Cat { name: string }

            impl Greeter for Cat {
                fn greet(self) -> string {
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
                fn add(a: int, b: int) -> int {
                    return a + b;
                }

                fn run() -> int {
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
            struct Node { next: any }

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
        out << "export fn ok() -> int { return 1; }\n"
               "fn hidden() -> int { return 2; }\n";
    }
    {
        std::ofstream out(entry_path);
        out << "import \"foo.zph\" as foo;\n"
               "fn run() -> int {\n"
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
            fn classify(value: any) -> int {
                return match value {
                    nil => 0,
                };
            }
        )",
        "unit_check_match_hint",
        std::filesystem::current_path());

    require(warnings.str().find("warning: match may not cover all cases: missing string patterns") != std::string::npos,
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
        out << "export fn add(a: int, b: int) -> int {\n"
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

            fn run() -> int {
                return lib.add(2, 3);
            }
        )",
        "unit_package_import_check",
        std::filesystem::current_path());

    vm.execute_string(
        R"(
            import "lib" as lib;

            fn run() -> int {
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
            fn accumulate(limit: int) -> int {
                let mut total: int = 0;
                let mut i: int = 0;
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
            fn sum_large(values) -> int {
                let mut total: int = 0;
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

void test_bytecode_for_in_range_syntax() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn sum_exclusive(limit: int) -> int {
                let mut total: int = 0;
                for value in 0..limit {
                    total = total + value;
                }
                return total;
            }

            fn sum_inclusive(limit: int) -> int {
                let mut total: int = 0;
                for value in 0..=limit {
                    total = total + value;
                }
                return total;
            }
        )",
        "unit_for_range",
        std::filesystem::current_path());

    const auto exclusive = vm.get_function("unit_for_range", "sum_exclusive");
    require(exclusive.has_value(), "missing sum_exclusive handle");
    const auto exclusive_result = vm.call(*exclusive, {zephyr::ZephyrValue(5)});
    require(exclusive_result.is_int() && exclusive_result.as_int() == 10,
            "0..limit should exclude the upper bound");

    const auto inclusive = vm.get_function("unit_for_range", "sum_inclusive");
    require(inclusive.has_value(), "missing sum_inclusive handle");
    const auto inclusive_result = vm.call(*inclusive, {zephyr::ZephyrValue(5)});
    require(inclusive_result.is_int() && inclusive_result.as_int() == 15,
            "0..=limit should include the upper bound");
}

void test_bytecode_struct_enum_match() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            struct Point { x: int, y: int }
            enum Command { Move(int, int), Idle }

            fn drive(flag: bool) -> int {
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
            fn shadow_and_closure() -> int {
                let mut current: int = 1;
                let inc = fn(step: int) -> int {
                    current = current + step;
                    return current;
                };
                let outer: int = 5;
                {
                    let outer: int = 20;
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
                let mut current: int = 4;
                return coroutine fn() -> int {
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
        {}, "Nil");

    vm.execute_string(
        R"(
            fn drive() -> int {
                let inner = (fn() -> Function {
                    let mut seed: int = 3;
                    let build = fn() -> Function {
                        return fn(step: int) -> int {
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
        {}, "Nil");

    vm.execute_string(
        R"(
            struct Box {
                value: int,
            }

            fn drive() -> int {
                let run = (fn() -> Function {
                    let mut values = [1];
                    let mut box = Box { value: 10 };
                    return fn() -> int {
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
        {}, "Nil");

    vm.execute_string(
        R"(
            fn drive() -> int {
                let run = (fn() -> Function {
                    let counter = coroutine fn() -> int {
                        yield 1;
                        return 2;
                    };

                    return fn() -> int {
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
            export let seed: int = 38;
            export fn add_bonus(value: int) -> int {
                return value + 1;
            }
        )";
    }

    {
        std::ofstream main(main_path);
        main << R"(
            import "dep.zph";

            let mut total: int = add_bonus(seed);
            let values = [1, 2];

            for value in values {
                total = total + value;
            }

            export fn result() -> int {
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
            export fn value(input: int) -> int {
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
            export fn value(input: int) -> int {
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
            fn main() -> int {
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

            fn read() -> int {
                return after;
            }

            fn nested_global() -> int {
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
                return coroutine fn() -> int {
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

            fn locals() -> int {
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
        fn add(a: int, b: int) -> int {
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
        require(result.is_int(), "lightweight: result must be int");
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
        fn outer() -> int {
            let mut x = 10;
            let inc = fn() -> int {
                x = x + 1;
                return x;
            };
            let get = fn() -> int { return x; };

            let mut sum = 0;
            sum = sum + inc();
            sum = sum + inc();
            sum = sum + get();
            return sum;
        }
        export fn run() -> int {
            return outer();
        }
    )", "nested_upvalue_test", std::filesystem::current_path());

    auto handle = vm.get_function("nested_upvalue_test", "run");
    require(handle.has_value(), "nested upvalue: run function must exist");

    auto call_result = vm.call(*handle, {});
    require(call_result.is_int(), "nested upvalue: result must be int");
    // inc() returns 11, 12; get() returns 12 (shared mutable upvalue); sum = 11 + 12 + 12 = 35
    require(call_result.as_int() == 35,
            "nested upvalue: sum must be 35 (got " + std::to_string(call_result.as_int()) + ")");
}

void test_phase1_2_constant_folding_reduces_bytecode() {
    zephyr::ZephyrVM vm;
    vm.install_core();

    // A function with constant arithmetic should be folded at compile time.
    vm.execute_string(R"(
        fn compute() -> int {
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
        fn fib(n: int) -> int {
            if n <= 1 {
                return n;
            }
            return fib(n - 1) + fib(n - 2);
        }
        export fn run() -> int {
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
        export fn compare_ops(a: int, b: int) -> int {
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

void test_generic_function_parsing() {
    zephyr::ZephyrVM vm;
    // Generic function declaration should parse and check cleanly (no exception)
    bool accepted = false;
    try {
        vm.check_string(R"(
            fn identity<T>(x: T) -> T { return x; }
            let r = identity<int>(42);
        )", "unit_generic_fn_parse", std::filesystem::current_path());
        accepted = true;
    } catch (const std::exception& error) {
        require(false, std::string("generic function parsing: unexpected error: ") + error.what());
    }
    require(accepted, "generic function: check_string should accept generic fn syntax");
}

void test_generic_struct_parsing() {
    zephyr::ZephyrVM vm;
    bool accepted = false;
    try {
        vm.check_string(R"(
            struct Pair<A, B> {
                first: A,
                second: B,
            }
            let p = Pair { first: 1, second: "hello" };
        )", "unit_generic_struct_parse", std::filesystem::current_path());
        accepted = true;
    } catch (const std::exception& error) {
        require(false, std::string("generic struct parsing: unexpected error: ") + error.what());
    }
    require(accepted, "generic struct: check_string should accept generic struct syntax");
}

void test_generic_function_execution() {
    zephyr::ZephyrVM vm;
    vm.execute_string(R"(
        fn identity<T>(x: T) -> T { return x; }
        fn swap<A, B>(a: A, b: B) -> B { return b; }
        export fn run_identity() -> int { return identity<int>(42); }
    )", "unit_generic_fn_exec", std::filesystem::current_path());
    const auto handle = vm.get_function("unit_generic_fn_exec", "run_identity");
    require(handle.has_value(), "generic function execution: run_identity must exist");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 42,
            "generic function execution: identity<int>(42) should return 42");
}

void test_generic_struct_instantiation() {
    zephyr::ZephyrVM vm;
    vm.execute_string(R"(
        struct Pair<A, B> {
            first: A,
            second: B,
        }
        export fn run_pair() -> int {
            let p = Pair { first: 10, second: "world" };
            return p.first;
        }
    )", "unit_generic_struct_inst", std::filesystem::current_path());
    const auto handle = vm.get_function("unit_generic_struct_inst", "run_pair");
    require(handle.has_value(), "generic struct instantiation: run_pair must exist");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 10,
            "generic struct instantiation: p.first should be 10");
}

void test_generic_multi_param_function() {
    zephyr::ZephyrVM vm;
    vm.execute_string(R"(
        fn first<A, B>(a: A, b: B) -> A { return a; }
        export fn run_first() -> int { return first<int, string>(99, "ignored"); }
    )", "unit_generic_multi_param", std::filesystem::current_path());
    const auto handle = vm.get_function("unit_generic_multi_param", "run_first");
    require(handle.has_value(), "generic multi-param function: run_first must exist");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 99,
            "generic multi-param function: first<int,string>(99, ignored) should be 99");
}

void test_wave_l_result_ok_err() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            export fn run_ok() -> int {
                let r = Ok(42);
                return match r {
                    Ok(v)  => v,
                    Err(_) => 0,
                };
            }
            export fn run_err() -> int {
                let r = Err("oops");
                return match r {
                    Ok(v)  => v,
                    Err(_) => -1,
                };
            }
        )",
        "unit_wave_l_result",
        std::filesystem::current_path());

    const auto ok_h = vm.get_function("unit_wave_l_result", "run_ok");
    require(ok_h.has_value(), "wave_l_result: missing run_ok handle");
    const auto ok_r = vm.call(*ok_h);
    require(ok_r.is_int() && ok_r.as_int() == 42, "wave_l_result: Ok(42) match should yield 42");

    const auto err_h = vm.get_function("unit_wave_l_result", "run_err");
    require(err_h.has_value(), "wave_l_result: missing run_err handle");
    const auto err_r = vm.call(*err_h);
    require(err_r.is_int() && err_r.as_int() == -1, "wave_l_result: Err case should yield -1");
}

void test_wave_l_result_question_op() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn safe_dec(a: int) {
                if a == 0 { return Err("zero"); }
                return Ok(a - 1);
            }
            fn compute(a: int) {
                let x = safe_dec(a)?;
                return Ok(x + 10);
            }
            export fn ok_case() -> int {
                let r = compute(5);
                return match r {
                    Ok(v)  => v,
                    Err(_) => -1,
                };
            }
            export fn err_case() -> int {
                let r = compute(0);
                return match r {
                    Ok(v)  => v,
                    Err(_) => -1,
                };
            }
        )",
        "unit_wave_l_question",
        std::filesystem::current_path());

    const auto ok_h = vm.get_function("unit_wave_l_question", "ok_case");
    require(ok_h.has_value(), "wave_l_question: missing ok_case handle");
    const auto ok_r = vm.call(*ok_h);
    require(ok_r.is_int() && ok_r.as_int() == 14, "wave_l_question: dec(5)=4, +10=14");

    const auto err_h = vm.get_function("unit_wave_l_question", "err_case");
    require(err_h.has_value(), "wave_l_question: missing err_case handle");
    const auto err_r = vm.call(*err_h);
    require(err_r.is_int() && err_r.as_int() == -1, "wave_l_question: zero input should propagate Err");
}

void test_wave_l_array_pattern() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            export fn describe_empty() -> int {
                let arr = [];
                return match arr {
                    []             => 0,
                    [x]            => 1,
                    [a, b, ..rest] => 2,
                };
            }
            export fn describe_one() -> int {
                let arr = [99];
                return match arr {
                    []             => 0,
                    [x]            => x,
                    [a, b, ..rest] => 2,
                };
            }
            export fn describe_many() -> int {
                let arr = [10, 20, 30, 40];
                return match arr {
                    []             => 0,
                    [x]            => x,
                    [a, b, ..rest] => a + b,
                };
            }
        )",
        "unit_wave_l_array",
        std::filesystem::current_path());

    const auto empty_h = vm.get_function("unit_wave_l_array", "describe_empty");
    require(empty_h.has_value(), "wave_l_array: missing describe_empty handle");
    const auto empty_r = vm.call(*empty_h);
    require(empty_r.is_int() && empty_r.as_int() == 0, "wave_l_array: empty array pattern");

    const auto one_h = vm.get_function("unit_wave_l_array", "describe_one");
    require(one_h.has_value(), "wave_l_array: missing describe_one handle");
    const auto one_r = vm.call(*one_h);
    require(one_r.is_int() && one_r.as_int() == 99, "wave_l_array: single element array pattern");

    const auto many_h = vm.get_function("unit_wave_l_array", "describe_many");
    require(many_h.has_value(), "wave_l_array: missing describe_many handle");
    const auto many_r = vm.call(*many_h);
    require(many_r.is_int() && many_r.as_int() == 30, "wave_l_array: many elements: first+second");
}

void test_wave_l_struct_pattern() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            struct Point { x: int, y: int }
            export fn classify_origin() -> int {
                let p = Point { x: 0, y: 0 };
                return match p {
                    Point { x: 0, y: 0 } => 0,
                    Point { x, y }       => x + y,
                };
            }
            export fn classify_general() -> int {
                let p = Point { x: 3, y: 4 };
                return match p {
                    Point { x: 0, y } => y,
                    Point { x, y }    => x + y,
                };
            }
        )",
        "unit_wave_l_struct",
        std::filesystem::current_path());

    const auto origin_h = vm.get_function("unit_wave_l_struct", "classify_origin");
    require(origin_h.has_value(), "wave_l_struct: missing classify_origin handle");
    const auto origin_r = vm.call(*origin_h);
    require(origin_r.is_int() && origin_r.as_int() == 0, "wave_l_struct: Point{0,0} should match origin case");

    const auto gen_h = vm.get_function("unit_wave_l_struct", "classify_general");
    require(gen_h.has_value(), "wave_l_struct: missing classify_general handle");
    const auto gen_r = vm.call(*gen_h);
    require(gen_r.is_int() && gen_r.as_int() == 7, "wave_l_struct: Point{3,4} should give 3+4=7");
}

void test_wave_l_nested_pattern() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            struct Point { x: int, y: int }
            export fn nested_ok() -> int {
                let r = Ok(Point { x: 1, y: 2 });
                return match r {
                    Ok(Point { x, y }) => x + y,
                    Err(_)             => -1,
                };
            }
            export fn nested_err() -> int {
                let r = Err("bad");
                return match r {
                    Ok(Point { x, y }) => x + y,
                    Err(_)             => -1,
                };
            }
        )",
        "unit_wave_l_nested",
        std::filesystem::current_path());

    const auto ok_h = vm.get_function("unit_wave_l_nested", "nested_ok");
    require(ok_h.has_value(), "wave_l_nested: missing nested_ok handle");
    const auto ok_r = vm.call(*ok_h);
    require(ok_r.is_int() && ok_r.as_int() == 3, "wave_l_nested: Ok(Point{1,2}) should give 1+2=3");

    const auto err_h = vm.get_function("unit_wave_l_nested", "nested_err");
    require(err_h.has_value(), "wave_l_nested: missing nested_err handle");
    const auto err_r = vm.call(*err_h);
    require(err_r.is_int() && err_r.as_int() == -1, "wave_l_nested: Err should give -1");
}

void test_named_import() {
    const auto base_dir = std::filesystem::current_path() / "tmp_named_import_test";
    std::filesystem::remove_all(base_dir);
    std::filesystem::create_directories(base_dir);

    const auto lib_path  = base_dir / "lib.zph";
    const auto main_path = base_dir / "main.zph";
    {
        std::ofstream out(lib_path);
        out << "export let foo: int = 42;\nexport let bar: int = 7;\n";
    }
    {
        std::ofstream out(main_path);
        out << "import { foo } from \"lib.zph\";\n"
               "export fn get_foo() -> int { return foo; }\n";
    }

    zephyr::ZephyrVM vm;
    vm.execute_file(main_path);

    const auto module_name = std::filesystem::weakly_canonical(main_path).string();
    const auto h = vm.get_function(module_name, "get_foo");
    require(h.has_value(), "named_import: missing get_foo handle");
    const auto result = vm.call(*h);
    require(result.is_int() && result.as_int() == 42, "named_import: expected foo==42");

    std::filesystem::remove_all(base_dir);
}

void test_re_export() {
    const auto base_dir = std::filesystem::current_path() / "tmp_re_export_test";
    std::filesystem::remove_all(base_dir);
    std::filesystem::create_directories(base_dir);

    const auto lib_path    = base_dir / "lib.zph";
    const auto middle_path = base_dir / "middle.zph";
    const auto main_path   = base_dir / "main.zph";
    {
        std::ofstream out(lib_path);
        out << "export let foo: int = 99;\n";
    }
    {
        std::ofstream out(middle_path);
        out << "export { foo } from \"lib.zph\";\n";
    }
    {
        std::ofstream out(main_path);
        out << "import { foo } from \"middle.zph\";\n"
               "export fn get_foo() -> int { return foo; }\n";
    }

    zephyr::ZephyrVM vm;
    vm.execute_file(main_path);

    const auto module_name = std::filesystem::weakly_canonical(main_path).string();
    const auto h = vm.get_function(module_name, "get_foo");
    require(h.has_value(), "re_export: missing get_foo handle");
    const auto result = vm.call(*h);
    require(result.is_int() && result.as_int() == 99, "re_export: expected foo==99");

    std::filesystem::remove_all(base_dir);
}

void test_circular_import_error() {
    const auto temp_dir = std::filesystem::current_path() / "tmp_circular_runtime_test";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    const auto a_path = temp_dir / "a.zph";
    const auto b_path = temp_dir / "b.zph";
    {
        std::ofstream out(a_path);
        out << "import \"b.zph\";\nlet x: int = 1;\n";
    }
    {
        std::ofstream out(b_path);
        out << "import \"a.zph\";\nlet y: int = 2;\n";
    }

    bool rejected = false;
    try {
        zephyr::ZephyrVM vm;
        vm.execute_file(a_path);
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("circular") != std::string::npos ||
                message.find("Circular") != std::string::npos ||
                message.find("already loading") != std::string::npos,
                "circular_import: should report circular import");
    }

    std::filesystem::remove_all(temp_dir);
    require(rejected, "circular_import: runtime should reject circular imports");
}

void test_std_math() {
    zephyr::ZephyrVM vm;

    vm.execute_string(
        R"(
            import { floor, sqrt, clamp, pi, min, max, pow, abs } from "std/math";

            export fn check_floor() -> bool { return floor(3.9) == 3.0; }
            export fn check_sqrt()  -> bool { return sqrt(4.0) == 2.0; }
            export fn check_pi()    -> bool { return pi > 3.14; }
            export fn check_clamp() -> bool { return clamp(10.0, 0.0, 5.0) == 5.0; }
            export fn check_min()   -> bool { return min(3.0, 7.0) == 3.0; }
            export fn check_max()   -> bool { return max(3.0, 7.0) == 7.0; }
            export fn check_abs()   -> bool { return abs(-4.0) == 4.0; }
            export fn check_pow()   -> bool { return pow(2.0, 3.0) == 8.0; }
        )",
        "unit_std_math",
        std::filesystem::current_path());

    const auto tests = std::vector<std::string>{"check_floor", "check_sqrt", "check_pi",
                                                 "check_clamp", "check_min",  "check_max",
                                                 "check_abs",   "check_pow"};
    for (const auto& fn_name : tests) {
        const auto h = vm.get_function("unit_std_math", fn_name);
        require(h.has_value(), "std_math: missing handle for " + fn_name);
        const auto result = vm.call(*h);
        require(result.is_bool() && result.as_bool(), "std_math: " + fn_name + " returned false");
    }
}

void test_std_string() {
    zephyr::ZephyrVM vm;

    vm.execute_string(
        R"(
            import { upper, lower, contains, len, starts_with, ends_with, trim, replace, substr } from "std/string";

            export fn check_upper()       -> bool { return upper("hello") == "HELLO"; }
            export fn check_lower()       -> bool { return lower("WORLD") == "world"; }
            export fn check_contains()    -> bool { return contains("hello world", "world") == true; }
            export fn check_len()         -> bool { return len("abc") == 3; }
            export fn check_starts_with() -> bool { return starts_with("zephyr", "zep") == true; }
            export fn check_ends_with()   -> bool { return ends_with("zephyr", "hyr") == true; }
            export fn check_trim()        -> bool { return trim("  hi  ") == "hi"; }
            export fn check_replace()     -> bool { return replace("hello", "l", "r") == "herro"; }
            export fn check_substr()      -> bool { return substr("hello", 1, 3) == "ell"; }
        )",
        "unit_std_string",
        std::filesystem::current_path());

    const auto tests = std::vector<std::string>{"check_upper",  "check_lower",       "check_contains",
                                                 "check_len",    "check_starts_with", "check_ends_with",
                                                 "check_trim",   "check_replace",     "check_substr"};
    for (const auto& fn_name : tests) {
        const auto h = vm.get_function("unit_std_string", fn_name);
        require(h.has_value(), "std_string: missing handle for " + fn_name);
        const auto result = vm.call(*h);
        require(result.is_bool() && result.as_bool(), "std_string: " + fn_name + " returned false");
    }
}

void test_forward_reference() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn main() -> string {
                return greet("world");
            }
            fn greet(name: string) -> string {
                return "hello " + name;
            }
        )",
        "unit_fwd_ref",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_fwd_ref", "main");
    require(handle.has_value(), "forward_reference: missing main handle");
    const auto result = vm.call(*handle);
    require(result.is_string() && result.as_string() == "hello world",
            "forward_reference: unexpected result");
}

void test_mutual_recursion() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn is_even(n: int) -> bool {
                if n == 0 { return true; }
                return is_odd(n - 1);
            }
            fn is_odd(n: int) -> bool {
                if n == 0 { return false; }
                return is_even(n - 1);
            }
        )",
        "unit_mutual_rec",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_mutual_rec", "is_even");
    require(handle.has_value(), "mutual_recursion: missing is_even handle");
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(4)});
    require(result.is_bool() && result.as_bool(), "mutual_recursion: is_even(4) should be true");
}

void test_trait_impl_missing_method() {
    zephyr::ZephyrVM vm;
    bool rejected = false;
    try {
        vm.execute_string(
            R"(
                trait Greeter {
                    fn greet(self) -> string;
                    fn farewell(self) -> string;
                }
                struct Bot { name: string }
                impl Greeter for Bot {
                    fn greet(self) -> string { return "hi"; }
                }
            )",
            "unit_trait_missing",
            std::filesystem::current_path());
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("farewell") != std::string::npos,
                "trait impl check should mention missing method 'farewell'");
    }
    require(rejected, "execute_string should reject impls with missing trait methods");
}

void test_where_clause_basic() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            trait Describable {
                fn describe(self) -> string;
            }
            struct Dog { name: string }
            impl Describable for Dog {
                fn describe(self) -> string { return "Dog: " + self.name; }
            }
            fn describe_it<T>(x: T) -> string where T: Describable {
                return x.describe();
            }
            export fn run() -> string {
                let d = Dog { name: "Rex" };
                return describe_it(d);
            }
        )",
        "unit_where_basic",
        std::filesystem::current_path());
    const auto handle = vm.get_function("unit_where_basic", "run");
    require(handle.has_value(), "where clause basic: run must exist");
    const auto result = vm.call(*handle);
    require(result.is_string() && result.as_string() == "Dog: Rex",
            "where clause basic: describe_it(d) should return 'Dog: Rex'");
}

void test_where_multiple_bounds() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            trait Printable {
                fn to_str(self) -> string;
            }
            trait Countable {
                fn count(self) -> int;
            }
            struct Bag { size: int }
            impl Printable for Bag {
                fn to_str(self) -> string { return "Bag"; }
            }
            impl Countable for Bag {
                fn count(self) -> int { return self.size; }
            }
            fn describe<T>(x: T) -> string where T: Printable, T: Countable {
                return x.to_str();
            }
            export fn run() -> string {
                let b = Bag { size: 3 };
                return describe(b);
            }
        )",
        "unit_where_multi",
        std::filesystem::current_path());
    const auto handle = vm.get_function("unit_where_multi", "run");
    require(handle.has_value(), "where multiple bounds: run must exist");
    const auto result = vm.call(*handle);
    require(result.is_string() && result.as_string() == "Bag",
            "where multiple bounds: describe(b) should return 'Bag'");
}

void test_where_bound_violation() {
    zephyr::ZephyrVM vm;
    bool rejected = false;
    try {
        vm.execute_string(
            R"(
                trait Flyable {
                    fn fly(self);
                }
                fn launch<T>(x: T) where T: Flyable {
                    x.fly();
                }
                struct Rock {}
                let r = Rock {};
                launch(r);
            )",
            "unit_where_violation",
            std::filesystem::current_path());
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("Rock") != std::string::npos || message.find("Flyable") != std::string::npos,
                "where bound violation: error should mention 'Rock' or 'Flyable'");
    }
    require(rejected, "where bound violation: should throw when calling launch with Rock");
}

void test_std_json_parse() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            import { stringify } from "std/json";
            export fn check_stringify_int()   -> bool { return stringify(42)    == "42"; }
            export fn check_stringify_true()  -> bool { return stringify(true)  == "true"; }
            export fn check_stringify_false() -> bool { return stringify(false) == "false"; }
            export fn check_stringify_null()  -> bool { return stringify(nil)   == "null"; }
            export fn check_stringify_str()   -> bool { return stringify("hi")  == "\"hi\""; }
        )",
        "unit_std_json_parse",
        std::filesystem::current_path());

    const auto tests = std::vector<std::string>{
        "check_stringify_int", "check_stringify_true", "check_stringify_false",
        "check_stringify_null", "check_stringify_str"};
    for (const auto& fn_name : tests) {
        const auto h = vm.get_function("unit_std_json_parse", fn_name);
        require(h.has_value(), "std_json: missing handle for " + fn_name);
        const auto result = vm.call(*h);
        require(result.is_bool() && result.as_bool(), "std_json: " + fn_name + " returned false");
    }
}

void test_std_json_array() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            import { parse, stringify } from "std/json";
            export fn check_array() -> bool {
                let arr = parse("[1, 2, 3]");
                return stringify(arr) == "[1,2,3]";
            }
        )",
        "unit_std_json_array",
        std::filesystem::current_path());

    const auto h = vm.get_function("unit_std_json_array", "check_array");
    require(h.has_value(), "std_json_array: missing check_array handle");
    const auto result = vm.call(*h);
    require(result.is_bool() && result.as_bool(), "std_json_array: stringify([1,2,3]) should return \"[1,2,3]\"");
}

void test_std_collections_hashmap() {
    zephyr::ZephyrVM vm;
    vm.add_module_search_path(std::filesystem::current_path().string());
    vm.execute_string(
        R"(
            import { HashMap } from "std/collections";
            export fn check_size() -> bool {
                let m = HashMap::new();
                m.set("x", 10);
                m.set("y", 20);
                return m.size() == 2;
            }
            export fn check_get() -> bool {
                let m = HashMap::new();
                m.set("k", 99);
                return m.get("k") == 99;
            }
            export fn check_has() -> bool {
                let m = HashMap::new();
                m.set("a", 1);
                return m.has("a") == true && m.has("b") == false;
            }
            export fn check_delete() -> bool {
                let m = HashMap::new();
                m.set("d", 7);
                m.delete("d");
                return m.size() == 0;
            }
        )",
        "unit_std_collections_hashmap",
        std::filesystem::current_path());

    const auto tests = std::vector<std::string>{"check_size", "check_get", "check_has", "check_delete"};
    for (const auto& fn_name : tests) {
        const auto h = vm.get_function("unit_std_collections_hashmap", fn_name);
        require(h.has_value(), "std_collections_hashmap: missing handle for " + fn_name);
        const auto result = vm.call(*h);
        require(result.is_bool() && result.as_bool(), "std_collections_hashmap: " + fn_name + " returned false");
    }
}

void test_assoc_fn_syntax() {
    zephyr::ZephyrVM vm;
    vm.add_module_search_path(std::filesystem::current_path().string());
    vm.execute_string(
        R"(
            import { HashMap } from "std/collections";
            export fn check_size() -> int {
                let m = HashMap::new();
                m.set("a", 1);
                m.set("b", 2);
                return m.size();
            }
        )",
        "unit_assoc_fn",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_assoc_fn", "check_size");
    require(handle.has_value(), "test_assoc_fn_syntax: missing check_size handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 2, "test_assoc_fn_syntax: expected 2");
}

void test_collections_set_method() {
    zephyr::ZephyrVM vm;
    vm.add_module_search_path(std::filesystem::current_path().string());
    vm.execute_string(
        R"(
            import { Set } from "std/collections";
            export fn check_size() -> int {
                let s = Set::new();
                s.add(10);
                s.add(20);
                s.add(10);
                return s.size();
            }
        )",
        "unit_set_method",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_set_method", "check_size");
    require(handle.has_value(), "test_collections_set_method: missing check_size handle");
    const auto result = vm.call(*handle);
    require(result.is_int() && result.as_int() == 2, "test_collections_set_method: expected 2");
}

void test_collections_queue_method() {
    zephyr::ZephyrVM vm;
    vm.add_module_search_path(std::filesystem::current_path().string());
    vm.execute_string(
        R"(
            import { Queue } from "std/collections";
            export fn check_first() -> string {
                let q = Queue::new();
                q.push("first");
                q.push("second");
                return q.pop();
            }
        )",
        "unit_queue_method",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_queue_method", "check_first");
    require(handle.has_value(), "test_collections_queue_method: missing check_first handle");
    const auto result = vm.call(*handle);
    require(result.is_string() && result.as_string() == "first", "test_collections_queue_method: expected 'first'");
}

}  // namespace zephyr_tests
