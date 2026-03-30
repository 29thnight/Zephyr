## Getting Started

### Install

Clone and build with CMake:

```bash
git clone https://github.com/your-org/zephyr.git
cd zephyr
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Or open `Zephyr.sln` in Visual Studio 18.

> Add the output directory to your **PATH** to use `zephyr` globally.

### Configure your editor

The official VS Code extension provides syntax highlighting, completions, hover, go-to-definition, and inline diagnostics:

```bash
cd editors/vscode-zephyr
npm install
npm run package
code --install-extension zephyr-*.vsix
```

The extension auto-starts the Zephyr LSP server (`zephyr lsp`) when a `.zph` file is opened.

### Command line

```
zephyr run <file>           Execute a script
zephyr check <file>         Typecheck without running
zephyr repl                 Interactive REPL
zephyr dump-bytecode <file> Disassemble bytecode
zephyr stats <file>         Show compilation stats
zephyr bench                Run benchmark suite
zephyr lsp                  Start LSP server (stdio)
zephyr dap                  Start DAP debug adapter (stdio)
```

Use `--profile` to enable the sampling profiler:

```bash
zephyr run --profile mygame.zph
# writes zephyr_profile.json
```

### Hello World

```zephyr
fn main() -> void {
    print("Hello, World!");
}
```

Run it:

```bash
zephyr run hello.zph
# Hello, World!
```

### A slightly larger example

```zephyr
struct Vec2 {
    x: float,
    y: float,
}

trait Printable {
    fn display(self) -> void;
}

impl Printable for Vec2 {
    fn display(self) -> void {
        print(f"Vec2({self.x}, {self.y})");
    }
}

fn main() -> void {
    let v = Vec2 { x: 1.0, y: 2.5 };
    v.display();
}
```

```
Vec2(1, 2.5)
```
