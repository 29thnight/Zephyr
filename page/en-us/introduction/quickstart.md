# Getting Started

This guide walks you through building the Zephyr compiler and VM, setting up the editor extension, and running your first script.

## Installation and Build

Build cross-platform via CMake (requires a C++20 compiler; no external dependencies):

```bash
git clone https://github.com/29thnight/Zephyr.git
cd Zephyr
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

On Windows, you can also open `Zephyr.sln` in Visual Studio 18 and build from there.

> Add the directory containing the built binaries to your system **PATH** to use the `zephyr` CLI globally.

## Editor Setup (VS Code)

The official VS Code extension provides syntax highlighting, auto-completion, hover type inference, go-to-definition, and inline diagnostics.

```bash
cd editors/vscode-zephyr
npm install
npm run package
code --install-extension zephyr-*.vsix
```

Opening any `.zph` file automatically starts the built-in language server (`zephyr lsp`) in the background.

<div class="custom-features-wrapper">
  <h2>CLI Commands</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>zephyr run</h3>
      <p>Compile and execute a script immediately. Append <code>--profile</code> to emit a sampling trace file.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr check</h3>
      <p>Run only the lexer, parser, and type-checker (semacheck) without executing the VM — useful for CI validation.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr repl / dump</h3>
      <p>Start an interactive session with <code>repl</code>, or disassemble a script into opcodes with <code>dump-bytecode</code>.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr lsp / dap</h3>
      <p>Host the standard language server (LSP) and debug adapter (DAP) in the background for editor integration.</p>
    </div>
  </div>
</div>

For profiling, add the `--profile` flag:
```bash
zephyr run --profile mygame.zph
# Writes zephyr_profile.json with sampling data.
```

## Hello World

Write a minimal script and run it:

```zephyr
// hello.zph
fn main() -> void {
    print("Hello, World!");
}
```

```bash
zephyr run hello.zph
# Output: Hello, World!
```

## A More Complete Example

Structs and traits in action:

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
