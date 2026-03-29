# Wave O — Semacheck 2-Pass: Declaration Hoisting + Trait Impl Completeness

## Goal

Implement a 2-pass compilation strategy in `compile_module()`:

- **Pass 1**: Emit all top-level declaration opcodes first (fn, struct, enum, trait, impl) — "hoisting"
- **Pass 2**: Emit all remaining executable statements in order

This enables:
1. Forward function references at module level
2. Mutual recursion between top-level functions
3. Trait impl completeness checking at compile time (bonus)

---

## Step 0 — Read current compile_module

In `src/zephyr_compiler.inl`, read `BytecodeCompiler::compile_module()` (around line 4010) in full.

Also read `compile_stmt()` (the function that dispatches by stmt type) — it handles FunctionDecl, StructDecl, EnumDecl, TraitDecl, ImplDecl, BlockStmt, IfStmt, etc.

Understand what opcodes each declaration type emits (DefineFunction / DeclareStruct / DeclareEnum / DeclareTrait / DeclareImpl).

---

## Step 1 — Add `is_top_level_declaration()` helper

In `src/zephyr_compiler.inl`, inside `BytecodeCompiler`, add a static helper:

```cpp
static bool is_top_level_declaration(Stmt* stmt) {
    return dynamic_cast<FunctionDecl*>(stmt) != nullptr
        || dynamic_cast<StructDecl*>(stmt)   != nullptr
        || dynamic_cast<EnumDecl*>(stmt)     != nullptr
        || dynamic_cast<TraitDecl*>(stmt)    != nullptr
        || dynamic_cast<ImplDecl*>(stmt)     != nullptr;
}
```

---

## Step 2 — Modify `compile_module()` to use 2 passes

Find `compile_module()` (around line 4010). Change it from:

```cpp
std::shared_ptr<BytecodeFunction> compile_module(const std::string& name, Program* program) {
    reset_function_state(name);
    function_->global_slots_use_module_root_base = false;
    use_local_slots_ = false;
    allow_return_ = false;
    for (auto& statement : program->statements) {
        compile_stmt(statement.get());
    }
    emit_constant(BytecodeConstant{std::monostate{}}, Span{});
    emit(BytecodeOp::Return, Span{});
    function_->local_count = 0;
    optimize_bytecode(*function_);
    return function_;
}
```

To:

```cpp
std::shared_ptr<BytecodeFunction> compile_module(const std::string& name, Program* program) {
    reset_function_state(name);
    function_->global_slots_use_module_root_base = false;
    use_local_slots_ = false;
    allow_return_ = false;

    // Pass 1: hoist all top-level declarations (fn, struct, enum, trait, impl)
    // This enables forward references and mutual recursion at module level.
    for (auto& statement : program->statements) {
        if (is_top_level_declaration(statement.get())) {
            compile_stmt(statement.get());
        }
    }

    // Pass 2: compile all non-declaration statements in source order
    for (auto& statement : program->statements) {
        if (!is_top_level_declaration(statement.get())) {
            compile_stmt(statement.get());
        }
    }

    emit_constant(BytecodeConstant{std::monostate{}}, Span{});
    emit(BytecodeOp::Return, Span{});
    function_->local_count = 0;
    optimize_bytecode(*function_);
    return function_;
}
```

---

## Step 3 — Add trait impl completeness checking (Pass 1 bonus)

This is a separate analysis pass that runs BEFORE the 2-pass bytecode emission.

In `BytecodeCompiler`, add a method `check_trait_impls()`:

```cpp
// Returns error if any impl block is missing required trait methods.
// Call this before compile_module's Pass 1.
VoidResult check_trait_impls(Program* program, const std::string& module_name) {
    // Step A: collect all trait declarations → map from trait_name → set of required method names
    std::unordered_map<std::string, std::vector<std::string>> trait_methods;
    for (auto& stmt : program->statements) {
        if (auto* trait_decl = dynamic_cast<TraitDecl*>(stmt.get())) {
            std::vector<std::string> methods;
            for (auto& method : trait_decl->methods) {
                // Only count methods WITHOUT a default body (abstract methods)
                if (method.body == nullptr) {
                    methods.push_back(method.name);
                }
            }
            trait_methods[trait_decl->name] = std::move(methods);
        }
    }

    // Step B: for each impl block, verify all required methods are provided
    for (auto& stmt : program->statements) {
        if (auto* impl_decl = dynamic_cast<ImplDecl*>(stmt.get())) {
            const std::string trait_name = impl_decl->trait_name.display_name();
            auto it = trait_methods.find(trait_name);
            if (it == trait_methods.end()) continue;  // trait not in this module, skip

            // Collect method names provided by this impl
            std::unordered_set<std::string> provided;
            for (auto& method : impl_decl->methods) {
                provided.insert(method.name);
            }

            // Check each required method is provided
            for (const auto& required : it->second) {
                if (provided.find(required) == provided.end()) {
                    const std::string type_name = impl_decl->for_type.display_name();
                    return make_loc_error<std::monostate>(
                        module_name, impl_decl->span,
                        "impl of trait '" + trait_name + "' for '" + type_name +
                        "' is missing required method '" + required + "'"
                    );
                }
            }
        }
    }
    return {};
}
```

**Important**: Read the actual `TraitDecl` and `ImplDecl` AST struct definitions in `src/zephyr_parser.inl` to confirm the exact field names (`.methods`, `.body`, `.name`, `.trait_name`, `.for_type`). Adjust the code above to match the actual field names found.

---

## Step 4 — Wire check_trait_impls into compile_module

At the start of `compile_module()`, call `check_trait_impls()`. However, since `compile_module()` currently returns `std::shared_ptr<BytecodeFunction>` (not `RuntimeResult`), you need to either:

**Option A**: Change `compile_module()` signature to `RuntimeResult<std::shared_ptr<BytecodeFunction>>`.
- Then update `Runtime::compile_module_bytecode()` in `src/zephyr.cpp` to propagate the error.
- Update all callers of `compile_module_bytecode`.

**Option B**: Store the error and emit it as a compile-time diagnostic without changing the signature.
- Add an `errors_` vector to `BytecodeCompiler` and push errors there.
- Caller checks `compiler.has_errors()` after compilation.

**Option A is preferred** — it's cleaner and consistent with the existing `RuntimeResult` error propagation pattern.

Do this refactor carefully:
1. Change `BytecodeCompiler::compile_module()` → `RuntimeResult<std::shared_ptr<BytecodeFunction>>`
2. Add `ZEPHYR_TRY(check_trait_impls(program, name));` at the top of the new body
3. Change `Runtime::compile_module_bytecode()` in `src/zephyr.cpp` → `RuntimeResult<std::shared_ptr<BytecodeFunction>>`
4. In `src/zephyr_gc.inl`, find where `compile_module_bytecode` is called and propagate the error with `ZEPHYR_TRY_ASSIGN`

---

## Step 5 — Tests

**Step 5a — Unit tests for forward references**

Add to `tests/test_compiler.cpp`:

```cpp
void test_forward_reference() {
    // main() calls greet() which is defined AFTER main in source order
    // With 2-pass hoisting, this should work
    const char* src = R"(
        fn main() -> string {
            return greet("world");
        }
        fn greet(name: string) -> string {
            return "hello " + name;
        }
        main()
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_string()->value, "hello world");
}

void test_mutual_recursion() {
    // is_even calls is_odd and vice versa — mutual recursion
    const char* src = R"(
        fn is_even(n: int) -> bool {
            if n == 0 { return true; }
            return is_odd(n - 1);
        }
        fn is_odd(n: int) -> bool {
            if n == 0 { return false; }
            return is_even(n - 1);
        }
        is_even(4)
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().as_bool());
}
```

**Step 5b — Unit test for trait impl completeness check**

```cpp
void test_trait_impl_missing_method() {
    // impl is missing a required method — should fail at compile time
    const char* src = R"(
        trait Greeter {
            fn greet(self) -> string;
            fn farewell(self) -> string;
        }
        struct Bot { name: string }
        impl Greeter for Bot {
            fn greet(self) -> string { return "hi"; }
            // farewell is missing
        }
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_error());
    // Error message should mention 'farewell'
}
```

Declare these in `tests/test_common.hpp` and register in `tests/test_main.cpp`.

---

## Step 6 — Corpus file `tests/corpus/10_forward_refs.zph`

```zephyr
// 10_forward_refs.zph — Forward reference and mutual recursion test

fn run_all() {
    assert(greet("Zephyr") == "hello Zephyr");
    assert(is_even(6) == true);
    assert(is_odd(7) == true);
    assert(fib(10) == 55);
}

fn greet(name: string) -> string {
    return "hello " + name;
}

fn is_even(n: int) -> bool {
    if n == 0 { return true; }
    return is_odd(n - 1);
}

fn is_odd(n: int) -> bool {
    if n == 0 { return false; }
    return is_even(n - 1);
}

fn fib(n: int) -> int {
    if n <= 1 { return n; }
    return fib(n - 1) + fib(n - 2);
}

run_all();
```

---

## Step 7 — Build and test

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  "C:\Users\lance\OneDrive\Documents\Project Zephyr\Zephyr.sln" `
  /p:Configuration=Release /p:Platform=x64 /v:minimal 2>&1 | `
  Select-String -Pattern "error C|error LNK|Build succeeded|FAILED" | Select-Object -First 20
```

```powershell
& "C:\Users\lance\OneDrive\Documents\Project Zephyr\x64\Release\zephyr_tests.exe" 2>&1
```

---

## Step 8 — Commit

```powershell
cd "C:\Users\lance\OneDrive\Documents\Project Zephyr"
git add src/zephyr_compiler.inl src/zephyr.cpp src/zephyr_gc.inl `
        tests/test_compiler.cpp tests/test_common.hpp tests/test_main.cpp `
        tests/corpus/10_forward_refs.zph
git commit -m "feat: 2-pass semacheck — declaration hoisting + trait impl completeness (Wave O)

- compile_module() now does Pass 1 (hoist fn/struct/enum/trait/impl) then Pass 2 (executable stmts)
- Forward references and mutual recursion between top-level functions now work
- check_trait_impls() validates impl completeness at compile time
- compile_module_bytecode() now returns RuntimeResult for error propagation
- Tests: test_forward_reference, test_mutual_recursion, test_trait_impl_missing_method
- Corpus: 10_forward_refs.zph"
```
