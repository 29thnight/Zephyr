# C++ Class Bindings

Zephyr effortlessly expands C++ execution limits bridging external structural native pipelines directly into VM instances ensuring native script interactions.

## Exporting Host Global Functions

Utilize the structural API method `vm.register_function` capturing isolated logging hooks encapsulating them deeply onto imported Engine modules securely.

```cpp
vm.register_function("log_message", [](ZephyrRuntime& rt, ZephyrArgs args) {
    printf("[script log] %s\n", args[0].as_string().c_str());
    return ZephyrValue::nil();
});
```

Integrating cleanly directly via `.zph` scripts:

```zephyr
import "engine"; // Globally registered C++ environment
log_message("Hello from script!");
```

## Binding Class Metadata (`ZephyrClassBinder<T>`)

The robust meta-template pipeline builder `ZephyrClassBinder<T>` maps complex internal C++ instances cleanly casting them backwards spanning fully integrated internal chainable logic.

```cpp
ZephyrClassBinder<Entity> binder("Entity");

// Extracting Lambda representations natively spanning ZephyrArgs loops
binder.method("get_x", [](Entity& e, ZephyrArgs) {
    return ZephyrValue::from_float(e.transform.x);
});

binder.method("set_x", [](Entity& e, ZephyrArgs args) {
    e.transform.x = args[0].as_float();
    return ZephyrValue::nil(); // Ensures Void formatting constraints
});

binder.method("destroy", [](Entity& e, ZephyrArgs) {
    e.destroy();
    return ZephyrValue::nil();
});

// Approves registration binding formally upon setup completion
vm.register_class(binder);
```

After validating object instantiation natively upon scripting evaluation, Zephyr pointers dynamically mutate internal boundaries cleanly executing the methods synchronously:

```zephyr
import "engine";
let player = get_entity("player");

player.set_x(100.0);
print(player.get_x());
player.destroy();
```
