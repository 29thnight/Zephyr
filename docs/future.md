# Zephyr — Future Direction

## Core Philosophy

**"Lua의 경량성 + 현대 문법 + 관찰 가능한 GC/코루틴 런타임"**

게임 엔진 임베딩에 최적화된 안전하고 예측 가능한 스크립팅 레이어.

## Performance Targets

| Metric | Lua 5.4 | Current Zephyr | Target |
|---|---|---|---|
| arithmetic (ns/op) | 3~5 | ~8 | 4~5 |
| coroutine resume | 200~400 ns | 593 ns | 300~400 ns |
| host resolve | — | 641 ns | < 300 ns |

## Roadmap

### Short-term

| # | Task | Notes |
|---|---|---|
| 1 | AOT compilation | bytecode → native object file, LLVM or QBE backend |
| 2 | JIT (hot path) | opcode_count threshold → ORC JIT, coroutine 제외 |
| 3 | LSP inlay hints | `textDocument/inlayHint` — 변수 타입 힌트 |
| 4 | Regression tests | zephyr_parser.cpp / zephyr_gc_impl.cpp 분리 후 TU 단위 테스트 |

### Mid-term

| # | Task | Notes |
|---|---|---|
| 5 | Package manager | `package.toml` fetch / lock / publish |
| 6 | std 라이브러리 확장 | `std/net`, `std/async`, `std/test` |
| 7 | REPL 강화 | multiline, history, tab completion |

### Long-term

| # | Task | Notes |
|---|---|---|
| 8 | Self-hosting compiler | Zephyr로 Zephyr 컴파일 |
| 9 | WASM target | Emscripten or wasm32-unknown-unknown |

## AOT/JIT Backend Comparison

| Backend | JIT | AOT | C++ 통합 | 비고 |
|---|---|---|---|---|
| LLVM | ✅ ORC | ✅ | C++ native | 빌드 무거움 (~50MB) |
| Cranelift | ✅ | ✅ | Rust FFI 필요 | 경량, Rust 툴체인 |
| QBE | ❌ | ✅ | C lib | 매우 경량 (~200KB) |
| MIR/libmir | ✅ | ❌ | C API | Red Hat, 경량 JIT 전용 |

> 결론: Gravity/Lua 같은 게임 스크립팅 VM은 JIT 없이도 충분히 실용적.
> 실제 병목 발생 시 추가하는 방향 권장.

## Non-Goals

- Borrow checking / lifetime annotations
- Async/await 패러다임 (코루틴으로 대체)
- Reflection / dynamic dispatch 외 메타프로그래밍
