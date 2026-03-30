# Zephyr Package Layout

Zephyr packages use a simple source-first layout:

```text
my_package/
├── package.toml
├── src/
│   ├── lib.zph
│   └── *.zph
└── tests/
    └── *.zph
```

## package.toml

```toml
[package]
name = "my_package"
version = "0.1.0"
entry = "src/lib.zph"

[dependencies]
math = "std/math"
utils = "path/to/utils"
```

`entry` identifies the package entry module. `ZephyrVM::set_package_root()` reads
`package.toml`, then adds the package root and the entry module directory to the
runtime module search paths so `import "lib"` resolves to `src/lib.zph`.

## Module Search Order

1. Relative to the importing file's directory
2. Package root (set via `set_package_root()`)
3. Host-registered modules (via `register_module()`)
4. `std/` built-in standard library

## Standard Library Modules

| Import | Contents |
|---|---|
| `import "std/math"` | `sin`, `cos`, `sqrt`, `abs`, `floor`, `ceil`, `clamp`, `lerp` |
| `import "std/string"` | `split`, `trim`, `replace`, `to_upper`, `to_lower`, `format` |
| `import "std/collections"` | `Map<K,V>`, `Set<T>`, `Queue<T>`, `Stack<T>` |
| `import "std/json"` | `parse(s: string) -> Result<any>`, `stringify(v: any) -> string` |
| `import "std/io"` | `read_file`, `write_file`, `read_lines` |
| `import "std/gc"` | `collect()`, `collect_young()`, `step()`, `stats()` |
| `import "std/profiler"` | `start()`, `stop()`, `report()` |

## Module Bytecode Caching

Compiled modules are cached by source mtime. On load, the runtime checks the
cached `.zphc` file's embedded mtime against the source; if they match, the
cache is used directly, skipping re-compilation.

Cache files are written to the build's working directory alongside the source
(or to `ZEPHYR_CACHE_DIR` if set).
