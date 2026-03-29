# Wave N — Lowercase Primitive Types

## Goal

Change all primitive type names from PascalCase to lowercase:
- `Int` → `int`
- `Float` → `float`
- `Bool` → `bool`
- `String` → `string`
- `Any` → `any`
- `Void` → `void`

User-defined types (structs, enums, traits) remain PascalCase (e.g., `Point`, `Color`, `MyTrait`).

---

## Step 0 — Survey before touching anything

1. Grep `src/zephyr_lexer.inl` for the keywords map (`{"import"`, etc.) to understand where to add new keywords.
2. Grep `src/zephyr_compiler.inl` for all occurrences of `"Int"`, `"Float"`, `"Bool"`, `"String"`, `"Any"`, `"Void"`, `"Nil"` as string literals.
3. Grep `src/zephyr_parser.inl` for `parse_type_ref` and any hardcoded type name strings.
4. Note all test files and corpus files that use uppercase type names.

---

## Step 1 — Lexer: add lowercase primitive keyword tokens

In `src/zephyr_lexer.inl`:

**1a.** Find the `TokenType` enum. Add new entries:
```cpp
KeywordInt,
KeywordFloat,
KeywordBool,
KeywordString,
KeywordVoid,
KeywordAny,
```
(Add near the other keyword tokens like `KeywordFn`, `KeywordLet`, etc.)

**1b.** Find the keywords map (the `std::unordered_map<std::string, TokenType>` or similar structure mapping string → TokenType). Add:
```cpp
{"int",    TokenType::KeywordInt},
{"float",  TokenType::KeywordFloat},
{"bool",   TokenType::KeywordBool},
{"string", TokenType::KeywordString},
{"void",   TokenType::KeywordVoid},
{"any",    TokenType::KeywordAny},
```

**1c.** In `token_type_to_string()` (or equivalent debug function), add cases for each new token type.

---

## Step 2 — Parser: accept lowercase primitive types in type position

In `src/zephyr_parser.inl`:

**2a.** Modify `Parser::parse_type_ref()` to also accept the new primitive keyword tokens, not just `TokenType::Identifier`:

```cpp
RuntimeResult<TypeRef> Parser::parse_type_ref() {
    TypeRef type;
    type.span = peek().span;

    // Accept either a plain identifier OR a lowercase primitive keyword as a type name
    static const std::unordered_map<TokenType, std::string> kPrimitiveTypeNames = {
        {TokenType::KeywordInt,    "int"},
        {TokenType::KeywordFloat,  "float"},
        {TokenType::KeywordBool,   "bool"},
        {TokenType::KeywordString, "string"},
        {TokenType::KeywordVoid,   "void"},
        {TokenType::KeywordAny,    "any"},
    };

    auto prim_it = kPrimitiveTypeNames.find(peek().type);
    if (prim_it != kPrimitiveTypeNames.end()) {
        advance();  // consume the keyword token
        type.parts.push_back(prim_it->second);
        return type;
    }

    // Existing path: plain identifier (user-defined types like Point, Color, T, U...)
    ZEPHYR_TRY_ASSIGN(type_name, consume(TokenType::Identifier, "Expected type name."));
    type.parts.push_back(type_name.lexeme);
    while (match({TokenType::DoubleColon})) {
        ZEPHYR_TRY_ASSIGN(segment, consume(TokenType::Identifier, "Expected type segment after '::'."));
        type.parts.push_back(segment.lexeme);
    }
    return type;
}
```

**2b.** The `type_like` heuristic (checks `std::isupper` on first char) is used to detect enum constructors vs variable references. This heuristic remains **unchanged** — lowercase primitive type names only appear in type annotation positions (after `:` or `->`), not as expressions. No change needed there.

---

## Step 3 — Compiler: update enforce_type and all hardcoded type strings

In `src/zephyr_compiler.inl`:

Search for ALL occurrences of these strings and replace:
- `"Int"` → `"int"` (where used as a type name string, not as a variable/field name)
- `"Float"` → `"float"`
- `"Bool"` → `"bool"`
- `"String"` → `"string"`
- `"Any"` → `"any"`
- `"Void"` → `"void"`

Specifically look for:
- `enforce_type(value, "Int", ...)` style calls
- `emit_define_symbol(name, span, "Int", ...)` — the for-loop index type annotation
- Any `type_name == "Int"` comparisons
- Any `TypeRef{{{"Int"}}, ...}` constructions

Be careful: only replace where the string represents a *type name*, not where it's a display string or error message.

Also check `src/zephyr_gc.inl` and `src/zephyr.cpp` for similar hardcoded type strings.

---

## Step 4 — Update `install_core()` type registrations (if any)

Search `src/zephyr_gc.inl` and `src/zephyr.cpp` for any calls that register primitive type names as strings (e.g., for `typeof()` builtin or type display). Update those from `"Int"` → `"int"` etc.

Also search for `value_type_name()` or similar functions that return `"Int"`, `"Float"` etc. as display names. Change those to lowercase.

---

## Step 5 — Update all test files

In `tests/test_compiler.cpp`, `tests/test_vm.cpp`, `tests/test_gc.cpp`, `tests/test_lexer.cpp`:

Replace all type annotations in embedded Zephyr source strings:
- `-> Int` → `-> int`
- `-> Float` → `-> float`
- `-> Bool` → `-> bool`
- `-> String` → `-> string`
- `-> Void` → `-> void`
- `: Int` → `: int` (in param/field positions)
- `: Float` → `: float`
- `: Bool` → `: bool`
- `: String` → `: string`
- `: Any` → `: any`

Be careful NOT to replace:
- Variable names that happen to be `Int`, `Float`, etc. (none should exist)
- Enum variant names like `Ok`, `Err`, `None` (these stay PascalCase)
- Struct/enum type names like `Point`, `Color` (stay PascalCase)

---

## Step 6 — Update all corpus files

Update these files in `tests/corpus/`:
- `03_optional_chaining.zph`: `Int` → `int`, `Any` → `any`
- `04_pattern_matching.zph`: `Int` → `int`, `String` → `string`
- `05_coroutine.zph`: `Int` → `int`
- `06_traits.zph`: `String` → `string`
- `07_generics.zph`: check and update if needed
- `08_result_patterns.zph`: `Int` → `int`, `Vec2 { x: Int, y: Int }` → `Vec2 { x: int, y: int }`
- `09_modules.zph`: check and update if needed

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

Fix any failures before committing.

---

## Step 8 — Update LSP hover/completion strings (if applicable)

In `cli/lsp_server.cpp`, search for any hardcoded `"Int"`, `"Float"`, `"Bool"`, `"String"` strings used in hover text, completion items, or diagnostic messages. Update to lowercase.

---

## Step 9 — Commit

```powershell
cd "C:\Users\lance\OneDrive\Documents\Project Zephyr"
git add src/zephyr_lexer.inl src/zephyr_parser.inl src/zephyr_compiler.inl `
        src/zephyr_gc.inl src/zephyr.cpp cli/lsp_server.cpp `
        tests/test_compiler.cpp tests/test_vm.cpp tests/test_gc.cpp tests/test_lexer.cpp `
        tests/corpus/03_optional_chaining.zph tests/corpus/04_pattern_matching.zph `
        tests/corpus/05_coroutine.zph tests/corpus/06_traits.zph `
        tests/corpus/07_generics.zph tests/corpus/08_result_patterns.zph `
        tests/corpus/09_modules.zph
git commit -m "refactor: lowercase primitive types (int/float/bool/string/void/any) (Wave N)

- Lexer: add KeywordInt/Float/Bool/String/Void/Any token types
- Parser: parse_type_ref() accepts lowercase primitive keywords
- Compiler: update all enforce_type and hardcoded type string comparisons
- Tests: update all embedded Zephyr source strings to use lowercase types
- Corpus: update all .zph files to use lowercase primitive types"
```
