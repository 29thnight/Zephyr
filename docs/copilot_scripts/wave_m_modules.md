# Wave M — Module System Enhancement

## Context

The module infrastructure already exists:
- `import "path" as alias;` → `ImportModule` opcode
- `export` keyword marks declarations exported (sets `stmt->exported = true`)
- `ModuleNamespaceObject` with `exports` vector
- `ModuleRecord` with `namespace_object`
- `execute_file_module()`, `execute_string_module()` in VM
- `add_module_search_path()` API

## Goals

### Part A — Named/Selective Imports
Add syntax: `import { foo, bar, baz as qux } from "path";`

Currently only `import "path" as alias;` works (whole namespace).
We need destructuring import that binds individual names.

**Step A1 — Parser: `ImportStmt` extension**

In `src/zephyr_parser.inl`, find `struct ImportStmt`:
```cpp
struct ImportStmt final : Stmt {
    std::string path;
    std::optional<std::string> alias;
```

Add a named imports field:
```cpp
struct ImportNameItem {
    std::string name;          // exported name
    std::string local_name;    // local binding (may differ via 'as')
};

struct ImportStmt final : Stmt {
    std::string path;
    std::optional<std::string> alias;        // import "p" as alias
    std::vector<ImportNameItem> named;       // import { foo, bar as b } from "p"
};
```

**Step A2 — Parser: `parse_import()`**

Current grammar: `import STRING (as IDENT)? ;`

New grammar:
```
import_stmt ::= 'import' STRING ('as' IDENT)? ';'
              | 'import' '{' import_list '}' 'from' STRING ';'

import_list ::= import_item (',' import_item)* ','?
import_item ::= IDENT ('as' IDENT)?
```

In `Parser::parse_import()` in `src/zephyr_parser.inl`:
- If next token is `{` → parse `import_list`, consume `from`, consume STRING path, consume `;`
- Otherwise keep existing `import STRING (as IDENT)? ;` path

Add `KeywordFrom` token if not present. Check `src/zephyr_lexer.inl` keywords map — if `"from"` is not there, add:
```cpp
{"from", TokenType::KeywordFrom},
```
And add `KeywordFrom` to the `TokenType` enum in `src/zephyr_lexer.inl`.

**Step A3 — Compiler: compile named imports**

In `src/zephyr_compiler.inl`, find where `ImportStmt` is compiled (look for `ImportModule` opcode emission).

After emitting `ImportModule` (which loads the module namespace into a temp), for each `named` item:
- Emit a `GetProperty` or equivalent opcode to fetch `item.name` from the namespace
- Bind the result to `item.local_name` in the current environment

If `named` is empty and `alias` is set → existing behavior (bind whole namespace to alias).
If `named` is non-empty → bind each name individually, don't bind the namespace itself.

---

### Part B — Re-exports

Add syntax: `export { foo, bar };` and `export { foo as bar } from "path";`

**Step B1 — Parser**

In `parse_statement()`, after `match(KeywordExport)`:
- If next token is `{` → parse a re-export list
  - Consume `{`, parse list of `IDENT ('as' IDENT)?`, consume `}`
  - Optionally consume `from STRING`
  - Consume `;`
  - Return a new `ReExportStmt` or extend `ImportStmt` with a `re_export` flag

Add `struct ReExportStmt`:
```cpp
struct ReExportItem {
    std::string name;
    std::string exported_as;  // same as name if no 'as'
};

struct ReExportStmt final : Stmt {
    std::string path;         // empty if not 're-export from'
    std::vector<ReExportItem> items;
};
```

**Step B2 — Compiler: compile re-exports**

For `ReExportStmt`:
- If `path` is non-empty: emit `ImportModule` for path (anonymous), then for each item emit `GetProperty` + `ExportName`
- If `path` is empty: for each item, emit `LoadVar(name)` + `ExportName(exported_as)`

---

### Part C — Circular Dependency Detection

In `src/zephyr_gc.inl` (VM execution), find where `ImportModule` opcode is handled at runtime (search for `case BytecodeOp::ImportModule`).

The VM likely calls `execute_file_module()`. Add a "currently loading" set:

1. In the `ZephyrVM` class (or equivalent), add:
   ```cpp
   std::unordered_set<std::string> modules_loading_;  // cycle detection
   ```

2. In the import handler, before loading:
   ```cpp
   if (modules_loading_.count(canonical_path)) {
       return make_error("Circular import: " + canonical_path);
   }
   modules_loading_.insert(canonical_path);
   // ... load module ...
   modules_loading_.erase(canonical_path);
   ```

3. Already-loaded modules (in `ModuleRecord` cache) skip the cycle check — they're already resolved.

---

### Part D — Built-in Standard Library Modules

Add two built-in host modules: `std/math` and `std/string`.

**Step D1 — Register in `install_core()` or equivalent**

In `src/zephyr_gc.inl`, find where host modules are registered (look for `register_module` or `HostModuleRecord`). Add:

```cpp
// std/math
vm.register_module("std/math", [](ZephyrModuleBinder& m) {
    m.def("pi",    Value::from_float(3.14159265358979323846));
    m.def("e",     Value::from_float(2.71828182845904523536));
    m.def("floor", [](Value x) { return Value::from_float(std::floor(x.as_float())); });
    m.def("ceil",  [](Value x) { return Value::from_float(std::ceil(x.as_float())); });
    m.def("sqrt",  [](Value x) { return Value::from_float(std::sqrt(x.as_float())); });
    m.def("abs",   [](Value x) {
        if (x.is_int()) return Value::from_int(std::abs(x.as_int()));
        return Value::from_float(std::abs(x.as_float()));
    });
    m.def("min",   [](Value a, Value b) { return (a.as_float() < b.as_float()) ? a : b; });
    m.def("max",   [](Value a, Value b) { return (a.as_float() > b.as_float()) ? a : b; });
    m.def("pow",   [](Value base, Value exp) { return Value::from_float(std::pow(base.as_float(), exp.as_float())); });
    m.def("sin",   [](Value x) { return Value::from_float(std::sin(x.as_float())); });
    m.def("cos",   [](Value x) { return Value::from_float(std::cos(x.as_float())); });
    m.def("log",   [](Value x) { return Value::from_float(std::log(x.as_float())); });
    m.def("round", [](Value x) { return Value::from_float(std::round(x.as_float())); });
    m.def("clamp", [](Value v, Value lo, Value hi) {
        double val = v.as_float(), l = lo.as_float(), h = hi.as_float();
        return Value::from_float(val < l ? l : val > h ? h : val);
    });
});

// std/string
vm.register_module("std/string", [](ZephyrModuleBinder& m) {
    m.def("len",      [](Value s) { return Value::from_int((int64_t)s.as_string()->value.size()); });
    m.def("upper",    [](Value s) {
        std::string r = s.as_string()->value;
        std::transform(r.begin(), r.end(), r.begin(), ::toupper);
        return /* create StringObject */;
    });
    m.def("lower",    [](Value s) {
        std::string r = s.as_string()->value;
        std::transform(r.begin(), r.end(), r.begin(), ::tolower);
        return /* create StringObject */;
    });
    m.def("trim",     [](Value s) { /* strip leading/trailing whitespace */ });
    m.def("starts_with", [](Value s, Value prefix) {
        return Value::from_bool(s.as_string()->value.starts_with(prefix.as_string()->value));
    });
    m.def("ends_with", [](Value s, Value suffix) {
        return Value::from_bool(s.as_string()->value.ends_with(suffix.as_string()->value));
    });
    m.def("contains", [](Value s, Value sub) {
        return Value::from_bool(s.as_string()->value.find(sub.as_string()->value) != std::string::npos);
    });
    m.def("split",    [](Value s, Value sep) { /* split into array */ });
    m.def("join",     [](Value arr, Value sep) { /* join array with separator */ });
    m.def("replace",  [](Value s, Value from, Value to) { /* replace all */ });
    m.def("substr",   [](Value s, Value start, Value len) { /* substring */ });
    m.def("to_int",   [](Value s) { /* parse int, return Result */ });
    m.def("to_float", [](Value s) { /* parse float, return Result */ });
});
```

**Implementation notes:**
- Use the existing `ZephyrModuleBinder::def()` patterns already in the codebase
- For string functions that return strings, use the existing helper to allocate `StringObject` (look for how other string operations allocate strings in the VM)
- For functions returning arrays (`split`), use the existing array allocation pattern
- For `to_int`/`to_float`, return `Ok(n)` or `Err("parse error")` using the Result enum

**Step D2 — Where to register**

Find in `src/zephyr_gc.inl` or `src/zephyr.cpp` where `install_core()` or the initial module registration happens. Add both module registrations there.

---

### Part E — Tests

**Step E1 — Unit tests in `tests/test_compiler.cpp`**

Add these test functions (declare in `tests/test_common.hpp`, register in `tests/test_main.cpp`):

```cpp
void test_named_import() {
    // create two modules: lib exporting foo=42, main importing { foo }
    // verify foo is accessible in main scope
}

void test_re_export() {
    // lib exports foo; middle re-exports foo; main imports { foo } from middle
}

void test_circular_import_error() {
    // a imports b, b imports a → should produce an error, not infinite loop
}

void test_std_math() {
    // import { floor, sqrt, pi } from "std/math"
    // assert floor(3.7) == 3.0
    // assert sqrt(4.0) == 2.0
    // assert pi > 3.14
}

void test_std_string() {
    // import { upper, contains, split } from "std/string"
    // assert upper("hello") == "HELLO"
    // assert contains("hello world", "world") == true
}
```

**Step E2 — Corpus file `tests/corpus/09_modules.zph`**

```zephyr
// 09_modules.zph - Module system test
// This file tests named imports and std modules.
// Run as: the test harness compiles and executes this, asserting no errors.

import { floor, sqrt, clamp, pi } from "std/math";
import { upper, contains, len } from "std/string";

fn test_math() {
    let x = floor(3.9);
    assert(x == 3.0);
    let s = sqrt(9.0);
    assert(s == 3.0);
    let c = clamp(10.0, 0.0, 5.0);
    assert(c == 5.0);
    assert(pi > 3.0);
}

fn test_strings() {
    let s = "Hello World";
    assert(upper(s) == "HELLO WORLD");
    assert(contains(s, "World") == true);
    assert(len(s) == 11);
}

test_math();
test_strings();
```

---

## Step 0 — Read and understand existing code

Before starting:
1. Read `src/zephyr_parser.inl` around line 184 (`parse_import`) to understand current import parsing
2. Read `src/zephyr_compiler.inl` around `case BytecodeOp::ImportModule` to understand how imports are compiled
3. Read `src/zephyr_gc.inl` around where `ImportModule` opcode is executed at runtime
4. Read `include/zephyr/api.hpp` around `ZephyrModuleBinder` to understand the module registration API
5. Read existing host module registrations (search for `register_module` calls) to understand the pattern

Then implement Part A, B, C, D, E in order. Build and test after each part.

## Build command

```
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' "C:\Users\lance\OneDrive\Documents\Project Zephyr\Zephyr.sln" /p:Configuration=Release /p:Platform=x64 /v:minimal 2>&1 | Select-String -Pattern "error C|error LNK|Build succeeded|FAILED" | Select-Object -First 20
```

## Test command

```
& "C:\Users\lance\OneDrive\Documents\Project Zephyr\x64\Release\zephyr_tests.exe" 2>&1
```

## Commit

After all tests pass:
```
git add src/zephyr_lexer.inl src/zephyr_parser.inl src/zephyr_compiler.inl src/zephyr_gc.inl src/zephyr.cpp tests/test_compiler.cpp tests/test_common.hpp tests/test_main.cpp tests/corpus/09_modules.zph
git commit -m "feat: module system — named imports, re-exports, circular detection, std/math, std/string (Wave M)"
```
