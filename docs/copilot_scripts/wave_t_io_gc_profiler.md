# Wave T — std/io module + GC pause metrics (std/gc) + Script-level profiler (std/profiler)

## Goal

Three small features sharing a single wave:

- **B2 std/io**: `print`, `println`, `read_line`, `eprint`, `eprintln` as a proper importable module
- **C1 std/gc**: Expose `GCPauseStats` fields + `gc_collect()` to Zephyr scripts via `import { ... } from "std/gc"`
- **C2 std/profiler**: Script-level `start()` / `stop()` profiler + `--profile` CLI flag

---

## Step 0 — Read before starting

1. Read `src/zephyr_gc.inl` around line 9344 — the `Runtime::install_core()` function where global `print` is registered and around line 9586 where `register_module("std/math", ...)` starts. Understand the exact pattern used (capturing vs non-capturing lambda).
2. Read `src/zephyr_gc.inl` lines 9586–9645 — the full `std/math` module registration to confirm the `m.add_function(name, lambda, param_types, return_type)` signature.
3. Read `cli/main.cpp` — specifically `run_file()` function and the `main()` function's argument parsing to understand where to add `--profile`.

---

## Part A — std/io module

### A1 — Register "std/io" built-in module in `src/zephyr_gc.inl`

Find the `install_core()` function. After the `std/string` module registration block (around line 9820), add a new block:

```cpp
// std/io built-in module
register_module("std/io", [this](ZephyrModuleBinder& m) {
    m.add_function("print", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        bool first = true;
        for (const auto& v : args) {
            if (!first) std::cout << " ";
            first = false;
            std::cout << to_string(v);
        }
        std::cout << std::endl;
        return ZephyrValue();
    }, {}, "Nil");

    m.add_function("println", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        bool first = true;
        for (const auto& v : args) {
            if (!first) std::cout << " ";
            first = false;
            std::cout << to_string(v);
        }
        std::cout << "\n";
        return ZephyrValue();
    }, {}, "Nil");

    m.add_function("eprint", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        bool first = true;
        for (const auto& v : args) {
            if (!first) std::cerr << " ";
            first = false;
            std::cerr << to_string(v);
        }
        std::cerr << std::endl;
        return ZephyrValue();
    }, {}, "Nil");

    m.add_function("eprintln", [](const std::vector<ZephyrValue>& args) -> ZephyrValue {
        bool first = true;
        for (const auto& v : args) {
            if (!first) std::cerr << " ";
            first = false;
            std::cerr << to_string(v);
        }
        std::cerr << "\n";
        return ZephyrValue();
    }, {}, "Nil");

    m.add_function("read_line", [](const std::vector<ZephyrValue>&) -> ZephyrValue {
        std::string line;
        if (!std::getline(std::cin, line)) return ZephyrValue(std::string(""));
        return ZephyrValue(line);
    }, {}, "string");
});
```

**Note**: The outer lambda captures `this` (needed for `ZephyrModuleBinder` constructor context). The inner function lambdas do NOT need `this` — keep them non-capturing for `print`/`println`/`eprint`/`eprintln`/`read_line`.

### A2 — Create `std/io.zph` stub file

Create `std/io.zph` as an **empty file** (zero bytes). The built-in module registration at the C++ level takes priority over `.zph` files. The file just needs to exist so module resolution doesn't error before checking the built-in registry.

Actually, check whether `std/math.zph` or `std/string.zph` exists on disk. If built-in modules work without a `.zph` stub, do NOT create `std/io.zph`. If they require a stub, create an empty one.

---

## Part B — std/gc module (GC pause metrics)

### B1 — Register "std/gc" built-in module in `src/zephyr_gc.inl`

After the `std/io` module block, add:

```cpp
// std/gc built-in module
register_module("std/gc", [this](ZephyrModuleBinder& m) {
    m.add_function("pause_p50_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p50_ns / 1000));
    }, {}, "int");

    m.add_function("pause_p95_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p95_ns / 1000));
    }, {}, "int");

    m.add_function("pause_p99_us", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().p99_ns / 1000));
    }, {}, "int");

    m.add_function("frame_miss_count", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        return ZephyrValue(static_cast<std::int64_t>(get_gc_pause_stats().frame_budget_miss_count));
    }, {}, "int");

    m.add_function("collect", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        collect_garbage();
        return ZephyrValue();
    }, {}, "Nil");
});
```

**Important**: These inner lambdas capture `this` (the `Runtime*`). This is safe because module functions are stored in the same `Runtime` that owns them.

---

## Part C — std/profiler module (script-level profiler)

### C1 — Register "std/profiler" built-in module in `src/zephyr_gc.inl`

After the `std/gc` module block, add:

```cpp
// std/profiler built-in module
register_module("std/profiler", [this](ZephyrModuleBinder& m) {
    m.add_function("start", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        start_profiling();
        return ZephyrValue();
    }, {}, "Nil");

    // stop() returns array of [name, calls, total_us, self_us] per entry
    m.add_function("stop", [this](const std::vector<ZephyrValue>&) -> ZephyrValue {
        ZephyrProfileReport report = stop_profiling();
        std::vector<ZephyrValue> entries;
        entries.reserve(report.entries.size());
        for (const auto& e : report.entries) {
            std::vector<ZephyrValue> row = {
                ZephyrValue(e.function_name),
                ZephyrValue(static_cast<std::int64_t>(e.call_count)),
                ZephyrValue(static_cast<std::int64_t>(e.total_ns / 1000)),
                ZephyrValue(static_cast<std::int64_t>(e.self_ns  / 1000)),
            };
            entries.push_back(ZephyrValue(std::move(row)));
        }
        return ZephyrValue(std::move(entries));
    }, {}, "Array");
});
```

**Note**: `stop()` returns an array of arrays. Each inner array is `[function_name: string, calls: int, total_us: int, self_us: int]`. Sorted by `self_us` descending (done inside `stop_profiling()`).

### C2 — Add `--profile` flag to CLI `run` command

In `cli/main.cpp`, find `run_file()`. Add a `bool profile` parameter and use it:

```cpp
int run_file(const std::filesystem::path& path, const std::filesystem::path& executable_path, bool profile = false) {
    zephyr::ZephyrVM vm;
    configure_default_module_paths(vm, executable_path);
    if (profile) vm.start_profiling();
    vm.execute_file(path);
    if (profile) {
        auto report = vm.stop_profiling();
        std::cout << "\n--- Profile Report ---\n";
        std::cout << std::left
                  << std::setw(40) << "Function"
                  << std::setw(10) << "Calls"
                  << std::setw(14) << "Self (us)"
                  << std::setw(14) << "Total (us)"
                  << "\n";
        std::cout << std::string(78, '-') << "\n";
        for (const auto& e : report.entries) {
            std::cout << std::left
                      << std::setw(40) << e.function_name
                      << std::setw(10) << e.call_count
                      << std::setw(14) << (e.self_ns / 1000)
                      << std::setw(14) << (e.total_ns / 1000)
                      << "\n";
        }
    }
    // existing main() call logic
    const auto handle = vm.get_function(std::filesystem::weakly_canonical(path).string(), "main");
    if (handle.has_value()) {
        const zephyr::ZephyrValue result = vm.call(*handle);
        if (!result.is_nil()) {
            std::cout << zephyr::to_string(result) << std::endl;
        }
    }
    return 0;
}
```

**Add `#include <iomanip>` at the top of `cli/main.cpp` if not already present.**

In `main()`, find the `"run"` subcommand handling and update it to detect `--profile`:

Find the code that calls `run_file(path, executable_path)` for the `run` subcommand. Change it to parse `--profile`:

```cpp
// In the "run" subcommand block:
bool profile_flag = false;
std::filesystem::path run_path;
// iterate remaining argv for --profile and the file path
for (int i = cmd_arg_start; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg == "--profile") {
        profile_flag = true;
    } else {
        run_path = argv[i];
    }
}
return run_file(run_path, argv[0], profile_flag);
```

**Read the actual `main()` argument parsing code first** and adapt the above to match the existing style (it might already parse `argv` in a specific way).

Also update `print_usage()` to mention:
```
  zephyr run [--profile] <file>
```

---

## Part D — Tests

Add to `tests/test_compiler.cpp`:

```cpp
void test_std_io_module() {
    // Test that std/io functions are importable and callable
    const char* src = R"(
        import { print, println } from "std/io";
        println("hello from io");
        42
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(result.value().as_int(), 42);
}

void test_std_gc_module() {
    // Trigger GC and read stats — just verify they return integers
    const char* src = R"(
        import { collect, pause_p50_us, pause_p99_us, frame_miss_count } from "std/gc";
        collect();
        let p50 = pause_p50_us();
        let miss = frame_miss_count();
        miss >= 0
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().as_bool());
}

void test_std_profiler_module() {
    const char* src = R"(
        import { start, stop } from "std/profiler";
        start();
        fn add(a: int, b: int) -> int { return a + b; }
        let x = add(1, 2);
        let entries = stop();
        __zephyr_std_len(entries) >= 0
    )";
    auto result = execute_module(src);
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result.value().as_bool());
}
```

Add these to `test_main.cpp` (in the test runner list where other tests are called).

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
git add src/zephyr_gc.inl cli/main.cpp tests/test_compiler.cpp tests/test_main.cpp
git commit -m "feat: std/io, std/gc, std/profiler modules + --profile CLI flag (Wave T)

- std/io: print, println, eprint, eprintln, read_line as importable module
- std/gc: pause_p50_us, pause_p95_us, pause_p99_us, frame_miss_count, collect
- std/profiler: start(), stop() -> [[name,calls,total_us,self_us],...] sorted by self
- CLI: zephyr run --profile <file> prints hot-function table after execution
- Tests: test_std_io_module, test_std_gc_module, test_std_profiler_module"
```
