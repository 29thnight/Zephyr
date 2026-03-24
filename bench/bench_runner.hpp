#pragma once

#include "zephyr/api.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zephyr::bench {

struct BenchmarkOptions {
    std::filesystem::path workspace_root;
    std::filesystem::path output_path;
    std::optional<std::filesystem::path> baseline_path;
    bool strict_gates = false;
    bool auto_discover_baseline = true;
    ZephyrVMConfig config{};
};

struct BenchmarkCaseSummary {
    std::string name;
    std::string description;
    std::size_t warmup_iterations = 0;
    std::size_t measure_iterations = 0;
    std::uint64_t min_ns = 0;
    std::uint64_t mean_ns = 0;
    std::uint64_t max_ns = 0;
    std::uint64_t p95_ns = 0;
    double mean_full_gc_cycles = 0.0;
    double mean_young_gc_cycles = 0.0;
    double mean_promotions = 0.0;
    double mean_host_resolves = 0.0;
    double mean_coroutine_resumes = 0.0;
    double mean_ast_fallbacks = 0.0;
    double mean_opcode_count = 0.0;
    double mean_superinstruction_fusions = 0.0;
    double mean_total_original_opcode_count = 0.0;
    double mean_superinstruction_hit_rate_pct = 0.0;
    double mean_local_binding_cache_hits = 0.0;
    double mean_local_binding_cache_misses = 0.0;
    double mean_global_binding_cache_hits = 0.0;
    double mean_global_binding_cache_misses = 0.0;
    std::string last_result;
    ZephyrRuntimeStats last_stats{};
};

struct BenchmarkGateResult {
    std::string name;
    std::string status;
    std::string detail;
    double current_value = 0.0;
    double baseline_value = 0.0;
    double target_value = 0.0;
    std::string unit;
};

struct BenchmarkReport {
    std::string generated_at_utc;
    std::string workspace_root;
    std::string output_path;
    std::string baseline_path;
    bool baseline_loaded = false;
    bool strict_gates = false;
    bool all_required_gates_passed = true;
    std::size_t passed_gates = 0;
    std::size_t failed_gates = 0;
    std::size_t skipped_gates = 0;
    std::vector<BenchmarkCaseSummary> cases;
    std::vector<BenchmarkGateResult> gates;
};

std::filesystem::path default_baseline_path(const std::filesystem::path& workspace_root);
BenchmarkReport run_suite(const BenchmarkOptions& options);
bool gates_failed(const BenchmarkReport& report);
std::string to_json(const BenchmarkReport& report);
void write_report(const BenchmarkReport& report, const std::filesystem::path& path);

}  // namespace zephyr::bench
