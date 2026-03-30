# Embedding ZephyrVM

Zephyr was engineered to be effortlessly initialized and executed seamlessly from within robust C++ Game Engine environments. Every built-in host manipulation API rests explicitly within the `include/zephyr/api.hpp` footprint.

## Minimal Integration Example

Evaluate fundamental logic initializing a lightweight `ZephyrVM` instance compiling structural bytecode arrays optimally directly towards native engine execution.

```cpp
#include <zephyr/api.hpp>

int main() {
    // 1. Spawning VM and configuring native Runtime pipeline.
    ZephyrVM vm;
    auto rt = vm.create_runtime();

    // 2. Formatting a raw script code string.
    const char* source = R"(
        fn add(a: int, b: int) -> int { return a + b; }
        fn main() -> int { return add(10, 20); }
    )";

    // 3. Generating structural bytecode translating via the target string payload
    auto chunk = vm.compile_bytecode_function(source, "main");
    auto result = rt.execute(chunk);

    // 4. Extracting ZephyrValue objects cleanly
    printf("Result: %lld\n", result.as_int()); // 30

    return 0;
}
```

## Parsing Static File Directives

Instruct the host VM instance referencing exact directory bounds (`set_package_root`) bridging multi-file compilation.

```cpp
ZephyrVM vm;
vm.set_package_root("game/scripts/");
auto rt = vm.create_runtime();

// Evaluates targeted disk package immediately traversing standard bounds.
rt.run_file("game/scripts/main.zph");
```

To invoke logic natively across engine loops, invoke reflective bindings scanning module states manually:

```cpp
auto chunk = vm.compile_module_bytecode(source, "game");
rt.load_module(chunk, "game");

// Targets `update` bound exclusively parsing the script.
ZephyrValue fn_val = rt.get_value("update");
ZephyrValue arg = ZephyrValue::from_float(0.016f);   // Submits Delta Time bindings
rt.call(fn_val, {arg});
```

## Coroutines from C++

Zephyr allows direct manipulation of coroutines from the host environment, enabling seamless integration with engine update loops.

```cpp
auto co = rt.spawn_coroutine("patrol_ai");
rt.pass_arg(co, entity_handle);

// In engine update loop:
while (!rt.query_coroutine(co).done) {
    rt.resume(co, {});
    engine.step();
}
rt.cancel_coroutine(co);
```
