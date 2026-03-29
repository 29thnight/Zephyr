# Wave S — Better Error Messages (T-1) + Iterator Protocol (S-3)

---

## Part A — Better Error Messages (T-1)

### Background

Current error output:
```
script.zph:3:5: undefined variable 'greeet'
```

Target output:
```
error[E001] at script.zph:3:5
  3 |     greeet("hello");
    |     ^^^^^^ undefined variable 'greeet'
hint: did you mean 'greet'?
```

### Step A0 — Read existing error infrastructure

1. Read `src/zephyr_internal.hpp` around `struct Span`, `format_location()`, `make_loc_error()` (around line 213-230).
2. Read `include/zephyr/api.hpp` for `ZephyrRuntimeError` — how errors are surfaced to the host. Check if it has a `message` field and how it's formatted when displayed.
3. Read `src/zephyr_gc.inl` for where undefined variable errors are produced — search for `"undefined variable"` or `"undefined"` strings to find the error emission sites.
4. Grep the codebase for the most common error message patterns (undefined variable, type mismatch, etc.).

### Step A1 — Store source text per module

In the `ModuleRecord` struct (in `src/zephyr_compiler.inl`), add:
```cpp
std::string source_text;   // original source for error display
```

When a module is loaded (in the `load_file_record` or `load_virtual_record` functions in `src/zephyr_gc.inl`), store the source text in the `ModuleRecord`.

### Step A2 — Add source-context extraction helper

In `src/zephyr_internal.hpp`, add after `format_location()`:

```cpp
// Extract the source line and build a caret pointer for error display.
// Returns a 3-line string: "  N | source_line\n    | ^^^^^\n"
inline std::string format_source_context(const std::string& source_text, const Span& span,
                                          std::size_t highlight_len = 1) {
    if (source_text.empty()) return {};

    // Find the start of the line
    std::size_t pos = 0;
    std::size_t current_line = 1;
    while (pos < source_text.size() && current_line < span.line) {
        if (source_text[pos] == '\n') ++current_line;
        ++pos;
    }

    // Find end of the line
    std::size_t line_start = pos;
    std::size_t line_end = pos;
    while (line_end < source_text.size() && source_text[line_end] != '\n') ++line_end;

    std::string source_line = source_text.substr(line_start, line_end - line_start);

    // Build output
    std::ostringstream out;
    const std::string line_num = std::to_string(span.line);
    const std::string indent(line_num.size(), ' ');

    out << "  " << line_num << " | " << source_line << "\n";
    out << "  " << indent << " | ";

    // Add spaces up to the column
    const std::size_t col = (span.column > 0) ? span.column - 1 : 0;
    for (std::size_t i = 0; i < col && i < source_line.size(); ++i) {
        out << (source_line[i] == '\t' ? '\t' : ' ');
    }

    // Add carets
    const std::size_t caret_len = std::min(highlight_len, source_line.size() > col ? source_line.size() - col : 1ul);
    for (std::size_t i = 0; i < std::max(caret_len, std::size_t(1)); ++i) out << '^';
    out << "\n";

    return out.str();
}
```

### Step A3 — Add "did you mean?" suggestion helper

In `src/zephyr_internal.hpp`, add:

```cpp
// Returns the closest name from `candidates` to `name` if within edit distance 2.
// Uses simple Levenshtein distance.
inline std::optional<std::string> suggest_similar_name(
    const std::string& name,
    const std::vector<std::string>& candidates)
{
    auto edit_distance = [](const std::string& a, const std::string& b) -> std::size_t {
        const std::size_t m = a.size(), n = b.size();
        std::vector<std::vector<std::size_t>> dp(m + 1, std::vector<std::size_t>(n + 1));
        for (std::size_t i = 0; i <= m; ++i) dp[i][0] = i;
        for (std::size_t j = 0; j <= n; ++j) dp[0][j] = j;
        for (std::size_t i = 1; i <= m; ++i)
            for (std::size_t j = 1; j <= n; ++j)
                dp[i][j] = (a[i-1] == b[j-1]) ? dp[i-1][j-1]
                          : 1 + std::min({dp[i-1][j], dp[i][j-1], dp[i-1][j-1]});
        return dp[m][n];
    };

    std::optional<std::string> best;
    std::size_t best_dist = 3;  // only suggest if distance <= 2
    for (const auto& candidate : candidates) {
        if (candidate == name) continue;
        const auto dist = edit_distance(name, candidate);
        if (dist < best_dist) {
            best_dist = dist;
            best = candidate;
        }
    }
    return best;
}
```

### Step A4 — Improve "undefined variable" error messages

In `src/zephyr_gc.inl`, find where "undefined variable" errors are produced. There will be a pattern like:
```cpp
return make_loc_error<Value>(module_name, span, "undefined variable '" + name + "'");
```

Change it to also:
1. Collect all variable names currently in scope (from the environment chain)
2. Call `suggest_similar_name(name, all_names)`
3. If a suggestion is found, append `"\nhint: did you mean '" + *suggestion + "'?"` to the error message

To collect names from the environment, add a helper to `Environment`:
```cpp
void collect_names(std::vector<std::string>& out) const {
    for (const auto& [name, _] : bindings) {
        out.push_back(name);
    }
    if (parent) parent->collect_names(out);
}
```

Search for the `Environment` struct definition (it has `bindings` and `parent` fields) and add this method.

### Step A5 — Improve make_loc_error to include source context

In `make_loc_error()` (`src/zephyr_internal.hpp` around line 74), the current implementation creates an error with `format_location()`. We want to optionally include source context.

However, `make_loc_error()` doesn't have access to the source text. The cleanest approach is to attach source context when converting errors to user-visible strings.

In `src/zephyr_gc.inl`, find where `RuntimeError` objects are converted to displayed strings (look for where `ZephyrRuntimeError` is constructed or where error messages are formatted for output). Enhance those points to:
1. Look up the module source text from `ModuleRecord`
2. Extract the relevant line using `format_source_context()`
3. Prepend it to the error message

**Important**: Don't change `make_loc_error()` itself — it's used everywhere. Instead, enhance the error *display* path in the runtime.

### Step A6 — Common error message improvements

Search for these patterns and improve the messages:

1. `"undefined variable"` → add "did you mean?" hint
2. `"Expected type"` → add source context
3. `"is missing required method"` (trait impl) → add source context
4. `"does not implement trait"` → suggest adding `impl TraitName for Type`

---

## Part B — Iterator Protocol (S-3)

### Background

Current `for x in collection {}` only works with Arrays (hardcoded `ArrayLength` + `LoadIndex`).

Goal: support any object that has `has_next()` and `next()` methods:

```zephyr
// Custom range iterator
struct Range { current: int, end: int }
impl Range {
    fn new(start: int, end: int) -> Range {
        return Range { current: start, end: end };
    }
    fn has_next(self) -> bool { return self.current < self.end; }
    fn next(self) -> int {
        let val = self.current;
        self.current = self.current + 1;
        return val;
    }
}

for x in Range::new(0, 5) {
    // x = 0, 1, 2, 3, 4
}
```

### Step B0 — Read the ForStmt compilation

In `src/zephyr_compiler.inl`, read the full `ForStmt` compilation (around line 5463-5520). It currently:
1. Evaluates `iterable`
2. Stores it and an index counter
3. Loops: index < ArrayLength, body, index++

### Step B1 — Add Iterator trait to install_core()

In `src/zephyr_gc.inl`, in `install_core()` where built-in types are registered, add the `Iterator` trait:

```cpp
// Register built-in Iterator trait
// This is a marker trait — any object with has_next() + next() implements it automatically
```

Actually, **no need to register it formally** — use duck typing. The for-loop compiler will just check at runtime whether the object has `has_next` and `next` methods.

### Step B2 — Modify ForStmt compilation to support both paths

In `src/zephyr_compiler.inl`, change the `ForStmt` handling to emit a dual-path loop:

```
// Pseudocode for new compiled for-in:
eval iterable → store as __iter

// Check if it's an array (ArrayLength check) or iterator (has_next check)
// Emit: if typeof(__iter) == Array → use existing array loop
//       else → use iterator loop (call has_next()/next())
```

**Implementation**: Instead of emitting `BytecodeOp::ArrayLength` directly, emit a runtime check:

Add a new helper opcode `BytecodeOp::ForIterInit` and `BytecodeOp::ForIterNext` OR use the existing method call mechanism.

**Simpler approach** (no new opcodes): At the top of the loop, try `ArrayLength` — if the value is not an array, fall through to a method-call-based loop:

Actually the simplest approach that doesn't require new opcodes:

Emit this pattern for `for x in obj { body }`:
```
eval obj, store as __it
// Check: is __it an array?
dup __it
IsArray  → new opcode, or use existing kind check
JumpIfTrue → array_path

// Iterator path:
while_start:
  load __it
  CallMethod "has_next", 0 args
  JumpIfFalse → exit
  load __it
  CallMethod "next", 0 args
  define x
  body
  jump while_start

array_path:
// existing array loop code
exit:
```

**Better approach**: Add `BytecodeOp::IsArray` opcode that pushes bool, or reuse the existing `typeof` mechanism.

Actually the cleanest implementation: look at the runtime VM and add a check. When `ArrayLength` is emitted and executed, if the value is NOT an array but HAS `has_next()` and `next()` methods, fall through to the iterator path.

**Most practical implementation**:

In the `ForStmt` compiler, emit a conditional:
1. Check if the iterable is an array at compile time if the type is known — but since we don't have static type analysis, do it at runtime
2. Add a new opcode `BytecodeOp::ForIterCheck` that:
   - If TOS is an Array → push the array + 0 (index) + sentinel "array_mode"
   - If TOS has `has_next` method → push the object + sentinel "iter_mode"
   - Otherwise → runtime error

**Simplest practical approach** — modify the runtime ForStmt loop:

Add a new opcode `BytecodeOp::IterHasNext`:
- If the iterable is an Array: `index < array.length`
- If the iterable is an object with `has_next()`: call `has_next()` on it

Add `BytecodeOp::IterNext`:
- If Array: `array[index]`, index++
- If object with `next()`: call `next()` on it

In the compiler, replace `ArrayLength` + `Less` with `IterHasNext`, and replace `LoadIndex` with `IterNext`.

### Step B3 — Implement IterHasNext and IterNext opcodes

In `src/zephyr_compiler.inl`, add:
```cpp
IterHasNext,   // TOS: iterable + index → bool (array: index < len; iterator: call has_next())
IterNext,      // TOS: iterable + index → (value, index+1) (array: arr[i], i+1; iterator: next(), same index)
```

Add their string representations in `opcode_to_string()`.

In the ForStmt compiler (around line 5476), change:
```cpp
// OLD:
emit_load_symbol(index_name, for_stmt->span);
emit_load_symbol(iter_name, for_stmt->span);
emit(BytecodeOp::ArrayLength, for_stmt->span);
emit(BytecodeOp::Less, for_stmt->span);
// ...
emit_load_symbol(iter_name, for_stmt->span);
emit_load_symbol(index_name, for_stmt->span);
emit(BytecodeOp::LoadIndex, for_stmt->span);

// NEW:
emit_load_symbol(iter_name, for_stmt->span);
emit_load_symbol(index_name, for_stmt->span);
emit(BytecodeOp::IterHasNext, for_stmt->span);
// ... (JumpIfFalse to exit)
emit_load_symbol(iter_name, for_stmt->span);
emit_load_symbol(index_name, for_stmt->span);
emit(BytecodeOp::IterNext, for_stmt->span);  // pushes value AND updates index
```

In the runtime VM dispatch (stack-based loop and register-based loop in `src/zephyr_gc.inl`), implement the handlers:

```cpp
case BytecodeOp::IterHasNext: {
    // TOS: index, TOS-1: iterable
    Value index = stack.back(); stack.pop_back();
    Value iterable = stack.back(); stack.pop_back();

    if (iterable.is_object() && iterable.as_object()->kind == ObjectKind::Array) {
        auto* arr = static_cast<ArrayObject*>(iterable.as_object());
        bool has = index.as_int() < static_cast<std::int64_t>(arr->elements.size());
        stack.push_back(iterable);   // push back for IterNext
        stack.push_back(index);
        stack.push_back(Value::from_bool(has));
    } else {
        // Duck-typing: call has_next() on the object
        // Push iterable + index back, then call has_next()
        stack.push_back(iterable);
        stack.push_back(index);
        // Call iterable.has_next()
        ZEPHYR_TRY_ASSIGN(result, call_method(iterable, "has_next", {}, span, module_name, module));
        stack.push_back(result);
    }
    break;
}

case BytecodeOp::IterNext: {
    // TOS: index, TOS-1: iterable → pushes element value, updates index on stack
    Value index = stack.back(); stack.pop_back();
    Value iterable = stack.back(); stack.pop_back();

    if (iterable.is_object() && iterable.as_object()->kind == ObjectKind::Array) {
        auto* arr = static_cast<ArrayObject*>(iterable.as_object());
        Value elem = arr->elements[index.as_int()];
        stack.push_back(iterable);
        stack.push_back(Value::from_int(index.as_int() + 1));  // advance index
        stack.push_back(elem);
    } else {
        // Duck-typing: call next() on the object
        ZEPHYR_TRY_ASSIGN(elem, call_method(iterable, "next", {}, span, module_name, module));
        stack.push_back(iterable);
        stack.push_back(index);  // iterator manages its own state, index unused
        stack.push_back(elem);
    }
    break;
}
```

**Note**: Read the existing `LoadIndex` and `ArrayLength` handlers to understand the exact stack conventions used. Also check how `call_method` is done in the runtime — look for existing method dispatch (e.g. `CallMethod` opcode or similar) and use the same pattern.

Read the ForStmt compilation fully to understand how `index_name` is stored and updated (there may be a separate increment step). Make sure `IterNext` updates the index correctly for array mode.

### Step B4 — Update std/collections.zph range() to return iterator

Update `range(start, end)` in `std/collections.zph` to return a Range iterator instead of an array:

```zephyr
struct Range { current: int, stop: int }

impl Range {
    fn new(start: int, stop: int) -> Range {
        return Range { current: start, stop: stop };
    }
    fn has_next(self) -> bool { return self.current < self.stop; }
    fn next(self) -> int {
        let val = self.current;
        self.current = self.current + 1;
        return val;
    }
}

export fn range(start: int, stop: int) -> Range {
    return Range::new(start, stop);
}
```

This makes `for x in range(0, 5) {}` work via the iterator protocol.

### Step B5 — Tests

Add to `tests/test_compiler.cpp`:

```cpp
void test_custom_iterator() {
    const char* src = R"(
        struct Counter { n: int, max: int }
        impl Counter {
            fn has_next(self) -> bool { return self.n < self.max; }
            fn next(self) -> int {
                let v = self.n;
                self.n = self.n + 1;
                return v;
            }
        }
        let c = Counter { n: 0, max: 3 };
        let sum = 0;
        for x in c {
            sum = sum + x;
        }
        sum
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_int(), 3);  // 0+1+2 = 3
}

void test_range_iterator() {
    const char* src = R"(
        import { range } from "std/collections";
        let total = 0;
        for i in range(1, 5) {
            total = total + i;
        }
        total
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_int(), 10);  // 1+2+3+4 = 10
}

void test_better_error_message() {
    // Test that undefined variable error includes helpful hint
    const char* src = R"(
        fn greet(name: string) -> string { return "hi " + name; }
        greeet("world")
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_error());
    // Error message should contain "greet" as suggestion
    const std::string& msg = result.error().message;
    ASSERT_TRUE(msg.find("greet") != std::string::npos);
}
```

Add corpus `tests/corpus/14_iterator.zph`:

```zephyr
// 14_iterator.zph — Custom iterator protocol

struct Counter { val: int, limit: int }
impl Counter {
    fn has_next(self) -> bool { return self.val < self.limit; }
    fn next(self) -> int {
        let v = self.val;
        self.val = self.val + 1;
        return v;
    }
}

fn sum_counter(limit: int) -> int {
    let c = Counter { val: 0, limit: limit };
    let total = 0;
    for x in c {
        total = total + x;
    }
    return total;
}

assert(sum_counter(5) == 10);  // 0+1+2+3+4 = 10

// Array for-in still works
let arr = [10, 20, 30];
let s = 0;
for v in arr { s = s + v; }
assert(s == 60);
```

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
git add src/zephyr_internal.hpp src/zephyr_compiler.inl src/zephyr_gc.inl `
        std/collections.zph `
        tests/test_compiler.cpp tests/test_common.hpp tests/test_main.cpp `
        tests/test_corpus.cpp tests/corpus/14_iterator.zph
git commit -m "feat: better error messages + iterator protocol (Wave S)

T-1 (error messages):
- format_source_context(): source line + caret pointer in error output
- suggest_similar_name(): Levenshtein-based did-you-mean for undefined variables
- Environment::collect_names(): enumerate scope for suggestions
- ModuleRecord stores source_text for context display

S-3 (iterator protocol):
- IterHasNext opcode: array index check OR duck-typed has_next() call
- IterNext opcode: array element load OR duck-typed next() call
- ForStmt uses IterHasNext/IterNext instead of ArrayLength/LoadIndex
- std/collections.zph: range() returns Range iterator struct

Tests: test_custom_iterator, test_range_iterator, test_better_error_message
Corpus: 14_iterator.zph"
```
