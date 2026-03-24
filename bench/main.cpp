#include "bench_runner.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

void print_usage() {
    std::cout << "zephyr_bench [--output <json>] [--baseline <json>] [--strict]\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::optional<std::filesystem::path> output_path;
        std::optional<std::filesystem::path> baseline_path;
        bool strict_gates = false;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--output") {
                if (i + 1 >= argc) {
                    print_usage();
                    return 1;
                }
                output_path = argv[++i];
            } else if (arg == "--baseline") {
                if (i + 1 >= argc) {
                    print_usage();
                    return 1;
                }
                baseline_path = argv[++i];
            } else if (arg == "--strict") {
                strict_gates = true;
            } else {
                print_usage();
                return 1;
            }
        }

        zephyr::bench::BenchmarkOptions options;
        options.workspace_root = std::filesystem::current_path();
        if (output_path.has_value()) {
            options.output_path = *output_path;
        }
        options.baseline_path = baseline_path;
        options.strict_gates = strict_gates;
        auto report = zephyr::bench::run_suite(options);
        const std::filesystem::path final_path = output_path.value_or(options.workspace_root / "bench" / "results" / "latest.json");
        zephyr::bench::write_report(report, final_path);
        std::cout << zephyr::bench::to_json(report) << std::endl;
        return zephyr::bench::gates_failed(report) && strict_gates ? 2 : 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
