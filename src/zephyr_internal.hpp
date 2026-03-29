#pragma once

#include "zephyr/api.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <deque>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#endif


namespace zephyr {

[[noreturn]] inline void fail(const std::string& message) {
    throw std::runtime_error(message);
}

[[noreturn]] inline void fail_runtime_error(const std::string& message) {
    const std::size_t stack_index = message.find("\n  at ");
    if (stack_index == std::string::npos) {
        throw zephyr::ZephyrRuntimeError(message);
    }
    throw zephyr::ZephyrRuntimeError(message.substr(0, stack_index), message.substr(stack_index + 1));
}

struct Span;
std::string format_location(const std::string& module_name, const Span& span);

template <typename T>
using RuntimeResult = std::expected<T, std::string>;

using VoidResult = RuntimeResult<std::monostate>;

#ifdef _DEBUG
constexpr bool kBytecodeAstFallbackEnabled = true;
#else
constexpr bool kBytecodeAstFallbackEnabled = false;
#endif

template <typename T>
RuntimeResult<T> make_error(std::string message) {
    return std::unexpected(std::move(message));
}

template <typename T>
RuntimeResult<T> make_loc_error(const std::string& module_name, const Span& span, std::string message) {
    return std::unexpected(format_location(module_name, span) + ": " + std::move(message));
}

inline VoidResult ok_result() {
    return std::monostate{};
}

inline std::string ast_fallback_disabled_message(const std::string& context) {
    return context + " requires AST fallback bytecode. EvalAstExpr/ExecAstStmt are debug-only and disabled in this build.";
}

template <typename T>
std::string join_strings(const std::vector<T>& values, const std::string& separator) {
    std::ostringstream out;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            out << separator;
        }
        out << values[index];
    }
    return out.str();
}

#define ZEPHYR_TRY_ASSIGN(name, expr)                     \
    auto name##_result = (expr);                         \
    if (!name##_result) {                                \
        return std::unexpected(name##_result.error());   \
    }                                                    \
    auto name = std::move(name##_result.value())

#define ZEPHYR_TRY(expr)                                 \
    do {                                                 \
        auto zephyr_result = (expr);                     \
        if (!zephyr_result) {                            \
            return std::unexpected(zephyr_result.error()); \
        }                                                \
    } while (false)

template <typename T>
class ScopedVectorPush {
public:
    ScopedVectorPush(std::vector<T*>& values, T* value) : values_(values), value_(value) {
        values_.push_back(value_);
    }

    ~ScopedVectorPush() {
        if (!values_.empty() && values_.back() == value_) {
            values_.pop_back();
        }
    }

    ScopedVectorPush(const ScopedVectorPush&) = delete;
    ScopedVectorPush& operator=(const ScopedVectorPush&) = delete;

private:
    std::vector<T*>& values_;
    T* value_;
};

template <typename T>
class ScopedVectorItem {
public:
    ScopedVectorItem(std::vector<T>& values, T value) : values_(values), value_(value) {
        values_.push_back(value_);
    }

    ~ScopedVectorItem() {
        if (!values_.empty() && values_.back() == value_) {
            values_.pop_back();
        }
    }

    ScopedVectorItem(const ScopedVectorItem&) = delete;
    ScopedVectorItem& operator=(const ScopedVectorItem&) = delete;

private:
    std::vector<T>& values_;
    T value_;
};

template <typename T>
std::size_t compact_vector_storage(std::vector<T>& values) {
    const std::size_t before = values.capacity();
    if (before == values.size()) {
        return 0;
    }

    std::vector<T> compacted;
    if (!values.empty()) {
        compacted.reserve(values.size());
        for (auto& value : values) {
            compacted.push_back(std::move(value));
        }
    }
    values.swap(compacted);
    const std::size_t after = values.capacity();
    return before > after ? (before - after) : 0;
}

#ifdef DEBUG_LEAK_CHECK
struct DebugGcLeakState {
    std::size_t live_objects = 0;
    std::size_t total_allocations = 0;
    std::size_t peak_live_objects = 0;
};

inline DebugGcLeakState& debug_gc_leak_state() {
    static DebugGcLeakState state;
    return state;
}

inline void on_gc_object_created() {
    auto& state = debug_gc_leak_state();
    ++state.live_objects;
    ++state.total_allocations;
    state.peak_live_objects = std::max(state.peak_live_objects, state.live_objects);
}

inline void on_gc_object_destroyed() {
    auto& state = debug_gc_leak_state();
    if (state.live_objects > 0) {
        --state.live_objects;
    }
}
#endif

template <typename T>
const T& expect_variant(const std::variant<std::monostate, bool, std::int64_t, double, std::string, ZephyrValue::Array,
                                           std::shared_ptr<ZephyrRecord>, std::shared_ptr<ZephyrEnumValue>,
                                           std::shared_ptr<ZephyrHostObjectRef>>& storage,
                        const char* expected_name) {
    const auto* value = std::get_if<T>(&storage);
    if (value == nullptr) {
        fail(std::string("ZephyrValue is not a ") + expected_name + ".");
    }
    return *value;
}

struct Span {
    std::size_t line = 1;
    std::size_t column = 1;
};

inline std::string format_location(const std::string& module_name, const Span& span) {
    std::ostringstream out;
    out << module_name << ":" << span.line << ":" << span.column;
    return out.str();
}

constexpr const char* kSerializedEnvelopeType = "ZephyrSaveEnvelope";
constexpr const char* kSerializedNodeType = "ZephyrSaveNode";
constexpr const char* kSerializedFieldMapType = "ZephyrSaveFields";
constexpr const char* kSerializedSchemaName = "zephyr.save";
constexpr std::int64_t kSerializedSchemaVersion = 1;

inline RuntimeResult<std::int64_t> parse_integer_literal(const std::string& text, const std::string& module_name, const Span& span) {
    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [parsed, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || parsed != end) {
        return make_loc_error<std::int64_t>(module_name, span, "Invalid integer literal '" + text + "'.");
    }
    return value;
}

inline RuntimeResult<double> parse_float_literal(const std::string& text, const std::string& module_name, const Span& span) {
    errno = 0;
    char* parsed = nullptr;
    const double value = std::strtod(text.c_str(), &parsed);
    if (errno == ERANGE || parsed != text.c_str() + text.size()) {
        return make_loc_error<double>(module_name, span, "Invalid float literal '" + text + "'.");
    }
    return value;
}

enum class TokenType {
    EndOfFile,
    Identifier,
    Integer,
    Float,
    String,
    FStringStart,
    FStringEnd,
    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    Comma,
    Dot,
    DotDot,
    DotDotEqual,
    Semicolon,
    Colon,
    DoubleColon,
    Arrow,
    FatArrow,
    Plus,
    PlusEqual,
    Minus,
    MinusEqual,
    Star,
    StarEqual,
    Slash,
    SlashEqual,
    Percent,
    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Question,
    QuestionDot,
    Pipe,
    AndAnd,
    OrOr,
    KeywordFn,
    KeywordCoroutine,
    KeywordYield,
    KeywordResume,
    KeywordLet,
    KeywordMut,
    KeywordIf,
    KeywordElse,
    KeywordWhile,
    KeywordFor,
    KeywordTrait,
    KeywordImpl,
    KeywordIn,
    KeywordBreak,
    KeywordContinue,
    KeywordReturn,
    KeywordStruct,
    KeywordEnum,
    KeywordMatch,
    KeywordImport,
    KeywordExport,
    KeywordAs,
    KeywordFrom,
    KeywordTrue,
    KeywordFalse,
    KeywordNil,
    KeywordInt,
    KeywordFloat,
    KeywordBool,
    KeywordString,
    KeywordVoid,
    KeywordAny,
};

inline std::optional<TokenType> compound_assignment_binary_op(TokenType assignment_op) {
    switch (assignment_op) {
        case TokenType::PlusEqual:
            return TokenType::Plus;
        case TokenType::MinusEqual:
            return TokenType::Minus;
        case TokenType::StarEqual:
            return TokenType::Star;
        case TokenType::SlashEqual:
            return TokenType::Slash;
        default:
            return std::nullopt;
    }
}

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string lexeme;
    Span span;
};

#include "zephyr_lexer.inl"

enum class ObjectKind {
    String,
    Array,
    Environment,
    UpvalueCell,
    ScriptFunction,
    NativeFunction,
    StructType,
    StructInstance,
    EnumType,
    EnumInstance,
    Coroutine,
    ModuleNamespace,
};

inline const char* object_kind_name(ObjectKind kind) {
    switch (kind) {
        case ObjectKind::String:
            return "String";
        case ObjectKind::Array:
            return "Array";
        case ObjectKind::Environment:
            return "Environment";
        case ObjectKind::UpvalueCell:
            return "UpvalueCell";
        case ObjectKind::ScriptFunction:
            return "ScriptFunction";
        case ObjectKind::NativeFunction:
            return "NativeFunction";
        case ObjectKind::StructType:
            return "StructType";
        case ObjectKind::StructInstance:
            return "StructInstance";
        case ObjectKind::EnumType:
            return "EnumType";
        case ObjectKind::EnumInstance:
            return "EnumInstance";
        case ObjectKind::Coroutine:
            return "Coroutine";
        case ObjectKind::ModuleNamespace:
            return "ModuleNamespace";
    }
    return "Unknown";
}


#include "zephyr_types.inl"

#include "zephyr_compiler.inl"

}  // namespace zephyr
