# H.0 Register-based VM 구현 지시

## 브랜치
현재 브랜치: `feature/register-vm` (master에서 분기)

## 목표
Stack-based VM → Register-based VM 전환.
- 매 opcode의 push/pop 제거 → operand를 로컬 슬롯 인덱스로 직접 참조
- 기대 효과: ~8 ns/op → ~4~5 ns/op (Lua 5.4 수준)
- 벤치마크 게이트 5/5 PASS 유지

## 빌드 및 테스트 명령
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## Phase 1: CompactInstruction에 레지스터 필드 추가

### 현재 구조 파악
먼저 다음 파일들을 읽어 현재 구조를 파악하라:
- `src/zephyr_compiler.inl` — `CompactInstruction`, `InstructionMetadata`, 현재 opcode 정의
- `src/zephyr_gc.inl` — VM dispatch 루프 (`execute_bytecode_chunk`)
- `include/zephyr/api.hpp` — 공개 API

### 구현

1. **CompactInstruction에 레지스터 필드 추가** (`src/zephyr_compiler.inl`):
   ```cpp
   struct CompactInstruction {
       uint8_t opcode;
       uint8_t dst;   // destination register (local slot index)
       uint8_t src1;  // source register 1
       uint8_t src2;  // source register 2 (또는 immediate/const index)
       // 기존 필드들 유지 (operand_a, operand_b 등)
   };
   static_assert(sizeof(CompactInstruction) <= 24);
   ```
   - dst/src1/src2는 로컬 슬롯 인덱스 (0~255)
   - 기존 operand_a/operand_b와 union으로 공유 가능

2. **레지스터 기반 opcode 추가** (기존 stack opcode는 유지):
   새로운 레지스터 opcode prefix `R_` 추가:
   - `R_ADD` — `locals[dst] = locals[src1] + locals[src2]`
   - `R_SUB` — `locals[dst] = locals[src1] - locals[src2]`
   - `R_MUL` — `locals[dst] = locals[src1] * locals[src2]`
   - `R_DIV` — `locals[dst] = locals[src1] / locals[src2]`
   - `R_MOD` — `locals[dst] = locals[src1] % locals[src2]`
   - `R_LOAD_CONST` — `locals[dst] = constants[src1]`
   - `R_LOAD_GLOBAL` — `locals[dst] = globals[src1]`
   - `R_STORE_GLOBAL` — `globals[dst] = locals[src1]`
   - `R_MOVE` — `locals[dst] = locals[src1]`
   - `R_CALL` — `locals[dst] = call(locals[src1], args_start=src2, argc=operand_a)`
   - `R_RETURN` — `return locals[src1]`
   - `R_JUMP` — unconditional jump
   - `R_JUMP_IF_FALSE` — `if (!locals[src1]) jump`
   - `R_JUMP_IF_TRUE` — `if (locals[src1]) jump`
   - `R_LT`, `R_LE`, `R_GT`, `R_GE`, `R_EQ`, `R_NE` — 비교 연산
   - `R_NOT` — `locals[dst] = !locals[src1]`
   - `R_NEG` — `locals[dst] = -locals[src1]`

---

## Phase 2: 레지스터 할당 컴파일러

### 구현

1. **RegisterAllocator 클래스 추가** (`src/zephyr_compiler.inl`):
   ```cpp
   class RegisterAllocator {
   public:
       uint8_t alloc();        // 다음 사용 가능한 레지스터 번호 반환
       void free(uint8_t reg); // 레지스터 해제
       uint8_t alloc_temp();   // 임시 레지스터 (expression 평가용)
       void free_temp(uint8_t reg);
       int next_reg = 0;
       int max_regs = 0;
   };
   ```

2. **선형 스캔 레지스터 할당 전략** (단순화):
   - 각 로컬 변수 → 고정 레지스터 번호 (선언 순서)
   - 임시 값 → 스택 상단 임시 레지스터 pool
   - 함수 인자 → r0, r1, r2, ...
   - `this` (self) → r0

3. **컴파일러 emit 함수에 레지스터 버전 추가**:
   ```cpp
   void emit_r(uint8_t opcode, uint8_t dst, uint8_t src1, uint8_t src2 = 0);
   void emit_r_load_const(uint8_t dst, int const_idx);
   void emit_r_binop(uint8_t op, uint8_t dst, uint8_t src1, uint8_t src2);
   ```

4. **`compile_register_mode` 플래그 추가** (`BytecodeFunction`):
   ```cpp
   bool uses_register_mode = false;
   ```
   - 새로 컴파일된 함수는 `uses_register_mode = true`
   - 기존 스택 기반 바이트코드와 공존 가능 (하위 호환)

5. **expression 컴파일을 레지스터 방식으로 재구현**:
   현재 `compile_expr()` 함수를 확장해 레지스터 결과 위치를 반환:
   ```cpp
   uint8_t compile_expr_r(const ExprNode& expr, RegisterAllocator& ra);
   // 결과가 저장된 레지스터 번호 반환
   ```

6. **statement 컴파일을 레지스터 방식으로 재구현**:
   ```cpp
   void compile_stmt_r(const StmtNode& stmt, RegisterAllocator& ra);
   ```

---

## Phase 3: Register-based VM Dispatch

### 구현

`src/zephyr_gc.inl`의 `execute_bytecode_chunk()` 함수에 레지스터 dispatch 경로 추가:

```cpp
// BytecodeFunction::uses_register_mode == true 시 이 경로 사용
Value execute_register_bytecode(BytecodeFunction* func, Value* args, int argc) {
    // 레지스터 파일: locals 배열 (크기 = func->max_regs)
    std::vector<Value> regs(func->max_regs);

    // 인자 로드
    for (int i = 0; i < argc; i++) regs[i] = args[i];

    const auto* ip = func->instructions.data();
    const auto* end = ip + func->instructions.size();
    const auto& consts = func->constants;

    while (ip < end) {
        const auto& instr = *ip++;
        switch (instr.opcode) {
        case R_ADD:
            regs[instr.dst] = regs[instr.src1] + regs[instr.src2];
            break;
        case R_SUB:
            regs[instr.dst] = regs[instr.src1] - regs[instr.src2];
            break;
        // ... 등등
        case R_RETURN:
            return regs[instr.src1];
        }
    }
    return Value::nil();
}
```

**핵심 최적화**: 정수 산술에 대해 NaN-boxing 태그 체크 후 raw 연산:
```cpp
case R_ADD: {
    Value a = regs[instr.src1], b = regs[instr.src2];
    if (a.is_int() && b.is_int()) {
        regs[instr.dst] = Value::from_int(a.as_int() + b.as_int());
    } else {
        regs[instr.dst] = apply_add(a, b); // fallback
    }
    break;
}
```

---

## Phase 4: 컴파일러-VM 통합 및 테스트

### 구현

1. **`ZephyrVM::execute()` 또는 `call_value()`에서 함수 타입 체크**:
   ```cpp
   if (func->uses_register_mode) {
       return execute_register_bytecode(func, args, argc);
   } else {
       return execute_bytecode_chunk(func, args, argc); // 기존 경로
   }
   ```

2. **기본 기능 테스트 추가** (`tests/test_vm.cpp` 또는 새 `tests/test_register_vm.cpp`):
   - 단순 산술 (1 + 2, a * b + c 등)
   - 변수 할당과 읽기
   - if/else 분기
   - while 루프
   - 함수 호출
   - 재귀 함수

3. **벤치마크 비교**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   ```
   - `hot_arithmetic_loop`이 이전보다 빨라지면 성공
   - 목표: ~4~5 ns/op

---

## Phase 5: Coroutine 통합 (선택적)

레지스터 VM과 코루틴의 통합:

1. 코루틴 frame에 레지스터 파일(`std::vector<Value> regs`) 포함
2. yield 시 레지스터 파일 저장
3. resume 시 레지스터 파일 복원

코루틴이 복잡해지면 이 단계는 건너뛰고 Phase 4까지만 완료해도 됨.

---

## 완료 후 처리

1. **빌드 + 테스트**:
   ```powershell
   & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
   x64\Release\zephyr_tests.exe
   ```

2. **벤치마크 실행**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   ```

3. **벤치마크 저장**:
   ```powershell
   x64\Release\zephyr_bench.exe --output bench\results\wave_h0_register_vm.json
   ```

4. **process.md 업데이트**:
   ```
   | H.0 | Register-based VM | ✅ 완료 | R_ opcode, RegisterAllocator, execute_register_bytecode |
   ```

## 중요 주의사항

- **하위 호환성 유지**: 기존 스택 기반 VM은 제거하지 말고 공존시킬 것
  - `uses_register_mode` 플래그로 두 경로 분기
  - 기존 테스트가 모두 통과해야 함
- **단계별 구현**: Phase 1 완료 후 빌드 확인 → Phase 2 → Phase 3 → Phase 4 순서
- **빌드 실패 시**: 변경 사항을 되돌리고 더 작은 단계로 재시도
- **이 브랜치는 `feature/register-vm`**: master에 merge하지 말 것

## 성공 기준
- `x64\Release\zephyr_tests.exe` 전체 통과
- `hot_arithmetic_loop` 벤치마크 개선 (현재 3.63ms → 목표 ~2ms)
- 게이트 5/5 PASS
