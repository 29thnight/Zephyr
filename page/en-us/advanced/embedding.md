# Embedding ZephyrVM

Zephyr is designed to be embedded into C++ environments such as game engines. All built-in APIs are exposed through the `include/zephyr/api.hpp` header.

## Minimal Example

Initialize a `ZephyrVM`, compile a script, and execute it:

```cpp
#include <zephyr/api.hpp>

int main() {
    // 1. Initialize VM and create a runtime
    ZephyrVM vm;
    auto rt = vm.create_runtime();

    // 2. Script source to compile
    const char* source = R"(
        fn add(a: int, b: int) -> int { return a + b; }
        fn main() -> int { return add(10, 20); }
    )";

    // 3. Compile and execute at a specific entry point
    auto chunk = vm.compile_bytecode_function(source, "main");
    auto result = rt.execute(chunk);

    // 4. Extract the result value
    printf("Result: %lld\n", result.as_int()); // 30

    return 0;
}
```

## Loading Files and Calling Functions Repeatedly

Set a package root directory and load script files from disk:

```cpp
ZephyrVM vm;
vm.set_package_root("game/scripts/");
auto rt = vm.create_runtime();

// Load and evaluate immediately
rt.run_file("game/scripts/main.zph");
```

To call a specific function inside a loaded module from the engine loop, use global symbol reflection:

```cpp
auto chunk = vm.compile_module_bytecode(source, "game");
rt.load_module(chunk, "game");

// Retrieve the 'update' function and call it each frame
ZephyrValue fn_val = rt.get_value("update");
ZephyrValue arg = ZephyrValue::from_float(0.016f);  // delta time
rt.call(fn_val, {arg});
```

## Coroutine Control from C++

Zephyr provides APIs to spawn and drive coroutines directly from the host, integrating naturally with the engine update loop:

```cpp
auto co = rt.spawn_coroutine("patrol_ai");
rt.pass_arg(co, entity_handle);

// Inside the engine update loop:
while (!rt.query_coroutine(co).done) {
    rt.resume(co, {});
    engine.step();
}
rt.cancel_coroutine(co);
```
