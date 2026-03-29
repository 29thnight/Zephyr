# Wave R — Associated Function Syntax (TypeName::fn_name) + Collections Method Style

## Goal

Enable Rust-like associated function syntax:
```zephyr
let m = HashMap::new();   // associated function (no self)
m.set("x", 10);           // instance method (with self)
m.get("x");
```

---

## Step 0 — Read before starting

1. Read `src/zephyr_parser.inl` around `parse_identifier_led_expression()` (line 954) — specifically the multi-part path + `(` handling (around line 1012) where `EnumInitExpr` is created for `Namespace::Variant(payload)`.
2. Read `src/zephyr_compiler.inl` around how `ImplDecl` is compiled — how methods are registered on struct type objects. Search for `DeclareImpl` opcode handler.
3. Read `src/zephyr_gc.inl` for the runtime `DeclareImpl` opcode handler — understand how impl methods are stored and dispatched.

---

## Part A — Parser: AssocCallExpr AST node + parsing

### A1 — Add AssocCallExpr struct

In `src/zephyr_parser.inl`, add a new AST node:

```cpp
struct AssocCallExpr final : Expr {
    std::string type_name;            // e.g., "HashMap"
    std::string fn_name;              // e.g., "new"
    std::vector<ExprPtr> args;
};
```

### A2 — Modify parse_identifier_led_expression() to detect associated calls

In `Parser::parse_identifier_led_expression()`, find the multi-part path case where `type_like && path.parts.size() >= 2 && check(TokenType::LeftParen)` leads to `EnumInitExpr` creation.

Read the exact code at that location first. The current logic is approximately:
```cpp
if (path.parts.size() >= 2) {
    expr->variant_name = path.parts.back();
    path.parts.pop_back();
    // creates EnumInitExpr...
}
```

**Change**: Before creating an `EnumInitExpr`, check if the last path part starts with a lowercase letter. If so, it's an associated function call, not an enum variant:

```cpp
if (type_like && path.parts.size() >= 2 && check(TokenType::LeftParen)) {
    const std::string& last_part = path.parts.back();
    const bool last_is_lowercase = !last_part.empty() &&
                                   std::islower(static_cast<unsigned char>(last_part.front()));

    if (last_is_lowercase) {
        // Associated function call: TypeName::fn_name(args)
        advance();  // consume '('
        auto expr = std::make_unique<AssocCallExpr>();
        expr->span = start;
        expr->type_name = path.parts.front();   // first part (e.g. "HashMap")
        expr->fn_name   = path.parts.back();    // last part (e.g. "new")
        // parse arguments
        if (!check(TokenType::RightParen)) {
            do {
                ZEPHYR_TRY_ASSIGN(arg, parse_expression());
                expr->args.push_back(std::move(arg));
            } while (match({TokenType::Comma}));
        }
        ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after arguments."));
        return expr;
    }
    // else: fall through to existing EnumInitExpr logic (uppercase last part = enum variant)
}
```

**Important**: Make sure the existing `EnumInitExpr` multi-part path logic still runs when the last part is uppercase.

---

## Part B — Compiler: compile AssocCallExpr

In `src/zephyr_compiler.inl`, find where `Expr` subtypes are dispatched (look for `dynamic_cast<CallExpr*>`, `dynamic_cast<MemberExpr*>` etc. in the expression compiler).

Add a case for `AssocCallExpr`:

```cpp
if (auto* assoc = dynamic_cast<AssocCallExpr*>(expr)) {
    // Desugar: TypeName::fn_name(args)
    // → load TypeName from env, get property fn_name, call with args

    // 1. Load the type/struct value from environment
    emit_load_symbol(assoc->type_name, assoc->span);

    // 2. Get the method from it
    emit(BytecodeOp::GetProperty, assoc->span, 0, assoc->fn_name);

    // 3. Compile and push all arguments
    for (auto& arg : assoc->args) {
        ZEPHYR_TRY(compile_expr(arg.get()));
    }

    // 4. Call with argc
    emit(BytecodeOp::Call, assoc->span, static_cast<int>(assoc->args.size()));
    return;
}
```

---

## Part C — Runtime: support `fn new()` (no-self) as static method on struct type

### C1 — Read how ImplDecl is currently compiled and executed

Search `src/zephyr_gc.inl` for the `DeclareImpl` opcode handler. It reads the impl AST and registers methods. Currently it likely only registers methods that take `self`.

### C2 — Register no-self methods on the struct type object itself

In the `DeclareImpl` opcode handler:
- For methods WITH `self` (first param is "self"): existing behavior — register on struct type for dynamic dispatch
- For methods WITHOUT `self`: load the struct type object from the environment and set the function as a property directly on it

The key change: when iterating `impl_decl->methods` in the DeclareImpl handler:

```cpp
for (auto& method_decl : impl_decl->methods) {
    bool has_self = !method_decl->params.empty() &&
                    method_decl->params.front().name == "self";

    if (has_self) {
        // existing: register as instance method on struct type
        // (no change needed here)
    } else {
        // NEW: register as a property on the struct TYPE OBJECT in the environment
        // This makes TypeName::static_fn() work via GetProperty on the type value
        auto* compiled_fn = compile_method(...);
        Value fn_value = Value::object(compiled_fn);

        // Find the struct type object in the environment
        auto type_val = environment->get(struct_type_name);
        if (type_val && type_val->is_object()) {
            auto* struct_type = dynamic_cast<StructTypeObject*>(type_val->as_object());
            if (struct_type) {
                // Add method_name → fn_value to struct_type's static_methods map
                struct_type->static_methods[method_decl->name] = fn_value;
            }
        }
    }
}
```

**Note**: You may need to add a `static_methods` field to `StructTypeObject`. Read its definition in `src/zephyr_compiler.inl` to find the exact struct layout. It likely already has a `methods` map — add a separate `static_methods` map or use the same one with a naming convention.

### C3 — GetProperty on struct type object should look in static_methods

In the `GetProperty` opcode handler in `src/zephyr_gc.inl`, when the object is a `StructTypeObject` (not a `StructInstanceObject`), look up in `static_methods`:

```cpp
if (object->kind == ObjectKind::StructType) {
    auto* struct_type = static_cast<StructTypeObject*>(object);
    auto it = struct_type->static_methods.find(property_name);
    if (it != struct_type->static_methods.end()) {
        stack.push_back(it->second);
    } else {
        return make_error("No static method '" + property_name + "' on type '" + struct_type->name + "'");
    }
}
```

---

## Part D — Rewrite std/collections.zph with method syntax

Replace `std/collections.zph` with:

```zephyr
// std/collections.zph — Collections with Rust-like method syntax

// ─── Array utilities ─────────────────────────────────────────────────────────

export fn range(start: int, end: int) -> Array {
    return __zephyr_std_range(start, end);
}

export fn map_array(arr: Array, f: Function) -> Array {
    let mut result = [];
    let mut i: int = 0;
    while i < __zephyr_std_len(arr) {
        result = __zephyr_std_push(result, f(arr[i]));
        i = i + 1;
    }
    return result;
}

export fn filter_array(arr: Array, pred: Function) -> Array {
    let mut result = [];
    let mut i: int = 0;
    while i < __zephyr_std_len(arr) {
        let value = arr[i];
        if pred(value) {
            result = __zephyr_std_push(result, value);
        }
        i = i + 1;
    }
    return result;
}

export fn fold_array(arr: Array, init: any, f: Function) -> any {
    let mut acc = init;
    let mut i: int = 0;
    while i < __zephyr_std_len(arr) {
        acc = f(acc, arr[i]);
        i = i + 1;
    }
    return acc;
}

// ─── HashMap ─────────────────────────────────────────────────────────────────

export struct HashMap { __h: any }

export impl HashMap {
    fn new() -> HashMap {
        return HashMap { __h: __zephyr_hashmap_new() };
    }
    fn set(self, key: string, val: any) {
        __zephyr_hashmap_set(self.__h, key, val);
    }
    fn get(self, key: string) -> any {
        return __zephyr_hashmap_get(self.__h, key);
    }
    fn has(self, key: string) -> bool {
        return __zephyr_hashmap_has(self.__h, key);
    }
    fn delete(self, key: string) {
        __zephyr_hashmap_delete(self.__h, key);
    }
    fn keys(self) -> Array {
        return __zephyr_hashmap_keys(self.__h);
    }
    fn values(self) -> Array {
        return __zephyr_hashmap_values(self.__h);
    }
    fn size(self) -> int {
        return __zephyr_hashmap_size(self.__h);
    }
    fn clear(self) {
        __zephyr_hashmap_clear(self.__h);
    }
}

// ─── Set ─────────────────────────────────────────────────────────────────────

export struct Set { __h: any }

export impl Set {
    fn new() -> Set {
        return Set { __h: __zephyr_set_new() };
    }
    fn add(self, val: any) {
        __zephyr_set_add(self.__h, val);
    }
    fn has(self, val: any) -> bool {
        return __zephyr_set_has(self.__h, val);
    }
    fn delete(self, val: any) {
        __zephyr_set_delete(self.__h, val);
    }
    fn size(self) -> int {
        return __zephyr_set_size(self.__h);
    }
    fn to_array(self) -> Array {
        return __zephyr_set_to_array(self.__h);
    }
}

// ─── Queue ───────────────────────────────────────────────────────────────────

export struct Queue { __h: any }

export impl Queue {
    fn new() -> Queue {
        return Queue { __h: __zephyr_queue_new() };
    }
    fn push(self, val: any) {
        __zephyr_queue_push(self.__h, val);
    }
    fn pop(self) -> any {
        return __zephyr_queue_pop(self.__h);
    }
    fn peek(self) -> any {
        return __zephyr_queue_peek(self.__h);
    }
    fn size(self) -> int {
        return __zephyr_queue_size(self.__h);
    }
    fn is_empty(self) -> bool {
        return __zephyr_queue_is_empty(self.__h);
    }
    fn to_array(self) -> Array {
        return __zephyr_queue_to_array(self.__h);
    }
}
```

**Note**: `export struct` and `export impl` — verify the parser supports `export` before `struct` and `impl`. If not, just use `struct` and `impl` without `export` prefix (the types will be visible since the module namespace is imported by the caller).

---

## Part E — Tests

Update `tests/test_compiler.cpp` to use the new method syntax:

```cpp
void test_assoc_fn_syntax() {
    // HashMap::new() — associated function syntax
    const char* src = R"(
        import { HashMap } from "std/collections";
        let m = HashMap::new();
        m.set("a", 1);
        m.set("b", 2);
        m.size()
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_int(), 2);
}

void test_collections_set_method() {
    const char* src = R"(
        import { Set } from "std/collections";
        let s = Set::new();
        s.add(10);
        s.add(20);
        s.add(10);
        s.size()
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_int(), 2);
}

void test_collections_queue_method() {
    const char* src = R"(
        import { Queue } from "std/collections";
        let q = Queue::new();
        q.push("first");
        q.push("second");
        q.pop()
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_string()->value, "first");
}
```

Also add corpus `tests/corpus/13_collections.zph`:

```zephyr
import { HashMap, Set, Queue, range, map_array } from "std/collections";

fn test_hashmap() {
    let m = HashMap::new();
    m.set("x", 42);
    m.set("y", 99);
    assert(m.get("x") == 42);
    assert(m.has("y") == true);
    assert(m.size() == 2);
    m.delete("x");
    assert(m.size() == 1);
}

fn test_set() {
    let s = Set::new();
    s.add(1); s.add(2); s.add(1);
    assert(s.size() == 2);
    assert(s.has(1) == true);
    assert(s.has(3) == false);
}

fn test_queue() {
    let q = Queue::new();
    q.push("a"); q.push("b");
    assert(q.pop() == "a");
    assert(q.size() == 1);
    assert(q.is_empty() == false);
}

fn test_range_map() {
    let r = range(0, 3);
    assert(r[0] == 0);
    assert(r[2] == 2);
}

test_hashmap();
test_set();
test_queue();
test_range_map();
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
git add src/zephyr_parser.inl src/zephyr_compiler.inl src/zephyr_gc.inl `
        std/collections.zph `
        tests/test_compiler.cpp tests/test_common.hpp tests/test_main.cpp `
        tests/test_corpus.cpp tests/corpus/13_collections.zph
git commit -m "feat: associated function syntax (TypeName::fn) + collections method style (Wave R)

- Parser: AssocCallExpr — TypeName::fn_name(args) where fn_name is lowercase
  → desugars to LoadVar(TypeName).GetProperty(fn_name).Call(args)
  → TypeName::Bar(x) with uppercase Bar still creates EnumInitExpr (unchanged)
- Compiler: AssocCallExpr emit LoadVar + GetProperty + Call
- Runtime: impl methods without 'self' registered as static_methods on StructTypeObject
- StructTypeObject: add static_methods map; GetProperty checks it for type objects
- std/collections.zph: HashMap/Set/Queue now use struct + impl with method syntax
  HashMap::new(), m.set(), m.get(), m.size() etc.
- Tests: test_assoc_fn_syntax, test_collections_set_method, test_collections_queue_method
- Corpus: 13_collections.zph"
```
