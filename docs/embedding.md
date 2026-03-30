## Embedding

Zephyr is designed for embedding into C++ applications. The public API lives in `include/zephyr/api.hpp`.

### Minimal example

Suppose you have the following Zephyr script:

```zephyr
fn add(a: int, b: int) -> int {
    return a + b;
}

fn main() -> int {
    return add(10, 20);
}
```

The minimum C++ code to compile and run it:

```cpp
#include <zephyr/api.hpp>

int main() {
    ZephyrVM vm;
    auto rt = vm.create_runtime();

    const char* source = R"(
        fn add(a: int, b: int) -> int { return a + b; }
        fn main() -> int { return add(10, 20); }
    )";

    auto chunk = vm.compile_bytecode_function(source, "main");
    auto result = rt.execute(chunk);

    // result is a ZephyrValue — extract as int
    printf("Result: %lld\n", result.as_int());   // 30

    return 0;
}
```

### Loading a file

```cpp
ZephyrVM vm;
vm.set_package_root("game/scripts/");
auto rt = vm.create_runtime();
rt.run_file("game/scripts/main.zph");
```

### Calling a specific function

```cpp
auto chunk = vm.compile_module_bytecode(source, "game");
rt.load_module(chunk, "game");

ZephyrValue fn_val = rt.get_value("update");
ZephyrValue arg = ZephyrValue::from_float(0.016f);   // delta time
rt.call(fn_val, {arg});
```

### Registering host functions

```cpp
vm.register_function("log_message", [](ZephyrRuntime& rt, ZephyrArgs args) {
    printf("[script] %s\n", args[0].as_string().c_str());
    return ZephyrValue::nil();
});
```

In script:

```zephyr
import "engine";
log_message("Hello from script!");
```

### Registering host types

Use `ZephyrClassBinder<T>` to expose C++ types as script objects:

```cpp
ZephyrClassBinder<Entity> binder("Entity");

binder.method("get_x", [](Entity& e, ZephyrArgs) {
    return ZephyrValue::from_float(e.transform.x);
});
binder.method("set_x", [](Entity& e, ZephyrArgs args) {
    e.transform.x = args[0].as_float();
    return ZephyrValue::nil();
});
binder.method("destroy", [](Entity& e, ZephyrArgs) {
    e.destroy();
    return ZephyrValue::nil();
});

vm.register_class(binder);
```

In script:

```zephyr
import "engine";
let player = get_entity("player");
player.set_x(100.0);
print(player.get_x());
player.destroy();
```

### Host handle lifetimes

Zephyr enforces handle generation checks to prevent use-after-free on host objects:

```cpp
// Allocate a stable handle that survives scene transitions:
auto handle = rt.create_stable_handle(my_entity);

// Advance the tick (invalidates Tick-class handles):
rt.advance_tick();

// Advance the scene (invalidates Persistent-class handles):
rt.advance_scene();
```

| Class | Created by | Invalidated by |
|---|---|---|
| `Frame` | `create_frame_handle()` | end of current call |
| `Tick` | `create_tick_handle()` | `advance_tick()` |
| `Persistent` | `create_persistent_handle()` | `advance_scene()` |
| `Stable` | `create_stable_handle()` | explicit `invalidate_host_handle()` |

### Coroutines from C++

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

### GC integration

Call `gc_step()` once per frame to keep GC pauses within budget:

```cpp
rt.advance_tick();
rt.gc_step(1000);   // 1000 µs budget
```

For full collections at scene transitions:

```cpp
rt.advance_scene();
rt.collect_garbage();
```

### Save / Load

```cpp
// Save script state to JSON envelope
std::string json = rt.serialize_value(my_value);

// Load it back (accepts both v2 envelope and legacy plain trees)
ZephyrValue loaded = rt.deserialize_value(json);
```

Only `Stable` handles survive serialization. `Persistent` and shorter handles are rejected at `serialize_value()` time.
