# C++ Class Bindings

You can bridge C++ functions and classes from the host side into the Zephyr script environment.

## Global Function Bindings

Register global host functions — such as engine logging or timers — and expose them to Zephyr's module system via `import`. Use `vm.register_function` on the host side:

```cpp
vm.register_function("log_message", [](ZephyrRuntime& rt, ZephyrArgs args) {
    printf("[script log] %s\n", args[0].as_string().c_str());
    return ZephyrValue::nil();
});
```

Usage in a script:

```zephyr
import "engine"; // default global host module
log_message("Hello from script!");
```

## Class Method Bindings (`ZephyrClassBinder<T>`)

Use the `ZephyrClassBinder<T>` template utility to expose member functions and fields of a C++ instance as chainable methods in the script:

```cpp
ZephyrClassBinder<Entity> binder("Entity");

// Bind member functions via lambdas
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

// Register the class with the VM
vm.register_class(binder);
```

After registration, scripts can read and write C++ instance state directly through host handles:

```zephyr
import "engine";
let player = get_entity("player");

player.set_x(100.0);
print(player.get_x());
player.destroy();
```
