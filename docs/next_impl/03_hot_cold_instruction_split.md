# CompactInstruction Hot/Cold 분리 구현 가이드

## 목표
CompactInstruction(28B)을 Hot(8B) + Cold(20B) 배열로 분리하여 icache 효율 3.5x 향상.
현재 Lua 4B vs Zephyr 28B → 분리 후 8B로 격차 축소.

## 현재 구조 (src/zephyr_compiler.hpp)
```cpp
struct CompactInstruction {      // 28 bytes
    BytecodeOp op;               // 4B — 매 opcode dispatch에 필요 (HOT)
    union {                      // 4B — 매 opcode에서 읽음 (HOT)
        int32_t operand;
        struct { uint8_t dst, src1, src2, operand_a; };
    };
    uint32_t span_line;          // 4B — 에러 시에만 (COLD)
    mutable uint32_t ic_slot;    // 4B — IC hit 시에만 (COLD)
    mutable Shape* ic_shape;     // 8B — IC hit 시에만 (COLD)
    int32_t jump_target;         // 4B — 조건 분기 시에만 (COLD)
};
```

## 목표 구조
```cpp
struct HotInstruction {          // 8 bytes — 매 dispatch에서 접근
    BytecodeOp op;
    union {
        int32_t operand;
        struct { uint8_t dst, src1, src2, operand_a; };
    };
};
// CompactInstruction은 그대로 유지 (cold 접근 + IC 쓰기용)
```

## 이전 시도에서 발생한 문제 (segfault)

`BytecodeFunction::hot_instructions` 벡터가 **일부 바이트코드 생성 경로에서만** 빌드되어
빈 벡터의 `.data()`가 nullptr → segfault.

### 반드시 hot_instructions가 생성되어야 하는 모든 경로:

1. **`compile_stack_function`** (zephyr_compiler.hpp ~line 4840)
   - `optimize_bytecode()` 후에 hot_instructions 빌드

2. **`try_compile_register_function`** (zephyr_compiler.hpp ~line 4860)
   - `optimize_bytecode()` 후에 hot_instructions 빌드

3. **`optimize_bytecode` 함수 자체** (가장 안전한 위치)
   - 이 함수 끝에서 항상 hot_instructions 재빌드하면 모든 경로 커버

4. **바이트코드 직렬화/역직렬화** (zephyr_compiler.hpp ~line 1349-1490)
   - `serialize_bytecode_function` / `deserialize_bytecode_function`
   - 역직렬화 후 hot_instructions 재빌드 필요

5. **런타임 컴파일** (`compile_bytecode_function` in zephyr_gc_impl.cpp)
   - 동적으로 바이트코드를 생성하는 경로

6. **코루틴 바이트코드** (MakeCoroutine 경유)
   - CoroutineObject의 frames.front().bytecode

7. **모듈 import** (execute_file → compile_module)
   - 모듈 바이트코드에도 적용 필요

## 안전한 구현 방법

### 방법 A: optimize_bytecode 끝에서 항상 빌드 (권장)
```cpp
void optimize_bytecode(BytecodeFunction& func) {
    // ... 기존 최적화 ...

    // 항상 hot_instructions 재빌드
    func.hot_instructions.resize(func.instructions.size());
    for (size_t i = 0; i < func.instructions.size(); ++i) {
        func.hot_instructions[i].op = func.instructions[i].op;
        func.hot_instructions[i].operand = func.instructions[i].operand;
    }
}
```

### 방법 B: Fallback 가드 (방어적)
execute_register_bytecode에서:
```cpp
const HotInstruction* hot_instrs = chunk.hot_instructions.empty()
    ? nullptr : chunk.hot_instructions.data();
// dispatch loop에서:
const auto& instruction = hot_instrs
    ? hot_instrs[ip]
    : reinterpret_cast<const HotInstruction&>(instructions_ptr[ip]);
```
→ 성능 저하 가능성 있으므로 방법 A 선호.

### 방법 C: BytecodeFunction 생성자에서 보장
```cpp
// BytecodeFunction에 메서드 추가:
void ensure_hot_instructions() {
    if (hot_instructions.size() != instructions.size()) {
        hot_instructions.resize(instructions.size());
        for (size_t i = 0; i < instructions.size(); ++i) {
            hot_instructions[i].op = instructions[i].op;
            hot_instructions[i].operand = instructions[i].operand;
        }
    }
}
```
execute_register_bytecode 진입 시 한번 호출.

## dispatch loop 변경 (src/zephyr_gc_impl.cpp)

### execute_register_bytecode:
```cpp
// 추가:
const HotInstruction* __restrict hot_instrs = chunk.hot_instructions.data();

// 변경:
// OLD: const CompactInstruction& instruction = instructions_ptr[ip];
// NEW: const HotInstruction& instruction = hot_instrs[ip];

// cold 접근 시 (에러, IC, jump_target):
// instruction_span(instructions_ptr[ip])  — span_line
// instructions_ptr[ip].ic_slot            — IC
// instructions_ptr[ip].jump_target        — 조건 분기
```

### 주의: instruction_span() 호출 위치 구분
- `execute_register_bytecode` → `instruction_span(instructions_ptr[ip])`
- `execute_bytecode_chunk` (스택 모드) → `instruction_span(instruction)` (CompactInstruction)
- `resume_register_coroutine_fast` → `instruction_span(instrs_ptr[local_ip])`

전역 replace 금지! 각 함수별로 수동 변경.

## C dispatch 변경 (src/zephyr_vm_dispatch.h/c)

### ZHotInstruction 추가:
```c
typedef struct {
    int32_t op;
    union {
        int32_t operand;
        struct { uint8_t dst, src1, src2, operand_a; };
    };
} ZHotInstruction;
```

### ZDispatchState에 추가:
```c
const ZHotInstruction* hot_instructions;  // hot array
const ZInstruction* cold_instructions;    // cold array (IC/jump_target)
```

### dispatch loop:
```c
// OLD: const ZInstruction* i = &insns[ip];
// NEW: const ZHotInstruction* i = &s->hot_instructions[ip];
// cold 접근: const ZInstruction* ci = &s->cold_instructions[ip];
```

## 검증 체크리스트
- [ ] `optimize_bytecode` 끝에서 hot_instructions 빌드
- [ ] 직렬화/역직렬화에서 hot_instructions 재빌드
- [ ] execute_register_bytecode에서 hot_instrs 사용
- [ ] 스택 모드 executor는 변경 없음 확인
- [ ] 코루틴 executor (resume_register_coroutine_fast)에서 hot 사용 여부 결정
- [ ] C dispatch에서 ZHotInstruction 사용
- [ ] static_assert(sizeof(HotInstruction) == 8)
- [ ] static_assert(sizeof(HotInstruction) == sizeof(ZHotInstruction))
- [ ] 전체 벤치마크 스크립트 동작 확인 (fib, closure, coroutine, struct, entity_update 등)
- [ ] MSVC + Clang 양쪽 빌드 확인

## 예상 효과
- icache: 28B → 8B per instruction (3.5x 감소)
- fibonacci, hot_loop, vector_math 등 opcode-intensive 벤치에서 10-20% 개선 예상
- 특히 MSVC switch dispatch에서 효과 클 것 (icache miss 감소)
