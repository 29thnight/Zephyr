#pragma once

#include "zephyr/api.hpp"
#include "bench_runner.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace zephyr_tests {

inline void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

inline std::size_t debug_live_gc_objects() {
#ifdef DEBUG_LEAK_CHECK
    return zephyr::debug_gc_stats().live_objects;
#else
    return 0;
#endif
}

inline void require_no_debug_gc_leaks(std::size_t baseline, const std::string& context) {
#ifdef DEBUG_LEAK_CHECK
    const auto live_objects = zephyr::debug_gc_stats().live_objects;
    require(live_objects == baseline, context + ": leaked " + std::to_string(live_objects - baseline) + " GC object(s)");
#else
    (void)baseline;
    (void)context;
#endif
}

inline const zephyr::ZephyrRecord& require_serialized_envelope(const zephyr::ZephyrValue& value, const std::string& context) {
    require(value.is_record(), context + ": serialized value should be a record envelope");
    const auto& envelope = value.as_record();
    require(envelope.type_name == "ZephyrSaveEnvelope", context + ": envelope type_name should be ZephyrSaveEnvelope");
    require(envelope.fields.contains("schema"), context + ": envelope should have a schema field");
    require(envelope.fields.contains("version"), context + ": envelope should have a version field");
    require(envelope.fields.contains("payload"), context + ": envelope should have a payload field");
    require(envelope.fields.at("schema").is_string() && envelope.fields.at("schema").as_string() == "zephyr.save",
            context + ": envelope schema should be zephyr.save");
    require(envelope.fields.at("version").is_int() && envelope.fields.at("version").as_int() == 1,
            context + ": envelope version should be 1");
    return envelope;
}

inline const zephyr::ZephyrRecord& require_serialized_node(const zephyr::ZephyrValue& value, const std::string& context) {
    require(value.is_record(), context + ": serialized node should be a record");
    const auto& node = value.as_record();
    require(node.type_name == "ZephyrSaveNode", context + ": node type_name should be ZephyrSaveNode");
    require(node.fields.contains("kind") && node.fields.at("kind").is_string(), context + ": node should have a string kind field");
    return node;
}

inline const zephyr::bench::BenchmarkGateResult* find_benchmark_gate(const zephyr::bench::BenchmarkReport& report,
                                                             const std::string& name) {
    for (const auto& gate : report.gates) {
        if (gate.name == name) {
            return &gate;
        }
    }
    return nullptr;
}

class StreamCapture {
public:
    explicit StreamCapture(std::ostream& stream) : stream_(stream), previous_(stream.rdbuf(buffer_.rdbuf())) {}
    ~StreamCapture() { stream_.rdbuf(previous_); }

    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;

    std::string str() const { return buffer_.str(); }

private:
    std::ostream& stream_;
    std::streambuf* previous_ = nullptr;
    std::ostringstream buffer_;
};

inline std::string normalize_captured_output(std::string text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            continue;
        }
        normalized.push_back(text[index]);
    }
    while (!normalized.empty() && normalized.back() == '\n') {
        normalized.pop_back();
    }
    return normalized;
}

inline void run_corpus_case(const std::filesystem::path& path, const std::string& expected_output) {
    zephyr::ZephyrVM vm;
    StreamCapture output(std::cout);

    vm.execute_file(path);

    const auto module_name = std::filesystem::weakly_canonical(path).string();
    const auto main_handle = vm.get_function(module_name, "main");
    require(main_handle.has_value(), "corpus: missing main handle for " + path.string());
    vm.call(*main_handle);

    const auto actual_output = normalize_captured_output(output.str());
    require(actual_output == expected_output,
            "corpus: unexpected output for " + path.string() + "\nexpected:\n" + expected_output + "\nactual:\n" + actual_output);
}

void test_execute_and_call();
void test_match_enum();
void test_match_guard_and_or_pattern();
void test_match_struct_range_and_if_let();
void test_while_let_and_array_tuple_patterns();
void test_language_expansions_wave_g();
void test_match_bool_and_nil_literals();
void test_trait_impl_dispatch();
void test_struct_literal_field_shorthand();
void test_error_propagation_lowering();
void test_generic_type_parameters();
void test_dap_server_smoke();
void test_snapshot_restore();
void test_host_object();
void test_host_object_identity_and_long_lived_handle();
void test_name_based_event_registration_and_error_propagation();
void test_frame_handle_storage_and_capture_are_rejected();
void test_handle_invalidation_and_epochs();
void test_incremental_gc_stats_progress();
void test_string_literal_interning_tracks_hits_and_misses();
void test_core_stdlib_helpers();
void test_stdlib_module_imports();
void test_package_root_imports_lib_entry();
void test_check_reports_parse_location();
void test_check_rejects_circular_import();
void test_check_reports_trait_impl_missing_method();
void test_check_accepts_complete_trait_impl_and_warns_on_extra_method();
void test_check_reports_function_signature_mismatch_with_definition_location();
void test_check_warns_for_optional_chain_nil_propagation();
void test_check_rejects_module_member_that_is_not_exported();
void test_runtime_error_includes_stack_trace();
void test_runtime_trait_method_not_found_includes_hint();
void test_check_warns_for_non_exhaustive_match_cases();
void test_gc_verify_full_and_dirty_barrier_dedup();
void test_gc_stress_mode_advances_at_bytecode_safe_points();
void test_serialization_requires_stable_handles();
void test_serialization_schema_rejects_unknown_version();
void test_bytecode_loop_and_branch();
void test_bytecode_for_in_array();
void test_bytecode_for_in_range_syntax();
void test_bytecode_struct_enum_match();
void test_bytecode_local_slots_shadow_and_closure_sync();
void test_closure_cell_capture_survives_outer_return_and_gc();
void test_coroutine_upvalue_bytecode_avoids_ast_fallback();
void test_lightweight_coroutine_skips_local_binding_cache();
void test_transitive_upvalue_bindings_support_nested_closure_after_outer_return();
void test_compound_member_and_index_assignment_run_on_native_bytecode_path();
void test_resume_expression_runs_on_native_bytecode_path();
void test_gc_collects_unreachable_cycles_and_returns_to_baseline();
void test_gc_preserves_temporary_callee_during_collection();
void test_module_bytecode_import_and_top_level_execution();
void test_module_bytecode_cache_reuse_and_invalidation();
void test_coroutine_resume_and_done();
void test_coroutine_is_lazy_and_preserves_state_across_gc();
void test_nested_script_function_yield_inside_coroutine();
void test_deeply_nested_script_function_yield_survives_gc();
void test_coroutine_runtime_stats_and_dump();
void test_yield_outside_coroutine_rejected();
void test_coroutine_rejects_frame_handle_yield();
void test_break_continue_and_compound_assignment();
void test_v2_callback_handle_and_dump_bytecode();
void test_v2_global_and_module_name_slots_use_cached_bindings();
void test_v2_coroutine_handles_young_gc_and_deserialize();
void test_gc_pause_stats_api();
void test_gc_trace_json_export();
void test_coroutine_trace_report_sequence();
void test_v2_minor_remembered_set_preserves_old_to_young_edges();
void test_v2_struct_cards_preserve_old_to_young_field_edges();
void test_v2_environment_cards_preserve_old_local_upvalue_cells();
void test_v2_suspended_coroutine_cards_preserve_local_young_values();
void test_v2_suspended_coroutine_syncs_binding_backed_locals_before_yield();
void test_v2_benchmark_gates_compare_against_baseline();
void test_v2_benchmark_auto_discovers_default_baseline();
void test_gc_object_sizeof_baselines();
void test_phase7_compact_old_generation();
void test_phase1_1_lightweight_call_skips_environment_allocation();
void test_phase1_1_nested_closure_with_upvalue_mutation();
void test_phase1_2_constant_folding_reduces_bytecode();
void test_wave_a_coroutine_set_unordered();
void test_wave_a_barrier_young_owner_skips_remembered_set();
void test_wave_a_int_arithmetic_fastpath();
void test_wave_e1_string_interpolation_and_optional_chaining();
void test_wave_e1_class_binder();
void test_wave_e1_profiler_report();
void test_corpus_scripts();
void test_wave_f_coroutine_trace_report();
void test_generic_function_parsing();
void test_generic_struct_parsing();
void test_generic_function_execution();
void test_generic_struct_instantiation();
void test_generic_multi_param_function();
void test_wave_l_result_ok_err();
void test_wave_l_result_question_op();
void test_wave_l_array_pattern();
void test_wave_l_struct_pattern();
void test_wave_l_nested_pattern();
void test_named_import();
void test_re_export();
void test_circular_import_error();
void test_std_math();
void test_std_string();

}  // namespace zephyr_tests
