# Wave Q — Standard Library: std/json (Q-1) + std/collections (Q-2)

---

## Step 0 — Read before starting

1. Read `src/zephyr_gc.inl` around line 9225 (`register_module("std/math", ...)`) to understand the exact `ZephyrModuleBinder` API (`add_function`, `add_constant`, argument types, return types).
2. Read `include/zephyr/api.hpp` for `ZephyrValue` API: `is_string()`, `as_string()`, `is_int()`, `as_int()`, `is_float()`, `as_float()`, `is_bool()`, `as_bool()`, `is_null()`, `is_array()`, `as_array()`, etc. Understand how to construct Values and how arrays work.
3. Read `src/zephyr_gc.inl` around the HostHandle system to understand how to use `ZephyrVM::register_host_object()` or equivalent for opaque C++ objects.

---

## Part A — std/json (Q-1)

### Goal

```zephyr
import { parse, stringify, parse_safe } from "std/json";

let data = parse('{"name": "Zephyr", "version": 1}');
let name = data.name;    // "Zephyr"
let ver  = data.version; // 1

let s = stringify(data); // '{"name":"Zephyr","version":1}'

let r = parse_safe("not json");  // Err("unexpected token")
```

### A1 — Implement a minimal JSON parser in C++ (in src/zephyr_gc.inl)

Add a static helper `json_parse_value()` and `json_stringify_value()` BEFORE the `install_core()` function (or in an anonymous namespace). Do NOT use any external JSON library — implement from scratch:

```cpp
// Forward declarations
static ZephyrValue json_parse_value(const std::string& src, std::size_t& pos);

static void skip_whitespace(const std::string& src, std::size_t& pos) {
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r'))
        ++pos;
}

// Returns ZephyrValue for: null, bool, int64, double, string, array, object
// Throws std::runtime_error on parse failure
static ZephyrValue json_parse_value(const std::string& src, std::size_t& pos) {
    skip_whitespace(src, pos);
    if (pos >= src.size()) throw std::runtime_error("Unexpected end of JSON");

    char c = src[pos];

    if (c == 'n') { /* parse null */
        if (src.substr(pos, 4) == "null") { pos += 4; return ZephyrValue(); /* null */ }
        throw std::runtime_error("Invalid JSON token at pos " + std::to_string(pos));
    }
    if (c == 't') { /* parse true */
        if (src.substr(pos, 4) == "true") { pos += 4; return ZephyrValue(true); }
        throw std::runtime_error("Invalid JSON token at pos " + std::to_string(pos));
    }
    if (c == 'f') { /* parse false */
        if (src.substr(pos, 5) == "false") { pos += 5; return ZephyrValue(false); }
        throw std::runtime_error("Invalid JSON token at pos " + std::to_string(pos));
    }
    if (c == '"') { /* parse string */
        ++pos;
        std::string result;
        while (pos < src.size() && src[pos] != '"') {
            if (src[pos] == '\\') {
                ++pos;
                switch (src[pos]) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    default:   result += src[pos]; break;
                }
            } else {
                result += src[pos];
            }
            ++pos;
        }
        if (pos >= src.size()) throw std::runtime_error("Unterminated string in JSON");
        ++pos; // consume closing '"'
        return ZephyrValue(result);
    }
    if (c == '[') { /* parse array */
        ++pos;
        std::vector<ZephyrValue> arr;
        skip_whitespace(src, pos);
        if (pos < src.size() && src[pos] == ']') { ++pos; return ZephyrValue(arr); }
        while (true) {
            arr.push_back(json_parse_value(src, pos));
            skip_whitespace(src, pos);
            if (pos >= src.size()) throw std::runtime_error("Unterminated array in JSON");
            if (src[pos] == ']') { ++pos; break; }
            if (src[pos] != ',') throw std::runtime_error("Expected ',' in JSON array");
            ++pos;
        }
        return ZephyrValue(arr);
    }
    if (c == '{') { /* parse object → Zephyr struct-like dict using array of key-value pairs stored as a special array, OR use ZephyrValue map */
        // Represent JSON object as a Zephyr "struct instance" with dynamic fields
        // Use ZephyrValue::make_object() or ZephyrValue dict API if available
        // Otherwise: represent as array of [key, value, key, value, ...] pairs
        // Best approach: check if ZephyrValue has a map/dict constructor
        // If not, use a flat array of alternating key-value pairs
        ++pos;
        // Actually: use ZephyrValue's object/struct API if available
        // Read api.hpp to find the right way to create a dynamic-keyed object
        // If ZephyrValue supports field setting: create an object and set fields
        // Otherwise fall back to array representation
        // TODO: read api.hpp and implement correctly
        throw std::runtime_error("TODO: implement object parsing");
    }
    // parse number
    std::size_t start = pos;
    bool is_float = false;
    if (c == '-') ++pos;
    while (pos < src.size() && std::isdigit(src[pos])) ++pos;
    if (pos < src.size() && src[pos] == '.') { is_float = true; ++pos; while (pos < src.size() && std::isdigit(src[pos])) ++pos; }
    if (pos < src.size() && (src[pos] == 'e' || src[pos] == 'E')) {
        is_float = true; ++pos;
        if (pos < src.size() && (src[pos] == '+' || src[pos] == '-')) ++pos;
        while (pos < src.size() && std::isdigit(src[pos])) ++pos;
    }
    std::string num_str = src.substr(start, pos - start);
    if (num_str.empty()) throw std::runtime_error("Invalid JSON at pos " + std::to_string(pos));
    if (is_float) return ZephyrValue(std::stod(num_str));
    return ZephyrValue(static_cast<std::int64_t>(std::stoll(num_str)));
}
```

**IMPORTANT for JSON objects**: Read `include/zephyr/api.hpp` to understand how to create a dynamic-key Zephyr value (like a dict/map or struct instance with arbitrary fields). Look for:
- `ZephyrValue` constructor taking a `std::unordered_map<std::string, ZephyrValue>`
- Or `ZephyrObjectBuilder` / `ZephyrMapValue`
- Or any way to create an object with arbitrary string keys

If none exists, represent JSON objects as a Zephyr Array of alternating [key, value, key, value...] pairs and note it as a limitation. The user can upgrade later.

**Implement `json_stringify_value()`**:
```cpp
static std::string json_stringify_value(const ZephyrValue& val, int indent = 0) {
    if (val.is_null())   return "null";
    if (val.is_bool())   return val.as_bool() ? "true" : "false";
    if (val.is_int())    return std::to_string(val.as_int());
    if (val.is_float())  {
        std::ostringstream oss; oss << val.as_float(); return oss.str();
    }
    if (val.is_string()) {
        std::string s = "\"";
        for (char c : val.as_string()) {
            if      (c == '"')  s += "\\\"";
            else if (c == '\\') s += "\\\\";
            else if (c == '\n') s += "\\n";
            else if (c == '\r') s += "\\r";
            else if (c == '\t') s += "\\t";
            else s += c;
        }
        return s + "\"";
    }
    if (val.is_array()) {
        std::string s = "[";
        const auto& arr = val.as_array();
        for (std::size_t i = 0; i < arr.size(); ++i) {
            if (i > 0) s += ",";
            s += json_stringify_value(arr[i]);
        }
        return s + "]";
    }
    // For struct instances / objects: iterate fields and stringify
    // Read how to iterate struct fields in ZephyrValue — check api.hpp for is_struct_instance(), fields(), etc.
    // TODO: implement struct/object stringification after reading api.hpp
    return "null";
}
```

### A2 — Register "std/json" module

In `install_core()` in `src/zephyr_gc.inl`, after the `std/string` module registration, add:

```cpp
register_module("std/json", [](ZephyrModuleBinder& m) {
    // parse(str) -> Value  (throws on invalid JSON)
    m.add_function("parse", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        if (args.size() != 1 || !args[0].is_string())
            fail("json.parse expects a string argument.");
        std::size_t pos = 0;
        return json_parse_value(args[0].as_string(), pos);
    }, {"string"}, "any");

    // stringify(value) -> string
    m.add_function("stringify", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        if (args.size() != 1) fail("json.stringify expects 1 argument.");
        return ZephyrValue(json_stringify_value(args[0]));
    }, {"any"}, "string");

    // parse_safe(str) -> Result (Ok(value) | Err(msg))
    m.add_function("parse_safe", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        if (args.size() != 1 || !args[0].is_string())
            fail("json.parse_safe expects a string argument.");
        try {
            std::size_t pos = 0;
            ZephyrValue result = json_parse_value(args[0].as_string(), pos);
            // Return Ok(result) — find how to construct Result::Ok in ZephyrValue
            // Check how the existing Result enum is created from host code
            // It may be: ZephyrValue::make_enum("Result", "Ok", result)
            // Or: look at how existing functions return Ok/Err
            return result; // TODO: wrap in Ok() once you know the API
        } catch (const std::exception& e) {
            return ZephyrValue(std::string(e.what())); // TODO: wrap in Err()
        }
    }, {"string"}, "any");
});
```

**Note**: For `parse_safe`, read how the `Result` enum (Ok/Err) is created from host C++ code. Search for existing uses of `ZephyrValue` with `"Ok"` or `"Err"` variant construction in `src/zephyr_gc.inl` or `include/zephyr/api.hpp`.

---

## Part B — std/collections (Q-2)

### Goal

```zephyr
import { HashMap, Set, Queue } from "std/collections";

let map = HashMap.new();
map.set("key", 42);
let v = map.get("key");   // 42
let has = map.has("key"); // true
map.delete("key");
let keys = map.keys();    // array
let size = map.size();    // int

let s = Set.new();
s.add(1); s.add(2); s.add(1);
s.size();      // 2
s.has(1);      // true
s.to_array();  // [1, 2]

let q = Queue.new();
q.push(1); q.push(2);
q.pop();   // 1 (FIFO)
q.peek();  // 2
q.size();  // 1
```

### Implementation strategy: HostHandle wrapping C++ STL containers

Read `include/zephyr/api.hpp` for the HostHandle API. Specifically look for:
- `ZephyrVM::register_host_type()` or `ZephyrVM::create_host_object()`
- How to store C++ data in a `ZephyrValue` that the GC can track
- The `ZephyrHostHandlePolicy` mechanism

If the VM supports opaque host handles, use them to wrap `std::unordered_map<std::string, ZephyrValue>` for HashMap, `std::unordered_set` (using value hashing) for Set, and `std::deque<ZephyrValue>` for Queue.

**Alternative (simpler)**: Implement all three as factory functions returning Zephyr struct instances with closures capturing a shared_ptr to C++ data. This avoids needing to extend the host handle system.

**Recommended approach — factory functions returning opaque objects**:

For HashMap, register functions in the module that take a "this" handle as first arg:

```cpp
register_module("std/collections", [this](ZephyrModuleBinder& m) {
    // HashMap.new() — returns a handle/token representing the map
    // map.set(key, value), map.get(key), map.has(key), map.delete(key),
    // map.keys(), map.values(), map.size(), map.clear()

    // The cleanest approach is to make HashMap.new() return a ZephyrValue
    // that wraps a shared_ptr<std::unordered_map<std::string, ZephyrValue>>
    // using the HostHandle system.

    // Read how HostHandle slots work in the VM. Look for:
    //   register_host_handle_kind()
    //   create_host_handle()
    //   get_host_handle<T>()
    // in include/zephyr/api.hpp

    // Then implement:
    m.add_function("HashMap_new", [this](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        auto map = std::make_shared<std::unordered_map<std::string, ZephyrValue>>();
        // Store map in host handle, return handle value
        // TODO: implement after reading the host handle API
        return ZephyrValue(); // placeholder
    }, {}, "any");
    // ... etc
});
```

**Read the host handle API first before implementing**. The existing `ZephyrClassBinder` in `include/zephyr/api.hpp` may already provide the pattern needed.

After reading the API, implement all three collection types with the appropriate method functions. Each collection type needs:

**HashMap**: new, set(key, val), get(key) → val|null, has(key) → bool, delete(key), keys() → array, values() → array, entries() → array of [k,v] pairs, size() → int, clear()

**Set**: new, add(val), has(val) → bool, delete(val), size() → int, to_array() → array, union(other) → Set, intersection(other) → Set

**Queue**: new, push(val), pop() → val|null, peek() → val|null, size() → int, is_empty() → bool, to_array() → array

---

## Tests

Add to `tests/test_compiler.cpp`:

```cpp
void test_std_json_parse() {
    const char* src = R"(
        import { parse, stringify } from "std/json";
        let data = parse("{\"x\": 42, \"ok\": true}");
        stringify(42)
    )";
    // At minimum, test that stringify works
    const char* src2 = R"(
        import { stringify } from "std/json";
        stringify(42)
    )";
    auto result = execute_module(src2);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_string()->value, "42");
}

void test_std_json_array() {
    const char* src = R"(
        import { parse, stringify } from "std/json";
        let arr = parse("[1, 2, 3]");
        stringify(arr)
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_string()->value, "[1,2,3]");
}

void test_std_collections_hashmap() {
    const char* src = R"(
        import { HashMap_new, HashMap_set, HashMap_get, HashMap_has, HashMap_size } from "std/collections";
        let m = HashMap_new();
        HashMap_set(m, "x", 10);
        HashMap_set(m, "y", 20);
        HashMap_size(m)
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_int(), 2);
}
```

Add corpus file `tests/corpus/12_std_json.zph`:

```zephyr
import { parse, stringify } from "std/json";

fn test_numbers() {
    assert(stringify(42) == "42");
    assert(stringify(3.14) == "3.14");
    assert(stringify(true) == "true");
    assert(stringify(false) == "false");
}

fn test_strings() {
    assert(stringify("hello") == "\"hello\"");
}

fn test_arrays() {
    let arr = parse("[1, 2, 3]");
    assert(stringify(arr) == "[1,2,3]");
}

test_numbers();
test_strings();
test_arrays();
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
git add src/zephyr_gc.inl include/zephyr/api.hpp `
        tests/test_compiler.cpp tests/test_common.hpp tests/test_main.cpp `
        tests/corpus/12_std_json.zph tests/test_corpus.cpp
git commit -m "feat: std/json + std/collections (Wave Q)

Q-1 (std/json):
- json_parse_value(): recursive descent JSON parser, no external deps
- json_stringify_value(): Value -> JSON string serialization
- Module exports: parse(), stringify(), parse_safe()

Q-2 (std/collections):
- HashMap: new/set/get/has/delete/keys/values/size/clear
- Set: new/add/has/delete/size/to_array
- Queue: new/push/pop/peek/size/is_empty

Tests: test_std_json_parse, test_std_json_array, test_std_collections_hashmap
Corpus: 12_std_json.zph"
```
