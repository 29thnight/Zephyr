<div align="center">
  <img src="logo.svg" width="96" height="96" alt="Zephyr logo"/>
  <h1>Zephyr</h1>
</div>

**Zephyr** is a statically-typed, GC-managed scripting language designed for game engine embedding. Written in C++20 with no external runtime dependencies, it features a register-based bytecode VM, generational garbage collector, coroutines, and a safe host-handle policy that keeps engine-owned objects out of GC reach.

**Zephyr** supports procedural, object-oriented, and data-driven programming styles with a Rust-inspired syntax. Traits, generics, pattern matching, and string interpolation are built in. The compiler, VM, and GC combined add less than 300 KB to the host executable on a 64-bit system.

**Zephyr** was built for the [Project Zephyr](https://github.com/29thnight/Zephyr) game scripting initiative to provide a safe, predictable scripting layer that integrates with frame/tick/scene lifetime models common in game engines.

## What Zephyr code looks like

```zephyr
struct Vec2 {
    x: float,
    y: float,
}

trait Drawable {
    fn draw(self) -> string
}

impl Drawable for Vec2 {
    fn draw(self) -> string {
        f"Vec2({self.x}, {self.y})"
    }
}

fn lerp(a: float, b: float, t: float) -> float {
    a + (b - a) * t
}

coroutine fn animate(from: Vec2, to: Vec2, steps: int) {
    let mut i = 0
    while i <= steps {
        let t = i as float / steps as float
        let pos = Vec2 { x: lerp(from.x, to.x, t), y: lerp(from.y, to.y, t) }
        yield pos.draw()
        i += 1
    }
}

fn main() -> string {
    let start = Vec2 { x: 0.0, y: 0.0 }
    let end   = Vec2 { x: 10.0, y: 5.0 }
    let anim  = animate(start, end, 4)
    let result = resume anim
    result
}
```

## Features

* register-based bytecode VM with superinstruction fusion
* generational GC (nursery + old generation, incremental)
* coroutines with heap-resident frames and `yield` / `resume`
* Rust-inspired syntax: `fn`, `let`, `struct`, `enum`, `match`
* traits and `impl` blocks with `where` clause generics
* `Result<T>` type and `?` operator for error propagation
* string interpolation (`f"..."`) and optional chaining (`?.`)
* pattern matching with exhaustiveness checking
* lexical closures with GC-managed upvalue cells
* relative-path and host-module `import` / `export`
* generation-checked host handles (`Frame`, `Tick`, `Persistent`, `Stable`)
* versioned save/load serialization (`ZephyrSaveEnvelope`)
* DAP debug adapter and LSP language server built in
* bytecode caching with mtime-based invalidation
* standard library: `std/math`, `std/string`, `std/collections`, `std/json`, `std/io`, `std/gc`, `std/profiler`
* benchmark harness with JSON reports and acceptance gates

## Building

**Visual Studio (Windows)**

Open `Zephyr.sln` and build, or:

```powershell
msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64
```

**CMake (cross-platform)**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
# Options: -DZEPHYR_BUILD_TESTS=ON -DZEPHYR_BUILD_BENCH=ON -DZEPHYR_BUILD_SAMPLES=ON
```

Requires a C++20 compiler. No external dependencies.

## Usage

```bash
zephyr run   script.zph                        # compile and execute
zephyr check script.zph                        # type-check without running
zephyr repl                                    # interactive REPL
zephyr stats script.zph                        # show VM runtime stats
zephyr dump-bytecode script.zph main           # disassemble function bytecode
zephyr bench bench/results/latest.json         # run benchmark suite
zephyr bench latest.json --baseline v1.json --strict  # regression gate check
zephyr lsp                                     # start LSP server (stdio)
zephyr dap                                     # start DAP debug adapter (stdio)
```

## Project Structure

```
src/
├── zephyr.cpp              ZephyrVM public API implementation
├── zephyr_parser.cpp       Lexer and parser (independent TU)
├── zephyr_gc.cpp           GC public entry points
├── zephyr_gc_impl.cpp      Generational GC implementation (independent TU)
├── zephyr_internal.hpp     Shared internal types and declarations
├── zephyr_lexer.hpp        Lexer class
├── zephyr_types.hpp        Core value/object type definitions
└── zephyr_compiler.hpp     Bytecode compiler and IR
include/zephyr/
└── api.hpp                 Public embedding API
cli/
├── main.cpp                CLI entry point
├── lsp_server.cpp          LSP language server (hover, completion, rename, …)
└── dap_server.cpp          DAP debug adapter
bench/
├── bench_runner.cpp        Benchmark harness
└── results/                JSON baseline files
tests/                      Test suite (lexer, compiler, VM, GC, host, corpus)
samples/engine_sample/      Host embedding example
std/                        Standard library modules (.zph)
editors/vscode-zephyr/      VS Code extension with syntax highlighting
```

## Embedding API

```cpp
#include "zephyr/api.hpp"

zephyr::ZephyrVM vm;
vm.add_module_search_path("scripts/");

// Register a host class
vm.register_host_class<MyEntity>(
    "Entity",
    zephyr::HandlePolicy::Persistent,
    [](auto& b) {
        b.method("get_hp",  &MyEntity::get_hp);
        b.method("set_hp",  &MyEntity::set_hp);
    });

// Execute a script
auto result = vm.execute_file("game/ai.zph");

// Drive coroutines from the game loop
void on_tick(zephyr::ZephyrVM& vm) {
    vm.advance_tick();
    for (auto& handle : active_coroutines)
        vm.resume(handle);
    vm.gc_step(/*budget_us=*/500);
}
```

Full API reference: [`include/zephyr/api.hpp`](include/zephyr/api.hpp)

## Example Scripts

* `examples/state_machine.zph` — state transitions with `enum` and `match`
* `examples/event_handling.zph` — host event handler registration
* `examples/simple_ai.zph` — AI decision tree with `struct` + `enum`

## License

Zephyr is available under the MIT license.
