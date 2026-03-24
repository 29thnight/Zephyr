# Wave E2 — Advanced Language Features & Tooling

Working directory: C:\Users\lance\OneDrive\Documents\Project Zephyr

## Context
Project Zephyr C++20 game scripting VM. Wave E1 complete (f-string, optional chaining, class binder, profiler).
Key files: src/zephyr_lexer.inl, src/zephyr_parser.inl, src/zephyr_compiler.inl, src/zephyr_gc.inl,
include/zephyr/api.hpp, tests/tests.cpp, docs/process.md.

Implement all 4 Wave E2 tasks in order.

---

## Task 5.3 — Pattern Matching Extensions

### Goal
Extend existing match/pattern syntax with: guard clauses (if cond), OR patterns (pat1 | pat2), and literal patterns (numbers, bools, nil).

### Steps
1. Read src/zephyr_lexer.inl, src/zephyr_parser.inl — find existing match/case AST nodes and parser.
2. Add guard support: after a pattern, allow optional `if <expr>` clause. AST: add `guard_expr` field (optional) to match arm node.
3. Add OR patterns: `case X | Y | Z:` — parse as a list of patterns, match if any matches.
4. Add literal patterns: integer, float, bool, nil literals as match patterns (likely already partially there; extend if not).
5. Update compiler to emit guard check: after pattern match succeeds, evaluate guard; if false, fall through to next arm.
6. Build, test. Add tests: match with guard, match with OR pattern, match nil/bool literal.

---

## Task 5.4 — Traits / Interfaces

### Goal
Add trait definitions and impl blocks so types can declare and implement interfaces.

### Syntax
```
trait Drawable {
    fn draw(self) -> nil;
    fn bounds(self) -> [int, int];
}

impl Drawable for Player {
    fn draw(self) { ... }
    fn bounds(self) { return [self.x, self.y]; }
}
```

### Steps
1. Read lexer/parser — add TokenType::Trait, TokenType::Impl, TokenType::For keywords if missing.
2. Add AST nodes: TraitDecl (name, method signatures), ImplDecl (trait_name, for_type, methods).
3. Parser: parse trait/impl blocks at top level.
4. Runtime representation: TraitObject (map of method name -> function), ImplRegistry (type name -> trait name -> TraitObject).
5. Compiler: register trait definitions and impl blocks at module load time. For trait method calls, look up impl registry at runtime.
6. VM: add opcode or use existing dispatch to call trait method by name + self type.
7. Build, test. Add tests: define trait, implement for struct, call trait method.

---

## Task 6.3 — Debug Adapter Protocol (DAP)

### Goal
Add ZephyrVM::start_dap_server(port) that listens for DAP connections and supports breakpoints, step, continue, variable inspection.

### Steps
1. Read include/zephyr/api.hpp — find ZephyrVM public interface.
2. Add minimal DAP support to api.hpp:

```cpp
struct ZephyrBreakpoint { std::string source_file; uint32_t line; };

class ZephyrVM {
    // existing...
    void start_dap_server(uint16_t port = 4711);
    void stop_dap_server();
    void set_breakpoint(const ZephyrBreakpoint& bp);
    void clear_breakpoints();
};
```

3. Implement using TCP socket (Windows: WinSock2). DAP messages are JSON over TCP with Content-Length header.
4. Support minimal DAP requests: initialize, launch/attach, setBreakpoints, configurationDone, continue, next (step over), stackTrace, scopes, variables.
5. In VM dispatch loop, add breakpoint check: if current instruction's source line matches a breakpoint, pause and enter debug event loop.
6. Variable inspection: serialize current frame's local variables as DAP Variable objects.
7. Build, test. The DAP server should compile and start without crashing. A basic smoke test is sufficient (no live debugger connection required).

---

## Task 6.4 — VM State Serialization / Snapshot

### Goal
Add ZephyrVM::snapshot() / restore_snapshot() to save and restore full VM state for save games or rewind.

### Steps
1. Read include/zephyr/api.hpp — find existing serialize/deserialize APIs.
2. Add to api.hpp:

```cpp
struct ZephyrSnapshot {
    std::vector<uint8_t> data;
    std::string version;
    uint64_t timestamp_utc;
};

class ZephyrVM {
    // existing...
    ZephyrSnapshot snapshot() const;
    bool restore_snapshot(const ZephyrSnapshot& snap);
};
```

3. Serialize VM state: global environment bindings (non-function values), GC heap objects reachable from globals (arrays, structs — skip host handles), call stack is NOT saved (snapshot is taken at quiescent state).
4. Use a simple binary format: magic bytes + version + serialized values.
5. restore_snapshot: clear current globals, deserialize and restore. Host handles become invalid — skip or null them out.
6. Build, test. Add tests: snapshot basic globals, restore, verify values match.

---

## Build & Verify after all 4 tasks

1. msbuild Zephyr.sln /p:Configuration=Release /p:Platform=x64 /m
2. x64\Release\zephyr_tests.exe
3. x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
4. Update docs/process.md — mark 5.3, 5.4, 6.3, 6.4 all as complete.
