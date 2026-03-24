# Wave E1 — Language Feature Extensions

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
Project Zephyr C++20 game scripting VM. Wave D complete + hot_arithmetic optimized.
Key files: src/zephyr_lexer.inl (Lexer/TokenType), src/zephyr_parser.inl (AST/Parser),
src/zephyr_compiler.inl (BytecodeCompiler), src/zephyr_gc.inl (VM dispatch),
include/zephyr/api.hpp (public API), tests/tests.cpp, docs/process.md.

Implement all 4 Wave E1 tasks in order.

---

## Task 5.1 — String Interpolation f"..."

### Goal
Support f"text {expr} text" syntax that evaluates expressions inside braces and concatenates.

### Steps
1. Read src/zephyr_lexer.inl — find TokenType enum and Lexer::next_token() / scan logic.
2. Add TokenType::FStringStart (or reuse existing string scanning) to detect f"..." prefix.
3. Lex f"text {expr} more" into segments: FStringStart, literal segments as StringLiteral tokens, expressions between { } as normal token streams, FStringEnd.
   Simple approach: when lexer sees f", switch to f-string mode. Emit literal segments as StringLiteral, emit LBRACE to signal expression start, lex normally until matching RBRACE, repeat.
4. Read src/zephyr_parser.inl — find string literal parsing and expression parsing.
5. Add InterpolatedStringExpr AST node: vector of segments (either string literal or expression).
6. Parse f-string token sequence into InterpolatedStringExpr.
7. Read src/zephyr_compiler.inl — find how string literals and binary concat are compiled.
8. Compile InterpolatedStringExpr: for each segment emit LoadConst(string) or compile the expression, then emit Add/concat between adjacent segments.
9. Build, test. Add test cases: f"hello {name}", f"{a + b} result", nested f"{'x'}".

---

## Task 5.2 — Optional Chaining ?.

### Goal
Support obj?.field and obj?.method() that short-circuit to nil if obj is nil.

### Steps
1. Read src/zephyr_lexer.inl — find TokenType enum.
2. Add TokenType::QuestionDot. In lexer, when seeing '?' followed by '.', emit QuestionDot.
3. Read src/zephyr_parser.inl — find member access and method call parsing (LoadMember, CallMethod).
4. Add OptionalMemberExpr and OptionalCallExpr AST nodes (or add an is_optional flag to existing nodes).
5. In the parser, after parsing a primary expression, check for '?.' and parse as optional chain.
6. Read src/zephyr_compiler.inl and src/zephyr_gc.inl — find BytecodeOp list.
7. Add BytecodeOp::JumpIfNilKeep — peeks top of stack, if nil jumps to target (does NOT pop, keeps nil on stack).
8. Compile OptionalMemberExpr: emit object, emit JumpIfNilKeep(end), emit LoadMember, label(end).
9. Build, test. Add test cases: nil?.field == nil, obj?.field works normally, chained a?.b?.c.

---

## Task 6.1 — Template-based Auto Binding ZephyrClassBinder

### Goal
Add ZephyrClassBinder<T> template to include/zephyr/api.hpp for ergonomic C++ class binding.

### Steps
1. Read include/zephyr/api.hpp — find existing host binding API (register_function, register_type, etc.).
2. Add to api.hpp:

template<typename T>
class ZephyrClassBinder {
public:
    ZephyrClassBinder(ZephyrVM& vm, std::string_view class_name);

    template<typename Ret, typename... Args>
    ZephyrClassBinder& method(std::string_view name, Ret (T::*fn)(Args...));

    template<typename Ret, typename... Args>
    ZephyrClassBinder& method(std::string_view name, Ret (T::*fn)(Args...) const);

    // Property getter/setter
    template<typename Field>
    ZephyrClassBinder& prop(std::string_view name, Field T::*member);
};

3. Implement using C++20 concepts where helpful. Use existing register_function internals to wrap member function pointers via lambda captures.
4. Usage example (add as comment in api.hpp):
   vm.bind<Player>("Player").method("damage", &Player::damage).prop("hp", &Player::hp);
5. Add bind<T>() factory method to ZephyrVM class.
6. Build, test. Add a simple compile-time test in tests.cpp that instantiates ZephyrClassBinder.

---

## Task 6.2 — Profiler API

### Goal
Add ZephyrVM::start_profiling() / stop_profiling() returning ZephyrProfileReport.

### Steps
1. Read include/zephyr/api.hpp — find ZephyrVM public interface.
2. Add to api.hpp:

struct ZephyrProfileEntry {
    std::string function_name;
    uint64_t call_count = 0;
    uint64_t total_ns = 0;    // inclusive time
    uint64_t self_ns = 0;     // exclusive time
};

struct ZephyrProfileReport {
    std::vector<ZephyrProfileEntry> entries;
    // sorted by self_ns descending by default
};

class ZephyrVM {
    // existing ...
    void start_profiling();
    ZephyrProfileReport stop_profiling();
};

3. In src/zephyr_gc.inl (VM dispatch), add profiling hooks: on function entry/exit record timestamp when profiling is active. Use a flag to avoid overhead when profiling is off.
4. Implement call stack tracking for self_ns calculation.
5. Build, test. Add test: start_profiling, run fib(10), stop_profiling, verify entries non-empty.

---

## Build & Verify after all 4 tasks

1. msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /m
2. x64\Release\zephyr_tests.exe
3. x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
4. Update docs/process.md — mark 5.1, 5.2, 6.1, 6.2 all as complete.
