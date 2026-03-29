#include "test_common.hpp"

namespace zephyr_tests {

void test_coroutine_trace_report_sequence() {
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
        "unit_coroutine_trace_report",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_coroutine_trace_report", "make_counter");
    require(handle.has_value(), "coroutine trace: missing make_counter handle");

    vm.start_profiling();
    vm.start_coroutine_trace();

    const auto coroutine = vm.spawn_coroutine(*handle);
    require(coroutine.has_value() && coroutine->valid(), "coroutine trace: spawn_coroutine should return a valid handle");

    const auto first = vm.resume(*coroutine);
    require(first.is_int() && first.as_int() == 6, "coroutine trace: unexpected first yield");

    const auto second = vm.resume(*coroutine);
    require(second.is_int() && second.as_int() == 9, "coroutine trace: unexpected final result");

    vm.stop_coroutine_trace();
    const auto report = vm.stop_profiling();

    require(!report.coroutine_trace.empty(), "coroutine trace: report should include coroutine events");

    const auto find_event = [&](zephyr::CoroutineTraceEvent::Type type) {
        return std::find_if(report.coroutine_trace.begin(), report.coroutine_trace.end(),
                            [type](const zephyr::CoroutineTraceEvent& event) { return event.type == type; });
    };

    const auto created = find_event(zephyr::CoroutineTraceEvent::Type::Created);
    const auto first_resumed = find_event(zephyr::CoroutineTraceEvent::Type::Resumed);
    const auto yielded = find_event(zephyr::CoroutineTraceEvent::Type::Yielded);
    const auto completed = find_event(zephyr::CoroutineTraceEvent::Type::Completed);
    require(created != report.coroutine_trace.end(), "coroutine trace: Created event should be present");
    require(first_resumed != report.coroutine_trace.end(), "coroutine trace: Resumed event should be present");
    require(yielded != report.coroutine_trace.end(), "coroutine trace: Yielded event should be present");
    require(completed != report.coroutine_trace.end(), "coroutine trace: Completed event should be present");

    auto second_resumed = report.coroutine_trace.end();
    bool saw_first_resumed = false;
    for (auto it = report.coroutine_trace.begin(); it != report.coroutine_trace.end(); ++it) {
        if (it->type != zephyr::CoroutineTraceEvent::Type::Resumed) {
            continue;
        }
        if (!saw_first_resumed) {
            saw_first_resumed = true;
            continue;
        }
        second_resumed = it;
        break;
    }
    require(second_resumed != report.coroutine_trace.end(), "coroutine trace: second Resumed event should be present");

    const auto coroutine_id = created->coroutine_id;
    require(coroutine_id != 0, "coroutine trace: coroutine id should be assigned");
    require(first_resumed->coroutine_id == coroutine_id && yielded->coroutine_id == coroutine_id &&
                second_resumed->coroutine_id == coroutine_id && completed->coroutine_id == coroutine_id,
            "coroutine trace: lifecycle events should share the same coroutine id");

    require(created < first_resumed, "coroutine trace: Created should precede first Resumed");
    require(first_resumed < yielded, "coroutine trace: first Resumed should precede Yielded");
    require(yielded < second_resumed, "coroutine trace: Yielded should precede second Resumed");
    require(second_resumed < completed, "coroutine trace: second Resumed should precede Completed");
}

void test_v2_benchmark_gates_compare_against_baseline() {
    const auto baseline_path = std::filesystem::temp_directory_path() / "zephyr_v2_unit_baseline.json";
    {
        std::ofstream baseline(baseline_path, std::ios::binary);
        require(static_cast<bool>(baseline), "failed to create temporary benchmark baseline");
        baseline << R"({
  "cases": [
    { "name": "hot_arithmetic_loop", "mean_ns": 1000000000 },
    { "name": "array_object_churn", "mean_full_gc_cycles": 10 },
    { "name": "host_handle_entity", "mean_ns": 100000000, "mean_host_resolves": 3000 },
    { "name": "coroutine_yield_resume", "mean_ns": 30000000, "mean_coroutine_resumes": 401 }
  ]
})";
    }

    struct ScopedRemove {
        std::filesystem::path path;
        ~ScopedRemove() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{baseline_path};

    zephyr::bench::BenchmarkOptions options;
    options.workspace_root = std::filesystem::current_path();
    options.baseline_path = baseline_path;
    options.strict_gates = true;
    options.config.benchmark.warmup_iterations = 0;
    options.config.benchmark.measure_iterations = 1;

    const auto report = zephyr::bench::run_suite(options);
    require(report.baseline_loaded, "benchmark suite should load the provided baseline");
    require(report.strict_gates, "benchmark report should remember strict gate mode");
    require(report.failed_gates == 0, "benchmark gates should pass against the generous synthetic baseline");
    require(report.passed_gates >= 3, "benchmark suite should record passing gates");
    require(!zephyr::bench::gates_failed(report), "strict gate helper should stay false when all gates pass");

    const auto* native_gate = find_benchmark_gate(report, "native_bytecode_no_ast_fallback");
    require(native_gate != nullptr && native_gate->status == "pass", "native bytecode gate should pass");

    const auto* hot_gate = find_benchmark_gate(report, "hot_arithmetic_vs_v1");
    require(hot_gate != nullptr && hot_gate->status == "pass", "hot arithmetic benchmark gate should compare against baseline");
    require(hot_gate->baseline_value > hot_gate->current_value, "hot arithmetic gate should report current/baseline values");

    const auto* host_gate = find_benchmark_gate(report, "host_handle_resolve_cost_vs_v1");
    require(host_gate != nullptr && host_gate->status == "pass", "host handle resolve cost gate should compare against baseline");

    const auto* coroutine_gate = find_benchmark_gate(report, "coroutine_resume_cost_vs_v1");
    require(coroutine_gate != nullptr && coroutine_gate->status == "pass", "coroutine resume cost gate should compare against baseline");
}

void test_v2_benchmark_auto_discovers_default_baseline() {
    zephyr::bench::BenchmarkOptions options;
    options.workspace_root = std::filesystem::current_path();
    options.strict_gates = true;
    options.config.benchmark.warmup_iterations = 0;
    options.config.benchmark.measure_iterations = 1;

    const auto expected_baseline = zephyr::bench::default_baseline_path(options.workspace_root);
    require(std::filesystem::exists(expected_baseline), "default benchmark baseline file should exist in the repository");

    const auto report = zephyr::bench::run_suite(options);
    require(report.baseline_loaded, "benchmark suite should auto-discover the default v1 baseline");
    require(report.baseline_path == expected_baseline.string(), "benchmark suite should report the discovered default baseline path");
    require(report.gates.size() >= 5, "auto-discovered baseline should enable the full acceptance gate set");
    require(report.skipped_gates == 0, "repository default baseline should activate all acceptance gates");
    require(report.failed_gates == 0, "repository default baseline should not fail current v2 gates");
}

void test_wave_e1_profiler_report() {
    zephyr::ZephyrVM vm;

    vm.execute_string(
        R"(
            fn fib(n: int) -> int {
                if n <= 1 {
                    return n;
                }
                return fib(n - 1) + fib(n - 2);
            }

            export fn run() -> int {
                return fib(10);
            }
        )",
        "wave_e1_profiler",
        std::filesystem::current_path());

    const auto run = vm.get_function("wave_e1_profiler", "run");
    require(run.has_value(), "wave e1: profiler run must exist");

    vm.start_profiling();
    const auto result = vm.call(*run);
    const auto report = vm.stop_profiling();

    require(result.is_int() && result.as_int() == 55, "wave e1: profiler fib result should be 55");
    require(!report.entries.empty(), "wave e1: profiler report must not be empty");

    bool found_fib = false;
    for (const auto& entry : report.entries) {
        if (entry.function_name == "fib") {
            found_fib = true;
            require(entry.call_count > 0, "wave e1: fib profile entry should record calls");
            break;
        }
    }
    require(found_fib, "wave e1: profiler report should include fib");
}

void test_wave_f_coroutine_trace_report() {
    zephyr::ZephyrVM vm;

    vm.execute_string(
        R"(
            export fn run() -> int {
                let worker = coroutine fn() -> int {
                    yield 1;
                    return 2;
                };

                let first = resume worker;
                let second = resume worker;
                return first + second;
            }
        )",
        "wave_f_coroutine_trace",
        std::filesystem::current_path());

    const auto run = vm.get_function("wave_f_coroutine_trace", "run");
    require(run.has_value(), "wave f: coroutine trace run must exist");

    vm.start_profiling();
    const auto result = vm.call(*run);
    const auto report = vm.stop_profiling();

    require(result.is_int() && result.as_int() == 3, "wave f: coroutine trace run should return 3");
    require(report.coroutine_trace.size() >= 5, "wave f: coroutine trace should contain lifecycle events");

    require(report.coroutine_trace[0].type == zephyr::CoroutineTraceEvent::Type::Created,
            "wave f: first coroutine event should be Created");
    require(report.coroutine_trace[1].type == zephyr::CoroutineTraceEvent::Type::Resumed,
            "wave f: second coroutine event should be Resumed");
    require(report.coroutine_trace[2].type == zephyr::CoroutineTraceEvent::Type::Yielded,
            "wave f: third coroutine event should be Yielded");
    require(report.coroutine_trace[3].type == zephyr::CoroutineTraceEvent::Type::Resumed,
            "wave f: fourth coroutine event should be Resumed");
    require(report.coroutine_trace[4].type == zephyr::CoroutineTraceEvent::Type::Completed,
            "wave f: fifth coroutine event should be Completed");

    const auto coroutine_id = report.coroutine_trace[0].coroutine_id;
    require(coroutine_id != 0, "wave f: coroutine trace should assign a non-zero coroutine id");
    for (std::size_t index = 1; index < 5; ++index) {
        require(report.coroutine_trace[index].coroutine_id == coroutine_id,
                "wave f: coroutine lifecycle events should share a coroutine id");
        require(report.coroutine_trace[index].timestamp_ns >= report.coroutine_trace[index - 1].timestamp_ns,
                "wave f: coroutine trace timestamps should be monotonic");
    }
}

}  // namespace zephyr_tests
