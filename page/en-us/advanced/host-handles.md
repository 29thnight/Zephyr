# Host Handle Policy

> [!WARNING] Defending against dangling pointers and memory leaks
> Exposing raw C++ pointers directly into a dynamic script environment is a common source of Use-After-Free (UAF) bugs and game crashes.

Zephyr uses an internal **4-tier generational handle system** to safely pass game engine objects (entities, components, etc.) into scripts. When a lifecycle boundary is crossed (e.g. a scene change), all handles in the affected tier and below are invalidated atomically.

## Handle Tiers

<div class="custom-features-wrapper">
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>🔴 Frame</h3>
      <p>Shortest lifespan. Invalidated when the current call-stack frame returns. (C++ API: <code>create_frame_handle</code>)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟠 Tick</h3>
      <p>Invalidated at the end of each engine tick (<code>advance_tick()</code>). (C++ API: <code>create_tick_handle</code>)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟡 Persistent</h3>
      <p>Invalidated on scene/level transitions (<code>advance_scene()</code>). (C++ API: <code>create_persistent_handle</code>)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟢 Stable</h3>
      <p>Kept until explicitly released (<code>invalidate_host_handle()</code>). <b>The only tier that can be serialized (saved).</b></p>
    </div>
  </div>
</div>

## Runtime Control

C++ engine developers must call these APIs at lifecycle boundaries for handle policy to work correctly:

```cpp
// Scene / level change: invalidates Persistent and below
rt.advance_scene();

// Start of each render frame: invalidates Tick and below
rt.advance_tick();
```

> [!NOTE] VM behavior on invalid handles
> If a script tries to access an invalidated handle, a **trap fires immediately in Debug builds** (for diagnostics). In Release builds, the runtime follows the configured **Handle Recovery Policy** — typically throwing a handle-exception runtime error.

## Handle Serialization (Save / Load)

When serializing game state (Save Envelope v2), the VM performs a full object-tree traversal:

```cpp
std::string json = rt.serialize_value(my_script_root_value);
```

Only `Stable`-tier handles are permitted in the serialized output. If `Persistent`, `Tick`, or `Frame` handles are encountered during traversal, `serialize_value()` raises an exception to prevent data corruption from short-lived references.

## Smart Handle Wrapper (`ZephyrHandle<T>`)

To prevent a script value from being accidentally expired by a `Frame` or `Tick` collection cycle while still held on the C++ side, use the RAII-based `ZephyrHandle<T>` smart wrapper:

```cpp
// Activate a smart handle around a host object pointer.
// The pointer is pinned (safe from reclamation) for the lifetime of the wrapper.
auto h = ZephyrHandle<Player>(vm, vm.make_host_object(player_ptr));

// Safely access the inner pointer through the wrapper.
if (h) {
    h.get()->damage(10);
}
```

The wrapper calls `vm.pin_value()` on construction and `vm.unpin_value()` on destruction, giving the GC and handle invalidation system an absolute hold-off signal.
