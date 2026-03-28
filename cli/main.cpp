#include "zephyr/api.hpp"
#include "bench_runner.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

// Declared in cli/lsp_server.cpp
int run_lsp_server(const char* exe_path);

namespace {

double superinstruction_hit_rate_pct(const zephyr::ZephyrRuntimeStats& stats) {
    if (stats.vm.total_original_opcode_count == 0) {
        return 0.0;
    }
    return static_cast<double>(stats.vm.superinstruction_fusions) * 100.0 /
           static_cast<double>(stats.vm.total_original_opcode_count);
}

double ns_to_us(std::uint64_t duration_ns) {
    return static_cast<double>(duration_ns) / 1000.0;
}

std::string gc_pause_summary(const zephyr::GCPauseStats& stats) {
    std::ostringstream out;
    out << "GC pause p50: " << ns_to_us(stats.p50_ns)
        << "us, p95: " << ns_to_us(stats.p95_ns)
        << "us, p99: " << ns_to_us(stats.p99_ns) << "us";
    return out.str();
}

void configure_default_module_paths(zephyr::ZephyrVM& vm, const std::filesystem::path& executable_path) {
    vm.add_module_search_path(std::filesystem::current_path().string());
    if (!executable_path.empty()) {
        const auto exe_dir = std::filesystem::weakly_canonical(executable_path).parent_path();
        vm.add_module_search_path(exe_dir.string());
    }
}

void print_usage() {
    std::cout << "Zephyr CLI\n"
              << "  zephyr run <file>\n"
              << "  zephyr check <file>\n"
              << "  zephyr repl\n"
              << "  zephyr stats <file>\n"
              << "  zephyr dump-bytecode <file> [function]\n"
              << "  zephyr bench [output.json] [--baseline <json>] [--strict]\n"
              << "  zephyr lsp              Start LSP server (stdin/stdout)\n";
}

std::string runtime_stats_json(const zephyr::ZephyrRuntimeStats& stats, const zephyr::GCPauseStats& gc_pause_stats) {
    std::ostringstream out;
    out << "{\n"
        << "  \"gc\": {\n"
        << "    \"live_objects\": " << stats.gc.live_objects << ",\n"
        << "    \"live_bytes\": " << stats.gc.live_bytes << ",\n"
        << "    \"young_objects\": " << stats.gc.young_objects << ",\n"
        << "    \"old_objects\": " << stats.gc.old_objects << ",\n"
        << "    \"total_young_collections\": " << stats.gc.total_young_collections << ",\n"
        << "    \"total_full_collections\": " << stats.gc.total_full_collections << ",\n"
        << "    \"total_promotions\": " << stats.gc.total_promotions << ",\n"
        << "    \"barrier_hits\": " << stats.gc.barrier_hits << "\n"
        << "  },\n"
        << "  \"gc_pause\": {\n"
        << "    \"p50_ns\": " << gc_pause_stats.p50_ns << ",\n"
        << "    \"p95_ns\": " << gc_pause_stats.p95_ns << ",\n"
        << "    \"p99_ns\": " << gc_pause_stats.p99_ns << ",\n"
        << "    \"frame_budget_miss\": " << gc_pause_stats.frame_budget_miss_count << ",\n"
        << "    \"summary\": \"" << gc_pause_summary(gc_pause_stats) << "\"\n"
        << "  },\n"
        << "  \"vm\": {\n"
        << "    \"opcode_count\": " << stats.vm.opcode_count << ",\n"
        << "    \"ast_fallback_executions\": " << stats.vm.ast_fallback_executions << ",\n"
        << "    \"local_binding_cache_hits\": " << stats.vm.local_binding_cache_hits << ",\n"
        << "    \"local_binding_cache_misses\": " << stats.vm.local_binding_cache_misses << ",\n"
        << "    \"global_binding_cache_hits\": " << stats.vm.global_binding_cache_hits << ",\n"
        << "    \"global_binding_cache_misses\": " << stats.vm.global_binding_cache_misses << ",\n"
        << "    \"callback_invocations\": " << stats.vm.callback_invocations << ",\n"
        << "    \"serialized_value_exports\": " << stats.vm.serialized_value_exports << ",\n"
        << "    \"deserialized_value_imports\": " << stats.vm.deserialized_value_imports << ",\n"
        << "    \"lightweight_calls\": " << stats.vm.lightweight_calls << ",\n"
        << "    \"superinstruction_fusions\": " << stats.vm.superinstruction_fusions << ",\n"
        << "    \"total_original_opcode_count\": " << stats.vm.total_original_opcode_count << ",\n"
        << "    \"superinstruction_hit_rate_pct\": " << superinstruction_hit_rate_pct(stats) << ",\n"
        << "    \"lightweight_call_ratio\": "
        << (stats.vm.callback_invocations > 0
                ? static_cast<double>(stats.vm.lightweight_calls) / static_cast<double>(stats.vm.callback_invocations)
                : 0.0)
        << "\n"
        << "  },\n"
        << "  \"coroutine\": {\n"
        << "    \"suspended_coroutines\": " << stats.coroutine.suspended_coroutines << ",\n"
        << "    \"completed_coroutines\": " << stats.coroutine.completed_coroutines << ",\n"
        << "    \"total_coroutine_resume_calls\": " << stats.coroutine.total_coroutine_resume_calls << ",\n"
        << "    \"total_coroutine_yields\": " << stats.coroutine.total_coroutine_yields << "\n"
        << "  },\n"
        << "  \"handle\": {\n"
        << "    \"resolve_count\": " << stats.handle.resolve_count << ",\n"
        << "    \"resolve_failures\": " << stats.handle.resolve_failures << ",\n"
        << "    \"stable_handles\": " << stats.handle.stable_handles << "\n"
        << "  },\n"
        << "  \"epochs\": {\n"
        << "    \"frame\": " << stats.frame_epoch << ",\n"
        << "    \"tick\": " << stats.tick_epoch << ",\n"
        << "    \"scene\": " << stats.scene_epoch << "\n"
        << "  }\n"
        << "}";
    return out.str();
}

int run_file(const std::filesystem::path& path, const std::filesystem::path& executable_path) {
    zephyr::ZephyrVM vm;
    configure_default_module_paths(vm, executable_path);
    vm.execute_file(path);
    const auto handle = vm.get_function(std::filesystem::weakly_canonical(path).string(), "main");
    if (handle.has_value()) {
        const zephyr::ZephyrValue result = vm.call(*handle);
        if (!result.is_nil()) {
            std::cout << zephyr::to_string(result) << std::endl;
        }
    }
    return 0;
}

int check_file(const std::filesystem::path& path, const std::filesystem::path& executable_path) {
    zephyr::ZephyrVM vm;
    configure_default_module_paths(vm, executable_path);
    vm.check_file(path);
    std::cout << "check ok: " << std::filesystem::weakly_canonical(path).string() << std::endl;
    return 0;
}

int dump_bytecode(const std::filesystem::path& path, const std::string& function_name, const std::filesystem::path& executable_path) {
    zephyr::ZephyrVM vm;
    configure_default_module_paths(vm, executable_path);
    vm.execute_file(path);
    const auto module_name = std::filesystem::weakly_canonical(path).string();
    std::cout << vm.dump_bytecode(module_name, function_name) << std::endl;
    return 0;
}

int stats_file(const std::filesystem::path& path, const std::filesystem::path& executable_path) {
    zephyr::ZephyrVM vm;
    configure_default_module_paths(vm, executable_path);
    vm.execute_file(path);
    const auto module_name = std::filesystem::weakly_canonical(path).string();
    const auto handle = vm.get_function(module_name, "main");
    if (handle.has_value()) {
        vm.call(*handle);
    }
    std::cout << runtime_stats_json(vm.runtime_stats(), vm.get_gc_pause_stats()) << std::endl;
    return 0;
}

int bench_suite(const std::optional<std::filesystem::path>& output_path,
                const std::optional<std::filesystem::path>& baseline_path,
                bool strict_gates) {
    zephyr::bench::BenchmarkOptions options;
    options.workspace_root = std::filesystem::current_path();
    if (output_path.has_value()) {
        options.output_path = *output_path;
    }
    options.baseline_path = baseline_path;
    options.strict_gates = strict_gates;
    auto report = zephyr::bench::run_suite(options);
    const auto final_path = output_path.value_or(std::filesystem::current_path() / "bench" / "results" / "latest.json");
    zephyr::bench::write_report(report, final_path);
    std::cout << "benchmark report: " << final_path.string() << "\n";
    std::cout << zephyr::bench::to_json(report) << std::endl;
    return zephyr::bench::gates_failed(report) && strict_gates ? 2 : 0;
}

int repl(const std::filesystem::path& executable_path) {
    zephyr::ZephyrVM vm;
    configure_default_module_paths(vm, executable_path);
    std::string buffer;
    std::cout << "Zephyr REPL. Enter :quit to exit.\n";
    while (true) {
        std::cout << "z> ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line == ":quit") {
            break;
        }
        buffer += line;
        buffer += '\n';
        try {
            vm.execute_string(buffer, "<repl>", std::filesystem::current_path());
        } catch (const std::exception& error) {
            std::cerr << error.what() << std::endl;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }

        const std::string command = argv[1];
        if (command == "run") {
            if (argc < 3) {
                print_usage();
                return 1;
            }
            return run_file(argv[2], argv[0]);
        }
        if (command == "check") {
            if (argc < 3) {
                print_usage();
                return 1;
            }
            return check_file(argv[2], argv[0]);
        }
        if (command == "repl") {
            return repl(argv[0]);
        }
        if (command == "stats") {
            if (argc < 3) {
                print_usage();
                return 1;
            }
            return stats_file(argv[2], argv[0]);
        }
        if (command == "dump-bytecode") {
            if (argc < 3) {
                print_usage();
                return 1;
            }
            return dump_bytecode(argv[2], argc >= 4 ? argv[3] : "", argv[0]);
        }
        if (command == "lsp") {
            return run_lsp_server(argv[0]);
        }
        if (command == "bench") {
            std::optional<std::filesystem::path> output_path;
            std::optional<std::filesystem::path> baseline_path;
            bool strict_gates = false;
            for (int i = 2; i < argc; ++i) {
                const std::string arg = argv[i];
                if (arg == "--baseline") {
                    if (i + 1 >= argc) {
                        print_usage();
                        return 1;
                    }
                    baseline_path = argv[++i];
                } else if (arg == "--strict") {
                    strict_gates = true;
                } else if (!output_path.has_value()) {
                    output_path = std::filesystem::path(arg);
                } else {
                    print_usage();
                    return 1;
                }
            }
            return bench_suite(output_path, baseline_path, strict_gates);
        }

        print_usage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
