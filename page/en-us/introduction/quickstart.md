# Getting Started

To explore Zephyr locally without binding it instantly to a C++ engine, you can compile and use the standalone CLI application.

## Prerequisites

- **CMake** (v3.15 or later)
- **C++ Compiler** with standard C++20 support (MSVC, GCC, Clang)

## Building the CLI

You can clone the Zephyr repository and assemble the toolchain right out of the box using CMake.

```bash
git clone https://github.com/29thnight/Zephyr.git
cd Zephyr
mkdir build && cd build

# Configure and compile
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

Once parsing and linking finishes, the `zephyr` CLI executable will be produced in the output directory.

## Testing Your First Script

Create a `hello.zph` file and utilize the standard I/O to output string properties.

```zephyr
// hello.zph
fn main() -> void {
    print("Welcome to Zephyr!");
}
```

Then invoke the script using the run sub-command:

```bash
zephyr run hello.zph
```

<div class="custom-features-wrapper">
  <h2>CLI Commands</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>zephyr run</h3>
      <p>Immediately loads and executes the `.zph` script. Append the `--profile` flag for trace exports.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr check</h3>
      <p>Runs Lexer, Parser, and Semacheck passes but halts execution to safely validate logic structures.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr repl / dump</h3>
      <p>Initiate an interactive REPL terminal, or dump bytecode instructions utilizing `dump-bytecode` for technical analysis.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr lsp / dap</h3>
      <p>Initiates the Language Server Protocol wrapper (LSP) and Debug Adapter (DAP) for seamless external IDE integration.</p>
    </div>
  </div>
</div>

## Editor Support

You can get code coloring, autocomplete, and syntax linting directly inside **VS Code** by searching for the official `Zephyr Language` extension on the marketplace, which hooks straight into `zephyr lsp`.
