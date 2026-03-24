#include "test_common.hpp"

namespace zephyr_tests {

void test_match_enum() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            enum State { Idle, Hurt(Int) }
            fn score(state: State) -> Int {
                return match state {
                    State::Idle => 0,
                    State::Hurt(value) => value,
                };
            }
        )",
        "unit_match",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_match", "score");
    require(handle.has_value(), "missing score handle");
    zephyr::ZephyrEnumValue value;
    value.type_name = "State";
    value.variant_name = "Hurt";
    value.payload.push_back(zephyr::ZephyrValue(7));
    const auto result = vm.call(*handle, {zephyr::ZephyrValue(value)});
    require(result.is_int() && result.as_int() == 7, "unexpected match result");
}

void test_match_guard_and_or_pattern() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            enum State { Idle, Hurt(Int) }

            fn score(state: State) -> Int {
                return match state {
                    State::Hurt(value) if value > 5 => value,
                    State::Idle | State::Hurt(_) => 0,
                };
            }
        )",
        "unit_match_guard_or",
        std::filesystem::current_path());

    const auto handle = vm.get_function("unit_match_guard_or", "score");
    require(handle.has_value(), "missing score guard/or handle");

    zephyr::ZephyrEnumValue hurt_big;
    hurt_big.type_name = "State";
    hurt_big.variant_name = "Hurt";
    hurt_big.payload.push_back(zephyr::ZephyrValue(9));
    const auto hurt_big_result = vm.call(*handle, {zephyr::ZephyrValue(hurt_big)});
    require(hurt_big_result.is_int() && hurt_big_result.as_int() == 9, "guarded match should keep matching payload");

    zephyr::ZephyrEnumValue hurt_small;
    hurt_small.type_name = "State";
    hurt_small.variant_name = "Hurt";
    hurt_small.payload.push_back(zephyr::ZephyrValue(3));
    const auto hurt_small_result = vm.call(*handle, {zephyr::ZephyrValue(hurt_small)});
    require(hurt_small_result.is_int() && hurt_small_result.as_int() == 0, "guard failure should fall through to OR-pattern arm");

    zephyr::ZephyrEnumValue idle;
    idle.type_name = "State";
    idle.variant_name = "Idle";
    const auto idle_result = vm.call(*handle, {zephyr::ZephyrValue(idle)});
    require(idle_result.is_int() && idle_result.as_int() == 0, "OR-pattern should match enum alternative");
}

void test_match_bool_and_nil_literals() {
    zephyr::ZephyrVM vm;
    vm.execute_string(
        R"(
            fn classify_bool(flag: Bool) -> Int {
                return match flag {
                    true => 1,
                    false => 2,
                };
            }

            fn classify_nil() -> Int {
                let value = nil;
                return match value {
                    nil => 3,
                    _ => 0,
                };
            }
        )",
        "unit_match_literals",
        std::filesystem::current_path());

    const auto bool_handle = vm.get_function("unit_match_literals", "classify_bool");
    require(bool_handle.has_value(), "missing classify_bool handle");
    const auto true_result = vm.call(*bool_handle, {zephyr::ZephyrValue(true)});
    require(true_result.is_int() && true_result.as_int() == 1, "true literal pattern should match");
    const auto false_result = vm.call(*bool_handle, {zephyr::ZephyrValue(false)});
    require(false_result.is_int() && false_result.as_int() == 2, "false literal pattern should match");

    const auto nil_handle = vm.get_function("unit_match_literals", "classify_nil");
    require(nil_handle.has_value(), "missing classify_nil handle");
    const auto nil_result = vm.call(*nil_handle);
    require(nil_result.is_int() && nil_result.as_int() == 3, "nil literal pattern should match");
}

void test_check_reports_parse_location() {
    zephyr::ZephyrVM vm;
    bool rejected = false;
    try {
        vm.check_string(
            R"(
                fn broken(a: Int -> Int {
                    return a;
                }
            )",
            "unit_parse_diag",
            std::filesystem::current_path());
    } catch (const std::exception& error) {
        rejected = true;
        const std::string message = error.what();
        require(message.find("unit_parse_diag:") != std::string::npos, "parse diagnostic should include module location");
        require(message.find("Expected") != std::string::npos, "parse diagnostic should include expectation text");
    }
    require(rejected, "check_string should reject malformed source");
}

void test_wave_e1_string_interpolation_and_optional_chaining() {
    zephyr::ZephyrVM vm;

    vm.execute_string(
        R"(
            struct Node { value: Int, next: Any }

            fn greeting(name: String) -> String {
                return f"hello {name}";
            }

            fn summary(a: Int, b: Int) -> String {
                return f"{a + b} result";
            }

            fn nested_text() -> String {
                return f"{("x")}";
            }

            fn nil_optional() -> Bool {
                return nil?.value == nil;
            }

            fn optional_member() -> Int {
                let third = Node { value: 7, next: nil };
                let second = Node { value: 5, next: third };
                let first = Node { value: 3, next: second };
                return first?.next?.next?.value;
            }

            fn optional_nil_chain() -> Bool {
                let first = Node { value: 1, next: nil };
                return first?.next?.value == nil;
            }
        )",
        "wave_e1_language",
        std::filesystem::current_path());

    const auto greeting = vm.get_function("wave_e1_language", "greeting");
    require(greeting.has_value(), "wave e1: greeting must exist");
    const auto greeting_result = vm.call(*greeting, {zephyr::ZephyrValue("zephyr")});
    require(greeting_result.is_string() && greeting_result.as_string() == "hello zephyr",
            "wave e1: greeting interpolation returned wrong result");

    const auto summary = vm.get_function("wave_e1_language", "summary");
    require(summary.has_value(), "wave e1: summary must exist");
    const auto summary_result = vm.call(*summary, {zephyr::ZephyrValue(2), zephyr::ZephyrValue(3)});
    require(summary_result.is_string() && summary_result.as_string() == "5 result",
            "wave e1: summary interpolation returned wrong result");

    const auto nested_text = vm.get_function("wave_e1_language", "nested_text");
    require(nested_text.has_value(), "wave e1: nested_text must exist");
    const auto nested_text_result = vm.call(*nested_text);
    require(nested_text_result.is_string() && nested_text_result.as_string() == "x",
            "wave e1: nested interpolation returned wrong result");

    const auto nil_optional = vm.get_function("wave_e1_language", "nil_optional");
    require(nil_optional.has_value(), "wave e1: nil_optional must exist");
    const auto nil_optional_result = vm.call(*nil_optional);
    require(nil_optional_result.is_bool() && nil_optional_result.as_bool(),
            "wave e1: nil optional chaining should produce nil");

    const auto optional_member = vm.get_function("wave_e1_language", "optional_member");
    require(optional_member.has_value(), "wave e1: optional_member must exist");
    const auto optional_member_result = vm.call(*optional_member);
    require(optional_member_result.is_int() && optional_member_result.as_int() == 7,
            "wave e1: optional member chain returned wrong result");

    const auto optional_nil_chain = vm.get_function("wave_e1_language", "optional_nil_chain");
    require(optional_nil_chain.has_value(), "wave e1: optional_nil_chain must exist");
    const auto optional_nil_chain_result = vm.call(*optional_nil_chain);
    require(optional_nil_chain_result.is_bool() && optional_nil_chain_result.as_bool(),
            "wave e1: optional nil chain should produce nil");
}

}  // namespace zephyr_tests
