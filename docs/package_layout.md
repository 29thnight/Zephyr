# Zephyr package layout

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

`package.toml` uses a minimal manifest format:

```toml
[package]
name = "my_package"
version = "0.1.0"
entry = "src/lib.zph"

[dependencies]
# reserved for future use
```

`entry` identifies the package entry module. `ZephyrVM::set_package_root()` reads
`package.toml`, then adds the package root and the entry module directory to the
runtime module search paths so imports like `import "lib"` resolve to
`src/lib.zph`.
