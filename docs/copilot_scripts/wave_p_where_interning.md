# Wave P — Trait Where Clauses (P-1) + String Interning Activation (P-2)

---

## Part A — String Interning Activation (P-2)

### Background

The interning infrastructure is already fully implemented:
- `make_literal_string(value)` — interns strings ≤ 64 chars via `intern_string()`
- `make_string(value)` — raw alloc, NO interning
- `is_interned` field on `StringObject`
- `interned_strings_` map, `string_intern_hits_/misses_` counters

The problem: string literal constants are likely emitted using `make_string` instead of `make_literal_string`.

### Step A1 — Find all string literal emission sites

In `src/zephyr_gc.inl` and `src/zephyr_compiler.inl`, search for:
- `BytecodeOp::LoadConst` or similar opcode that loads string constants at runtime
- The runtime handler where `BytecodeConstant` string values are pushed onto the stack
- Any call sites that load string literal constants from the bytecode

Specifically look for the opcode dispatch loop in `src/zephyr_gc.inl` where string constants are loaded (search for `BytecodeConstant` + `String` or similar). When a string constant is loaded at runtime, it should use `make_literal_string` (with interning), not `make_string`.

### Step A2 — Switch string constant loading to make_literal_string

Find the runtime handler for loading string constants from bytecode (it will look something like):
```cpp
case BytecodeOp::LoadConst: {
    const auto& constant = ...;
    if (std::holds_alternative<std::string>(constant)) {
        stack.push_back(make_string(std::get<std::string>(constant)));  // ← change this
    }
}
```

Change `make_string(...)` → `make_literal_string(...)` for all string constant loads.

Do this in BOTH the stack-based dispatch loop AND the register-based dispatch loop (if they exist separately).

### Step A3 — Intern field/property name strings

In the runtime, struct field access uses string names for property lookup. Find where field names are used as strings during struct creation or field access (e.g., `DeclareStruct` opcode handler, `SetProperty`/`GetProperty` handlers). If they allocate `StringObject` for field names, switch to `intern_string()`.

Search for: `make_string` calls in `DeclareStruct`, `DeclareEnum`, `GetProperty`, `SetProperty` opcode handlers.

### Step A4 — Expose intern stats in ZephyrStats

In `include/zephyr/api.hpp`, find `ZephyrStats` or equivalent struct. Add:
```cpp
std::size_t string_intern_hits = 0;
std::size_t string_intern_misses = 0;
std::size_t interned_string_count = 0;
```

In the stats collection (wherever `ZephyrStats` is populated), add:
```cpp
stats.string_intern_hits = string_intern_hits_;
stats.string_intern_misses = string_intern_misses_;
stats.interned_string_count = interned_strings_.size();
```

---

## Part B — Trait Where Clauses (P-1)

### Background

Current generics: `fn identity<T>(x: T) -> T` — T has no constraints.
Goal: `fn add<T>(a: T, b: T) -> T where T: Numeric` — T must implement `Numeric` trait.

Since Zephyr uses type erasure (all T → Value at runtime), bounds are checked:
- **At compile time**: verify impl blocks exist for concrete type arguments (best effort, when types are known)
- **At call site**: if the concrete type is known statically, emit a compile error if bound not satisfied

### Step B1 — Add TraitBound AST types

In `src/zephyr_parser.inl`, add before the `FunctionDecl` struct:
```cpp
struct TraitBound {
    std::string type_param;            // e.g., "T"
    std::vector<std::string> traits;   // e.g., {"Display", "Clone"}
};
```

### Step B2 — Add where_clauses to declaration AST nodes

Find `struct FunctionDecl`, `struct StructDecl`, `struct TraitDecl` in `src/zephyr_parser.inl`.
Add to each:
```cpp
std::vector<TraitBound> where_clauses;
```

### Step B3 — Add `KeywordWhere` token

In `src/zephyr_lexer.inl`:
1. Add `KeywordWhere` to the `TokenType` enum
2. Add `{"where", TokenType::KeywordWhere}` to the keywords map
3. Add debug string case for `KeywordWhere`

### Step B4 — Add parse_where_clause() to Parser

In `src/zephyr_parser.inl`, add:
```cpp
VoidResult parse_where_clause(std::vector<TraitBound>& out_bounds);
```

Implementation:
```cpp
VoidResult Parser::parse_where_clause(std::vector<TraitBound>& out_bounds) {
    if (!match({TokenType::KeywordWhere})) return {};  // where clause is optional

    do {
        TraitBound bound;
        // Parse: IDENT ':' TypeRef ('+' TypeRef)*
        ZEPHYR_TRY_ASSIGN(param_token, consume(TokenType::Identifier, "Expected type parameter name in where clause."));
        bound.type_param = param_token.lexeme;
        ZEPHYR_TRY(consume(TokenType::Colon, "Expected ':' after type parameter in where clause."));

        // Parse one or more trait names separated by '+'
        ZEPHYR_TRY_ASSIGN(first_trait, parse_type_ref());
        bound.traits.push_back(first_trait.display_name());
        while (match({TokenType::Plus})) {
            ZEPHYR_TRY_ASSIGN(next_trait, parse_type_ref());
            bound.traits.push_back(next_trait.display_name());
        }
        out_bounds.push_back(std::move(bound));
    } while (match({TokenType::Comma}));

    return {};
}
```

**Note**: Check if `TokenType::Plus` exists or if addition uses a different token name. Check the TokenType enum for the `+` operator token name (might be `TokenType::Plus` or `TokenType::Add`). Use whatever the existing token is.

**Note**: Check if `TypeRef` has a `display_name()` method. If not, use `parts.front()` or `parts[0]` to get the trait name string.

### Step B5 — Call parse_where_clause() in parse_function_decl()

In `Parser::parse_function_decl()` (around line 273), after `parse_function_signature()` but BEFORE `parse_block_stmt()`:

```cpp
ZEPHYR_TRY(parse_generic_type_params(function->generic_params));
ZEPHYR_TRY(parse_function_signature(function->params, function->return_type));
ZEPHYR_TRY(parse_where_clause(function->where_clauses));   // ← add this
ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected function body."));
```

For trait method declarations (inside `parse_trait_decl`), also add `parse_where_clause` support after the method signature.

### Step B6 — Pass where_clauses through compiler

In `src/zephyr_compiler.inl`:

**B6a**: Find `struct FunctionDecl` as seen by the compiler (it's in `src/zephyr_parser.inl` included by `src/zephyr.cpp`). The `where_clauses` field will be available automatically since the same struct is used.

**B6b**: In `compile_nested_function()` or wherever `FunctionDecl` is compiled, pass `where_clauses` alongside `generic_params`.

**B6c**: Add `where_clauses` to `ScriptFunctionObject`:
```cpp
std::vector<TraitBound> where_clauses;  // trait bounds from where clause
```

**B6d**: In `check_trait_impls()` (added in Wave O), extend to also validate where clause bounds when a generic function is called with known concrete types.

For now, the simplest approach: when compiling a generic function call where we can resolve the concrete type argument (e.g., `identity<int>(42)` — type arg is `int`), check if `int` satisfies all trait bounds in the where clause. Since primitives don't have trait impls, either skip primitive types or treat them as always satisfying bounds for now.

**B6e**: Add a runtime check in the function call opcode handler: when `CallGeneric` or a function call with type arguments is executed, verify that the concrete `Value` satisfies the trait bounds by checking if `impl TraitName for ConcreteType` exists in the environment. If not, produce a `RuntimeError`.

The runtime check logic (add to the function call handler in `src/zephyr_gc.inl`):
```cpp
// For each where_clause bound on the called function:
// Check if the concrete type (determined from the argument value) has an impl for each required trait
for (const auto& bound : function->where_clauses) {
    // Find the argument index corresponding to bound.type_param
    // Get the concrete type name of that argument value
    // For each trait in bound.traits:
    //   Look up impl_table_[concrete_type][trait_name]
    //   If not found, return RuntimeError: "type X does not implement trait Y"
}
```

Note: Search for how `impl` dispatch works in the VM — look for `impl_table_` or `trait_impls_` or similar. Use the same lookup mechanism.

### Step B7 — Tests

Add to `tests/test_compiler.cpp`:

```cpp
void test_where_clause_basic() {
    // Define a trait and use it as a where clause bound
    const char* src = R"(
        trait Describable {
            fn describe(self) -> string;
        }
        struct Dog { name: string }
        impl Describable for Dog {
            fn describe(self) -> string { return "Dog: " + self.name; }
        }
        fn describe_it<T>(x: T) -> string where T: Describable {
            return x.describe();
        }
        let d = Dog { name: "Rex" };
        describe_it(d)
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_string()->value, "Dog: Rex");
}

void test_where_multiple_bounds() {
    // T must implement two traits
    const char* src = R"(
        trait Named { fn name(self) -> string; }
        trait Aged  { fn age(self) -> int; }
        struct Person { n: string, a: int }
        impl Named for Person { fn name(self) -> string { return self.n; } }
        impl Aged  for Person { fn age(self)  -> int    { return self.a; } }
        fn bio<T>(x: T) -> string where T: Named + Aged {
            return x.name() + " age " + x.age();
        }
        let p = Person { n: "Alice", a: 30 };
        bio(p)
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
}

void test_where_bound_violation() {
    // Call generic fn with type that doesn't implement required trait
    // Should produce a runtime error
    const char* src = R"(
        trait Flyable { fn fly(self); }
        fn launch<T>(x: T) where T: Flyable { x.fly(); }
        struct Rock {}
        let r = Rock {};
        launch(r)
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_error());
}
```

Declare in `tests/test_common.hpp` and register in `tests/test_main.cpp`.

### Step B8 — Corpus file `tests/corpus/11_where_clauses.zph`

```zephyr
// 11_where_clauses.zph — Trait where clause bounds test

trait Printable {
    fn to_str(self) -> string;
}

struct Point { x: int, y: int }
struct Circle { radius: int }

impl Printable for Point {
    fn to_str(self) -> string {
        return "(" + self.x + ", " + self.y + ")";
    }
}

impl Printable for Circle {
    fn to_str(self) -> string {
        return "Circle(r=" + self.radius + ")";
    }
}

fn print_it<T>(val: T) -> string where T: Printable {
    return val.to_str();
}

fn test_where() {
    let p = Point { x: 3, y: 4 };
    let c = Circle { radius: 5 };
    assert(print_it(p) == "(3, 4)");
    assert(print_it(c) == "Circle(r=5)");
}

test_where();
```

---

## Step 0 — Read before starting

1. Read `src/zephyr_parser.inl` lines around `parse_generic_type_params` and `parse_function_decl` to understand the exact current structure.
2. Read `src/zephyr_gc.inl` around `make_string` / `make_literal_string` / `BytecodeOp::LoadConst` handling to find string literal emission sites.
3. Read `src/zephyr_gc.inl` around `DeclareStruct` / `GetProperty` / `SetProperty` opcode handlers to find field name string allocations.

Start with Part A (P-2, simpler), then Part B (P-1).

---

## Build and test

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  "C:\Users\lance\OneDrive\Documents\Project Zephyr\Zephyr.sln" `
  /p:Configuration=Release /p:Platform=x64 /v:minimal 2>&1 | `
  Select-String -Pattern "error C|error LNK|Build succeeded|FAILED" | Select-Object -First 20

& "C:\Users\lance\OneDrive\Documents\Project Zephyr\x64\Release\zephyr_tests.exe" 2>&1
```

---

## Commit

```powershell
cd "C:\Users\lance\OneDrive\Documents\Project Zephyr"
git add src/zephyr_lexer.inl src/zephyr_parser.inl src/zephyr_compiler.inl src/zephyr_gc.inl `
        include/zephyr/api.hpp `
        tests/test_compiler.cpp tests/test_common.hpp tests/test_main.cpp `
        tests/corpus/11_where_clauses.zph
git commit -m "feat: trait where clauses + string interning activation (Wave P)

P-1 (where clauses):
- Add TraitBound AST struct with type_param + traits vector
- Add KeywordWhere token, parse_where_clause() parser
- FunctionDecl/StructDecl gain where_clauses field
- Runtime bound check: verify impl exists for concrete type at call site

P-2 (string interning):
- String literal constants load via make_literal_string (intern <=64 chars)
- Field/property name strings interned at DeclareStruct/GetProperty/SetProperty
- Expose string_intern_hits/misses/count in ZephyrStats

Tests: test_where_clause_basic, test_where_multiple_bounds, test_where_bound_violation
Corpus: 11_where_clauses.zph"
```
