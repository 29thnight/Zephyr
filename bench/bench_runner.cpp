#include "bench_runner.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <functional>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace zephyr::bench {
namespace {

using Clock = std::chrono::steady_clock;

struct BenchmarkMeasurement {
    std::uint64_t elapsed_ns = 0;
    std::string result;
    ZephyrRuntimeStats stats{};
};

using BenchmarkCaseFn = std::function<BenchmarkMeasurement(const BenchmarkOptions&)>;

struct BenchmarkCaseDefinition {
    std::string name;
    std::string description;
    BenchmarkCaseFn run_once;
};

std::string json_escape(const std::string& text) {
    std::ostringstream out;
    for (const char ch : text) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch)) << std::dec << std::setfill(' ');
                } else {
                    out << ch;
                }
                break;
        }
    }
    return out.str();
}

std::string now_utc_string() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string format_number(double value, int precision = 2) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

double superinstruction_hit_rate_pct(const ZephyrRuntimeStats& stats) {
    if (stats.vm.total_original_opcode_count == 0) {
        return 0.0;
    }
    return static_cast<double>(stats.vm.superinstruction_fusions) * 100.0 /
           static_cast<double>(stats.vm.total_original_opcode_count);
}

std::uint64_t percentile95_ns(std::vector<std::uint64_t> samples) {
    if (samples.empty()) {
        return 0;
    }
    std::sort(samples.begin(), samples.end());
    const std::size_t index = std::min(samples.size() - 1, (samples.size() * 95 + 99) / 100 - 1);
    return samples[index];
}

template <typename Getter>
double mean_stat(const std::vector<BenchmarkMeasurement>& measurements, Getter getter) {
    if (measurements.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const auto& measurement : measurements) {
        total += static_cast<double>(getter(measurement.stats));
    }
    return total / static_cast<double>(measurements.size());
}

double safe_divide(double numerator, double denominator) {
    if (denominator <= 0.0) {
        return 0.0;
    }
    return numerator / denominator;
}

BenchmarkCaseSummary summarize_case(const BenchmarkCaseDefinition& definition,
                                    const std::vector<BenchmarkMeasurement>& measurements,
                                    const BenchmarkOptions& options) {
    BenchmarkCaseSummary summary;
    summary.name = definition.name;
    summary.description = definition.description;
    summary.warmup_iterations = options.config.benchmark.warmup_iterations;
    summary.measure_iterations = options.config.benchmark.measure_iterations;
    if (measurements.empty()) {
        return summary;
    }

    std::vector<std::uint64_t> elapsed;
    elapsed.reserve(measurements.size());
    for (const auto& measurement : measurements) {
        elapsed.push_back(measurement.elapsed_ns);
    }

    const auto minmax = std::minmax_element(elapsed.begin(), elapsed.end());
    summary.min_ns = *minmax.first;
    summary.max_ns = *minmax.second;
    summary.mean_ns = static_cast<std::uint64_t>(
        std::accumulate(elapsed.begin(), elapsed.end(), std::uint64_t{0}) / std::max<std::size_t>(std::size_t{1}, elapsed.size()));
    summary.p95_ns = percentile95_ns(elapsed);
    summary.last_result = measurements.back().result;
    summary.last_stats = measurements.back().stats;
    summary.mean_full_gc_cycles = mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.gc.total_full_collections; });
    summary.mean_young_gc_cycles =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.gc.total_young_collections; });
    summary.mean_promotions = mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.gc.total_promotions; });
    summary.mean_host_resolves = mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.handle.resolve_count; });
    summary.mean_coroutine_resumes =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.coroutine.total_coroutine_resume_calls; });
    summary.mean_ast_fallbacks =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.ast_fallback_executions; });
    summary.mean_opcode_count = mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.opcode_count; });
    summary.mean_superinstruction_fusions =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.superinstruction_fusions; });
    summary.mean_total_original_opcode_count =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.total_original_opcode_count; });
    summary.mean_superinstruction_hit_rate_pct = mean_stat(measurements, [](const ZephyrRuntimeStats& stats) {
        return superinstruction_hit_rate_pct(stats);
    });
    summary.mean_local_binding_cache_hits =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.local_binding_cache_hits; });
    summary.mean_local_binding_cache_misses =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.local_binding_cache_misses; });
    summary.mean_global_binding_cache_hits =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.global_binding_cache_hits; });
    summary.mean_global_binding_cache_misses =
        mean_stat(measurements, [](const ZephyrRuntimeStats& stats) { return stats.vm.global_binding_cache_misses; });
    return summary;
}

std::unordered_map<std::string, std::unordered_map<std::string, double>> load_baseline_metrics(const std::filesystem::path& path) {
    std::unordered_map<std::string, std::unordered_map<std::string, double>> metrics;
    std::ifstream input(path);
    if (!input) {
        return metrics;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    const std::string text = buffer.str();

    std::size_t position = 0;
    while (true) {
        const std::size_t name_key = text.find("\"name\"", position);
        if (name_key == std::string::npos) {
            break;
        }
        const std::size_t first_quote = text.find('"', name_key + 6);
        if (first_quote == std::string::npos) {
            break;
        }
        const std::size_t second_quote = text.find('"', first_quote + 1);
        if (second_quote == std::string::npos) {
            break;
        }
        const std::string case_name = text.substr(first_quote + 1, second_quote - first_quote - 1);
        const std::size_t object_end = text.find("}", second_quote);
        if (object_end == std::string::npos) {
            break;
        }
        const std::string object_text = text.substr(second_quote, object_end - second_quote);

        const auto parse_metric = [&](const std::string& key) -> std::optional<double> {
            const std::size_t metric_key = object_text.find("\"" + key + "\"");
            if (metric_key == std::string::npos) {
                return std::nullopt;
            }
            const std::size_t colon = object_text.find(':', metric_key);
            if (colon == std::string::npos) {
                return std::nullopt;
            }
            std::size_t number_start = colon + 1;
            while (number_start < object_text.size() && std::isspace(static_cast<unsigned char>(object_text[number_start]))) {
                ++number_start;
            }
            std::size_t number_end = number_start;
            while (number_end < object_text.size() &&
                   (std::isdigit(static_cast<unsigned char>(object_text[number_end])) || object_text[number_end] == '.')) {
                ++number_end;
            }
            if (number_start == number_end) {
                return std::nullopt;
            }
            return std::stod(object_text.substr(number_start, number_end - number_start));
        };

        for (const std::string key : {"mean_ns",
                                      "p95_ns",
                                      "mean_full_gc_cycles",
                                      "mean_host_resolves",
                                      "mean_coroutine_resumes",
                                      "mean_ast_fallbacks"}) {
            if (const auto value = parse_metric(key); value.has_value()) {
                metrics[case_name][key] = *value;
            }
        }

        position = object_end + 1;
    }
    return metrics;
}

void append_gate(std::vector<BenchmarkGateResult>& gates,
                 std::string name,
                 std::string status,
                 std::string detail,
                 double current_value = 0.0,
                 double baseline_value = 0.0,
                 double target_value = 0.0,
                 std::string unit = {}) {
    BenchmarkGateResult gate;
    gate.name = std::move(name);
    gate.status = std::move(status);
    gate.detail = std::move(detail);
    gate.current_value = current_value;
    gate.baseline_value = baseline_value;
    gate.target_value = target_value;
    gate.unit = std::move(unit);
    gates.push_back(std::move(gate));
}

std::vector<BenchmarkGateResult> build_gates(const BenchmarkReport& report) {
    std::vector<BenchmarkGateResult> gates;
    const auto find_case = [&](const std::string& name) -> const BenchmarkCaseSummary* {
        for (const auto& entry : report.cases) {
            if (entry.name == name) {
                return &entry;
            }
        }
        return nullptr;
    };

    bool native_path_ok = true;
    double worst_ast_fallbacks = 0.0;
    for (const auto& entry : report.cases) {
        worst_ast_fallbacks = std::max(worst_ast_fallbacks, entry.mean_ast_fallbacks);
        if (entry.mean_ast_fallbacks > 0.0) {
            native_path_ok = false;
        }
    }
    append_gate(gates,
                "native_bytecode_no_ast_fallback",
                native_path_ok ? "pass" : "fail",
                "max_mean_ast_fallbacks=" + format_number(worst_ast_fallbacks, 2),
                worst_ast_fallbacks,
                0.0,
                0.0,
                "fallbacks");

    const bool requested_baseline_available = report.baseline_loaded && !report.baseline_path.empty();
    const auto baseline_metrics =
        requested_baseline_available ? load_baseline_metrics(report.baseline_path)
                                     : std::unordered_map<std::string, std::unordered_map<std::string, double>>{};
    const auto allocation_baseline_path = std::filesystem::path(report.workspace_root) / "bench" / "results" / "v1_baseline.json";
    const bool allocation_baseline_available = std::filesystem::exists(allocation_baseline_path);
    const auto allocation_baseline_metrics =
        allocation_baseline_available ? load_baseline_metrics(allocation_baseline_path)
                                      : std::unordered_map<std::string, std::unordered_map<std::string, double>>{};

    if (const auto* current = find_case("hot_arithmetic_loop")) {
        const auto baseline_case = baseline_metrics.find("hot_arithmetic_loop");
        if (requested_baseline_available && baseline_case != baseline_metrics.end() && baseline_case->second.contains("mean_ns") &&
            baseline_case->second.at("mean_ns") > 0.0) {
            const double baseline = baseline_case->second.at("mean_ns");
            const double improvement = 1.0 - (static_cast<double>(current->mean_ns) / baseline);
            append_gate(gates,
                        "hot_arithmetic_vs_v1",
                        improvement >= 0.25 ? "pass" : "fail",
                        "improvement=" + format_number(improvement * 100.0) + "% target=25%",
                        static_cast<double>(current->mean_ns),
                        baseline,
                        baseline * 0.75,
                        "ns");
        } else {
            append_gate(gates,
                        "hot_arithmetic_vs_v1",
                        "skipped",
                        requested_baseline_available ? "baseline missing hot_arithmetic_loop.mean_ns"
                                                     : "baseline file not provided or could not be read");
        }
    } else {
        append_gate(gates, "hot_arithmetic_vs_v1", "skipped", "current report missing hot_arithmetic_loop");
    }

    if (const auto* current = find_case("array_object_churn")) {
        const auto baseline_case = allocation_baseline_metrics.find("array_object_churn");
        if (allocation_baseline_available && baseline_case != allocation_baseline_metrics.end() &&
            baseline_case->second.contains("mean_full_gc_cycles") &&
            baseline_case->second.at("mean_full_gc_cycles") > 0.0) {
            const double baseline = baseline_case->second.at("mean_full_gc_cycles");
            const double improvement = 1.0 - (current->mean_full_gc_cycles / baseline);
            append_gate(gates,
                        "allocation_full_gc_vs_v1",
                        improvement >= 0.40 ? "pass" : "fail",
                        "improvement=" + format_number(improvement * 100.0) + "% target=40%",
                        current->mean_full_gc_cycles,
                        baseline,
                        baseline * 0.60,
                        "full_gc_cycles");
        } else {
            append_gate(gates,
                        "allocation_full_gc_vs_v1",
                        "skipped",
                        allocation_baseline_available ? "baseline missing array_object_churn.mean_full_gc_cycles"
                                                      : "v1 baseline file not provided or could not be read");
        }
    } else {
        append_gate(gates, "allocation_full_gc_vs_v1", "skipped", "current report missing array_object_churn");
    }

    if (const auto* current = find_case("host_handle_entity")) {
        const auto baseline_case = baseline_metrics.find("host_handle_entity");
        if (requested_baseline_available && baseline_case != baseline_metrics.end() && baseline_case->second.contains("mean_ns") &&
            baseline_case->second.contains("mean_host_resolves") && baseline_case->second.at("mean_host_resolves") > 0.0) {
            const double baseline = safe_divide(baseline_case->second.at("mean_ns"), baseline_case->second.at("mean_host_resolves"));
            const double current_cost = safe_divide(static_cast<double>(current->mean_ns), current->mean_host_resolves);
            const double allowed = baseline * 1.10;
            append_gate(gates,
                        "host_handle_resolve_cost_vs_v1",
                        current_cost <= allowed ? "pass" : "fail",
                        "current=" + format_number(current_cost) + "ns/resolve baseline=" + format_number(baseline) +
                            "ns/resolve limit=" + format_number(allowed) + "ns/resolve",
                        current_cost,
                        baseline,
                        allowed,
                        "ns_per_resolve");
        } else {
            append_gate(gates,
                        "host_handle_resolve_cost_vs_v1",
                        "skipped",
                        requested_baseline_available ? "baseline missing host_handle_entity cost inputs"
                                                     : "baseline file not provided or could not be read");
        }
    } else {
        append_gate(gates, "host_handle_resolve_cost_vs_v1", "skipped", "current report missing host_handle_entity");
    }

    if (const auto* current = find_case("coroutine_yield_resume")) {
        const auto baseline_case = baseline_metrics.find("coroutine_yield_resume");
        if (requested_baseline_available && baseline_case != baseline_metrics.end() && baseline_case->second.contains("mean_ns") &&
            baseline_case->second.contains("mean_coroutine_resumes") && baseline_case->second.at("mean_coroutine_resumes") > 0.0) {
            const double baseline = safe_divide(baseline_case->second.at("mean_ns"), baseline_case->second.at("mean_coroutine_resumes"));
            const double current_cost = safe_divide(static_cast<double>(current->mean_ns), current->mean_coroutine_resumes);
            const double allowed = baseline * 1.10;
            append_gate(gates,
                        "coroutine_resume_cost_vs_v1",
                        current_cost <= allowed ? "pass" : "fail",
                        "current=" + format_number(current_cost) + "ns/resume baseline=" + format_number(baseline) +
                            "ns/resume limit=" + format_number(allowed) + "ns/resume",
                        current_cost,
                        baseline,
                        allowed,
                        "ns_per_resume");
        } else {
            append_gate(gates,
                        "coroutine_resume_cost_vs_v1",
                        "skipped",
                        requested_baseline_available ? "baseline missing coroutine_yield_resume cost inputs"
                                                     : "baseline file not provided or could not be read");
        }
    } else {
        append_gate(gates, "coroutine_resume_cost_vs_v1", "skipped", "current report missing coroutine_yield_resume");
    }

    return gates;
}

std::filesystem::path default_output_path(const std::filesystem::path& root) {
    return root / "bench" / "results" / "latest.json";
}

}  // namespace

std::filesystem::path default_baseline_path(const std::filesystem::path& workspace_root) {
    return workspace_root / "bench" / "results" / "v1_baseline.json";
}

namespace {

std::string stable_asset_module_source() {
    return R"(
        struct Exported { value: int }

        fn main() -> Array {
            let asset = stable_asset();
            return [asset, Exported { value: 9 }];
        }
    )";
}

BenchmarkMeasurement run_module_import_case(const BenchmarkOptions& options) {
    ZephyrVM vm(options.config);
    const auto path = options.workspace_root / "examples" / "import_main.zph";
    const auto start = Clock::now();
    vm.execute_file(path);
    const auto handle = vm.get_function(std::filesystem::weakly_canonical(path).string(), "main");
    ZephyrValue result;
    if (handle.has_value()) {
        result = vm.call(*handle);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return BenchmarkMeasurement{static_cast<std::uint64_t>(elapsed), to_string(result), vm.runtime_stats()};
}

BenchmarkMeasurement run_hot_arithmetic_case(const BenchmarkOptions& options) {
    ZephyrVM vm(options.config);
    vm.execute_string(
        R"(
            fn main() -> int {
                let mut acc = 0;
                let mut i = 0;
                while i < 20000 {
                    acc = acc + (i % 7);
                    i = i + 1;
                }
                return acc;
            }
        )",
        "bench_hot_arithmetic",
        options.workspace_root);
    const auto handle = vm.get_function("bench_hot_arithmetic", "main");
    const auto start = Clock::now();
    const ZephyrValue result = vm.call(*handle);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return BenchmarkMeasurement{static_cast<std::uint64_t>(elapsed), to_string(result), vm.runtime_stats()};
}

BenchmarkMeasurement run_array_object_churn_case(const BenchmarkOptions& options) {
    ZephyrVM vm(options.config);
    vm.execute_string(
        R"(
            struct Pair { left: int, right: int }

            fn main() -> int {
                let mut total = 0;
                let mut i = 0;
                while i < 2500 {
                    let arr = [i, i + 1, i + 2, i + 3];
                    let pair = Pair { left: i, right: i + 1 };
                    total = total + arr[0] + pair.left + pair.right;
                    i = i + 1;
                }
                return total;
            }
        )",
        "bench_array_object_churn",
        options.workspace_root);
    const auto handle = vm.get_function("bench_array_object_churn", "main");
    const auto start = Clock::now();
    const ZephyrValue result = vm.call(*handle);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return BenchmarkMeasurement{static_cast<std::uint64_t>(elapsed), to_string(result), vm.runtime_stats()};
}

BenchmarkMeasurement run_host_handle_case(const BenchmarkOptions& options) {
    struct Entity {
        int hp = 150;
    };

    auto klass = std::make_shared<ZephyrHostClass>("Entity");
    klass->add_method(
        "damage",
        [](void* instance, const std::vector<ZephyrValue>& args) {
            auto* entity = static_cast<Entity*>(instance);
            entity->hp -= static_cast<int>(args.at(0).as_int());
            return ZephyrValue(entity->hp);
        });
    klass->add_property(
        "hp",
        [](void* instance) { return ZephyrValue(static_cast<Entity*>(instance)->hp); },
        [](void* instance, const ZephyrValue& value) { static_cast<Entity*>(instance)->hp = static_cast<int>(value.as_int()); });

    const auto entity = std::make_shared<Entity>();
    ZephyrVM vm(options.config);
    vm.register_global_function(
        "player",
        [klass, entity](const std::vector<ZephyrValue>&) {
            return ZephyrValue(ZephyrHostObjectRef{klass, entity, ZephyrHostHandleKind::Entity, ZephyrHostHandleLifetime::Persistent});
        },
        {},
        "HostObject");
    vm.execute_string(
        R"(
            fn main() -> int {
                let p = player();
                let mut total = 0;
                let mut i = 0;
                while i < 1500 {
                    p.damage(1);
                    total = total + p.hp;
                    i = i + 1;
                }
                return total;
            }
        )",
        "bench_host_handle",
        options.workspace_root);
    const auto handle = vm.get_function("bench_host_handle", "main");
    const auto start = Clock::now();
    const ZephyrValue result = vm.call(*handle);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return BenchmarkMeasurement{static_cast<std::uint64_t>(elapsed), to_string(result), vm.runtime_stats()};
}

BenchmarkMeasurement run_coroutine_case(const BenchmarkOptions& options) {
    ZephyrVM vm(options.config);
    vm.execute_string(
        R"(
            fn make_counter(limit: int) -> Coroutine {
                return coroutine fn() -> int {
                    let mut i = 0;
                    while i < limit {
                        yield i;
                        i = i + 1;
                    }
                    return limit;
                };
            }
        )",
        "bench_coroutines",
        options.workspace_root);
    const auto handle = vm.get_function("bench_coroutines", "make_counter");
    const auto coroutine = vm.spawn_coroutine(*handle, {ZephyrValue(400)});
    const auto start = Clock::now();
    ZephyrValue last;
    while (true) {
        last = vm.resume(*coroutine);
        const auto info = vm.query_coroutine(*coroutine);
        if (!info.has_value() || info->completed) {
            break;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return BenchmarkMeasurement{static_cast<std::uint64_t>(elapsed), to_string(last), vm.runtime_stats()};
}

BenchmarkMeasurement run_serialization_case(const BenchmarkOptions& options) {
    struct Asset {
        std::string name = "mesh/player";
    };

    auto klass = std::make_shared<ZephyrHostClass>("AssetRef");
    klass->add_property("name", [](void* instance) { return ZephyrValue(static_cast<Asset*>(instance)->name); });

    const auto asset = std::make_shared<Asset>();
    ZephyrVM vm(options.config);
    vm.register_global_function(
        "stable_asset",
        [klass, asset](const std::vector<ZephyrValue>&) {
            ZephyrHostObjectRef handle;
            handle.host_class = klass;
            handle.instance = asset;
            handle.kind = ZephyrHostHandleKind::Asset;
            handle.lifetime = ZephyrHostHandleLifetime::Stable;
            handle.stable_guid = ZephyrGuid128{0xA11CEULL, 0xBEEF2026ULL};
            handle.policy.allow_serialize = true;
            handle.policy.allow_cross_scene = true;
            handle.policy.strong_residency_allowed = true;
            handle.policy.weak_by_default = false;
            handle.has_explicit_policy = true;
            return ZephyrValue(handle);
        },
        {},
        "HostObject");
    vm.execute_string(stable_asset_module_source(), "bench_serialization", options.workspace_root);
    const auto handle = vm.get_function("bench_serialization", "main");
    const auto start = Clock::now();
    const ZephyrValue value = vm.call(*handle);
    const ZephyrValue serialized = vm.serialize_value(value);
    const ZephyrValue round_trip = vm.deserialize_value(serialized);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    return BenchmarkMeasurement{static_cast<std::uint64_t>(elapsed), to_string(round_trip), vm.runtime_stats()};
}

std::vector<BenchmarkCaseDefinition> make_cases() {
    return {
        {"module_import", "Loads a file-backed module with relative import and executes main().", run_module_import_case},
        {"hot_arithmetic_loop", "Runs a tight arithmetic loop to track raw bytecode throughput.", run_hot_arithmetic_case},
        {"array_object_churn", "Allocates arrays and structs repeatedly to stress nursery and promotion policy.", run_array_object_churn_case},
        {"host_handle_entity", "Exercises repeated entity handle resolution, member access, and host calls.", run_host_handle_case},
        {"coroutine_yield_resume", "Creates a retained coroutine handle and resumes it until completion.", run_coroutine_case},
        {"serialization_export", "Serializes and deserializes a save-safe Stable host handle payload.", run_serialization_case},
    };
}

std::string stats_to_json(const ZephyrRuntimeStats& stats, int indent) {
    const std::string pad(indent, ' ');
    const std::string pad2(indent + 2, ' ');
    std::ostringstream out;
    out << pad << "{\n";
    out << pad2 << "\"frame_epoch\": " << stats.frame_epoch << ",\n";
    out << pad2 << "\"tick_epoch\": " << stats.tick_epoch << ",\n";
    out << pad2 << "\"scene_epoch\": " << stats.scene_epoch << ",\n";
    out << pad2 << "\"gc\": {\n";
    out << pad2 << "  \"live_objects\": " << stats.gc.live_objects << ",\n";
    out << pad2 << "  \"live_bytes\": " << stats.gc.live_bytes << ",\n";
    out << pad2 << "  \"young_objects\": " << stats.gc.young_objects << ",\n";
    out << pad2 << "  \"old_objects\": " << stats.gc.old_objects << ",\n";
    out << pad2 << "  \"total_young_collections\": " << stats.gc.total_young_collections << ",\n";
    out << pad2 << "  \"total_full_collections\": " << stats.gc.total_full_collections << ",\n";
    out << pad2 << "  \"total_promotions\": " << stats.gc.total_promotions << "\n";
    out << pad2 << "},\n";
    out << pad2 << "\"vm\": {\n";
    out << pad2 << "  \"opcode_count\": " << stats.vm.opcode_count << ",\n";
    out << pad2 << "  \"ast_fallback_executions\": " << stats.vm.ast_fallback_executions << ",\n";
    out << pad2 << "  \"local_binding_cache_hits\": " << stats.vm.local_binding_cache_hits << ",\n";
    out << pad2 << "  \"local_binding_cache_misses\": " << stats.vm.local_binding_cache_misses << ",\n";
    out << pad2 << "  \"global_binding_cache_hits\": " << stats.vm.global_binding_cache_hits << ",\n";
    out << pad2 << "  \"global_binding_cache_misses\": " << stats.vm.global_binding_cache_misses << ",\n";
    out << pad2 << "  \"callback_invocations\": " << stats.vm.callback_invocations << ",\n";
    out << pad2 << "  \"serialized_value_exports\": " << stats.vm.serialized_value_exports << ",\n";
    out << pad2 << "  \"deserialized_value_imports\": " << stats.vm.deserialized_value_imports << ",\n";
    out << pad2 << "  \"superinstruction_fusions\": " << stats.vm.superinstruction_fusions << ",\n";
    out << pad2 << "  \"total_original_opcode_count\": " << stats.vm.total_original_opcode_count << ",\n";
    out << pad2 << "  \"superinstruction_hit_rate_pct\": " << superinstruction_hit_rate_pct(stats) << ",\n";
    out << pad2 << "  \"superinstruction_summary\": \"superinstruction_fusions: "
        << stats.vm.superinstruction_fusions << " (hit_rate: " << format_number(superinstruction_hit_rate_pct(stats)) << "%)\"\n";
    out << pad2 << "},\n";
    out << pad2 << "\"coroutine\": {\n";
    out << pad2 << "  \"suspended_coroutines\": " << stats.coroutine.suspended_coroutines << ",\n";
    out << pad2 << "  \"completed_coroutines\": " << stats.coroutine.completed_coroutines << ",\n";
    out << pad2 << "  \"total_coroutine_resume_calls\": " << stats.coroutine.total_coroutine_resume_calls << ",\n";
    out << pad2 << "  \"total_coroutine_yields\": " << stats.coroutine.total_coroutine_yields << "\n";
    out << pad2 << "},\n";
    out << pad2 << "\"handle\": {\n";
    out << pad2 << "  \"resolve_count\": " << stats.handle.resolve_count << ",\n";
    out << pad2 << "  \"resolve_failures\": " << stats.handle.resolve_failures << ",\n";
    out << pad2 << "  \"stable_handles\": " << stats.handle.stable_handles << "\n";
    out << pad2 << "}\n";
    out << pad << "}";
    return out.str();
}

}  // namespace

BenchmarkReport run_suite(const BenchmarkOptions& input_options) {
    BenchmarkOptions options = input_options;
    if (options.workspace_root.empty()) {
        options.workspace_root = std::filesystem::current_path();
    }
    if (options.output_path.empty()) {
        options.output_path = default_output_path(options.workspace_root);
    }

    BenchmarkReport report;
    report.generated_at_utc = now_utc_string();
    report.workspace_root = std::filesystem::weakly_canonical(options.workspace_root).string();
    report.output_path = options.output_path.string();
    report.strict_gates = options.strict_gates;

    std::optional<std::filesystem::path> resolved_baseline = options.baseline_path;
    if (!resolved_baseline.has_value() && options.auto_discover_baseline) {
        const auto candidate = default_baseline_path(options.workspace_root);
        if (std::filesystem::exists(candidate)) {
            resolved_baseline = candidate;
        }
    }
    if (resolved_baseline.has_value()) {
        report.baseline_path = resolved_baseline->string();
        report.baseline_loaded = std::filesystem::exists(*resolved_baseline);
    }

    const auto cases = make_cases();
    for (const auto& definition : cases) {
        for (std::size_t i = 0; i < options.config.benchmark.warmup_iterations; ++i) {
            definition.run_once(options);
        }

        std::vector<BenchmarkMeasurement> measurements;
        measurements.reserve(options.config.benchmark.measure_iterations);
        for (std::size_t i = 0; i < options.config.benchmark.measure_iterations; ++i) {
            measurements.push_back(definition.run_once(options));
        }
        report.cases.push_back(summarize_case(definition, measurements, options));
    }

    report.gates = build_gates(report);
    for (const auto& gate : report.gates) {
        if (gate.status == "pass") {
            ++report.passed_gates;
        } else if (gate.status == "fail") {
            ++report.failed_gates;
        } else {
            ++report.skipped_gates;
        }
    }
    report.all_required_gates_passed = report.failed_gates == 0;
    return report;
}

bool gates_failed(const BenchmarkReport& report) {
    return report.failed_gates > 0;
}

std::string to_json(const BenchmarkReport& report) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"generated_at_utc\": \"" << json_escape(report.generated_at_utc) << "\",\n";
    out << "  \"workspace_root\": \"" << json_escape(report.workspace_root) << "\",\n";
    out << "  \"output_path\": \"" << json_escape(report.output_path) << "\",\n";
    out << "  \"baseline_path\": \"" << json_escape(report.baseline_path) << "\",\n";
    out << "  \"baseline_loaded\": " << (report.baseline_loaded ? "true" : "false") << ",\n";
    out << "  \"strict_gates\": " << (report.strict_gates ? "true" : "false") << ",\n";
    out << "  \"all_required_gates_passed\": " << (report.all_required_gates_passed ? "true" : "false") << ",\n";
    out << "  \"passed_gates\": " << report.passed_gates << ",\n";
    out << "  \"failed_gates\": " << report.failed_gates << ",\n";
    out << "  \"skipped_gates\": " << report.skipped_gates << ",\n";
    out << "  \"cases\": [\n";
    for (std::size_t i = 0; i < report.cases.size(); ++i) {
        const auto& entry = report.cases[i];
        out << "    {\n";
        out << "      \"name\": \"" << json_escape(entry.name) << "\",\n";
        out << "      \"description\": \"" << json_escape(entry.description) << "\",\n";
        out << "      \"warmup_iterations\": " << entry.warmup_iterations << ",\n";
        out << "      \"measure_iterations\": " << entry.measure_iterations << ",\n";
        out << "      \"min_ns\": " << entry.min_ns << ",\n";
        out << "      \"mean_ns\": " << entry.mean_ns << ",\n";
        out << "      \"max_ns\": " << entry.max_ns << ",\n";
        out << "      \"p95_ns\": " << entry.p95_ns << ",\n";
        out << "      \"mean_full_gc_cycles\": " << entry.mean_full_gc_cycles << ",\n";
        out << "      \"mean_young_gc_cycles\": " << entry.mean_young_gc_cycles << ",\n";
        out << "      \"mean_promotions\": " << entry.mean_promotions << ",\n";
        out << "      \"mean_host_resolves\": " << entry.mean_host_resolves << ",\n";
        out << "      \"mean_coroutine_resumes\": " << entry.mean_coroutine_resumes << ",\n";
        out << "      \"mean_ast_fallbacks\": " << entry.mean_ast_fallbacks << ",\n";
        out << "      \"mean_opcode_count\": " << entry.mean_opcode_count << ",\n";
        out << "      \"mean_superinstruction_fusions\": " << entry.mean_superinstruction_fusions << ",\n";
        out << "      \"mean_total_original_opcode_count\": " << entry.mean_total_original_opcode_count << ",\n";
        out << "      \"mean_superinstruction_hit_rate_pct\": " << entry.mean_superinstruction_hit_rate_pct << ",\n";
        out << "      \"mean_local_binding_cache_hits\": " << entry.mean_local_binding_cache_hits << ",\n";
        out << "      \"mean_local_binding_cache_misses\": " << entry.mean_local_binding_cache_misses << ",\n";
        out << "      \"mean_global_binding_cache_hits\": " << entry.mean_global_binding_cache_hits << ",\n";
        out << "      \"mean_global_binding_cache_misses\": " << entry.mean_global_binding_cache_misses << ",\n";
        out << "      \"last_result\": \"" << json_escape(entry.last_result) << "\",\n";
        out << "      \"last_stats\": " << stats_to_json(entry.last_stats, 6) << "\n";
        out << "    }";
        if (i + 1 < report.cases.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ],\n";
    out << "  \"gates\": [\n";
    for (std::size_t i = 0; i < report.gates.size(); ++i) {
        const auto& gate = report.gates[i];
        out << "    {\n";
        out << "      \"name\": \"" << json_escape(gate.name) << "\",\n";
        out << "      \"status\": \"" << json_escape(gate.status) << "\",\n";
        out << "      \"detail\": \"" << json_escape(gate.detail) << "\",\n";
        out << "      \"current_value\": " << gate.current_value << ",\n";
        out << "      \"baseline_value\": " << gate.baseline_value << ",\n";
        out << "      \"target_value\": " << gate.target_value << ",\n";
        out << "      \"unit\": \"" << json_escape(gate.unit) << "\"\n";
        out << "    }";
        if (i + 1 < report.gates.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

void write_report(const BenchmarkReport& report, const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to open benchmark output: " + path.string());
    }
    output << to_json(report);
}

}  // namespace zephyr::bench
