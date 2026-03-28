# H.0 Register-VM 추가 최적화 지시

## 브랜치
현재 브랜치: `feature/register-vm`

## 현재 성능
- hot_arithmetic_loop: 2.73ms (~6.8 ns/op)
- 목표: ~4~5 ns/op (Lua 5.4 수준)

## 빌드 및 테스트 명령
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## 최적화 ② R_* Superinstruction 추가

레지스터 VM에서 자주 연속되는 R_* opcode 쌍을 단일 dispatch로 합친다.

### 현재 R_* opcode 확인
먼저 `src/zephyr_compiler.inl`에서 현재 R_* opcode 정의를 읽어라.
`src/zephyr_gc.inl`의 `execute_register_bytecode()` dispatch 루프를 읽어라.

### 추가할 Superinstruction

1. **R_SI_ADD_STORE** — R_ADD + R_STORE_LOCAL 결합:
   ```
   // 기존: R_ADD dst, s1, s2  +  (dst는 이미 결과)
   // 패턴: 산술 결과를 바로 다른 슬롯에 저장하는 경우
   R_SI_ADD_STORE dst, src1, src2
   // locals[dst] = locals[src1] + locals[src2] (인라인)
   ```

2. **R_SI_SUB_STORE** — R_SUB + 저장 결합

3. **R_SI_MUL_STORE** — R_MUL + 저장 결합

4. **R_SI_CMP_JUMP_FALSE** — 비교 + 조건부 점프 결합:
   ```
   R_SI_CMP_JUMP_FALSE src1, src2, offset
   // if (locals[src1] < locals[src2]) ip += offset
   // 또는 if (!(locals[src1] == locals[src2])) ip += offset
   ```
   - 현재 R_LT + R_JUMP_IF_FALSE 두 개 → 하나로 합침

5. **R_SI_LOAD_ADD_STORE** — R_LOAD_CONST + R_ADD + R_MOVE 결합:
   ```
   R_SI_LOAD_ADD_STORE dst, local_src, const_idx
   // locals[dst] = locals[local_src] + constants[const_idx]
   ```
   - 루프 카운터 증가 패턴 (`i = i + 1`)에서 자주 발생

### optimize_register_bytecode() 함수 추가:
`src/zephyr_compiler.inl` 또는 `src/zephyr_gc.inl`에:
```cpp
void optimize_register_bytecode(BytecodeFunction* func) {
    // func->uses_register_mode가 true인 경우만 처리
    // 연속된 opcode 패턴 검사 후 superinstruction으로 교체
    // 교체 시 superinstruction_fusions 카운터 증가
}
```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과
- 벤치마크 실행해서 hot_arithmetic 개선 확인:
  ```powershell
  x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
  ```

---

## 최적화 ① PGO 재적용 (Register VM용)

Register VM 코드에 대해 새 PGO 프로파일을 수집해 switch dispatch 분기 예측을 개선한다.

### 확인
`ZephyrRuntime.vcxproj`에 `/GL` + `/LTCG` 설정이 있는지 확인.

### 구현

1. **PGO Instrument 빌드**:
   ```powershell
   & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGInstrument /v:minimal
   ```

2. **프로파일 수집** (Register VM 워크로드로):
   ```powershell
   # 벤치마크 실행으로 프로파일 데이터 수집
   x64\Release\zephyr_bench.exe
   ```
   - `.pgc` 파일이 생성됨 (`x64\Release\*.pgc`)

3. **PGO Optimize 빌드**:
   ```powershell
   & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /p:WholeProgramOptimization=PGOptimize /v:minimal
   ```

4. **PGO 빌드 실패 시 대안**: `/GL` + `/LTCG`만으로 일반 Release 빌드:
   ```powershell
   & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
   ```
   - `ZephyrRuntime.vcxproj`에 `<WholeProgramOptimization>true</WholeProgramOptimization>` 확인

### 검증
- `msbuild` + `zephyr_tests.exe` 통과
- 벤치마크 비교

---

## 최적화 ④ 레지스터 할당 개선

현재 단순 선형 할당을 개선해 레지스터 재사용을 최적화한다.

### 현재 구조 파악
`src/zephyr_compiler.inl`에서 `RegisterAllocator` 클래스와 `compile_expr_r()`, `compile_stmt_r()` 함수를 읽어라.

### 구현

1. **Live Range 분석 추가**:
   ```cpp
   struct LiveRange {
       int start;  // 첫 사용 instruction index
       int end;    // 마지막 사용 instruction index
       uint8_t reg;
   };
   ```

2. **임시 레지스터 재사용**:
   - expression 평가 중 임시 레지스터의 live range가 끝나면 즉시 해제
   - 현재: 함수 내내 임시 레지스터 보유 → 개선: 사용 후 즉시 반환
   ```cpp
   // RegisterAllocator::free_temp() 즉시 호출하도록 컴파일러 수정
   ```

3. **상수 로드 최적화**:
   - 같은 상수를 반복 사용 시 한 번만 R_LOAD_CONST → 레지스터 재사용
   - 루프 내 상수는 루프 진입 전에 미리 로드

4. **Move 제거 (Copy Propagation)**:
   - `R_MOVE dst, src` 다음에 `src`가 더 이상 안 쓰이면 → 후속 opcode의 src를 dst로 직접 대체

5. **최대 레지스터 수 최소화**:
   - `func->max_regs` 계산 개선 → 레지스터 파일 크기 감소
   - 기대 효과: 캐시 지역성 개선

### 검증
- `msbuild` + `zephyr_tests.exe` 통과
- 벤치마크 비교

---

## 최적화 ⑤ 코루틴 레지스터 VM 통합

코루틴이 포함된 함수도 레지스터 VM 경로를 사용하도록 Phase 5를 구현한다.

### 현재 상태 파악
- `src/zephyr_compiler.inl`에서 코루틴 가드 (`uses_register_mode = false` 설정 부분) 읽기
- `src/zephyr_gc.inl`에서 코루틴 resume/yield 처리 부분 읽기

### 구현

1. **CoroutineFrame에 레지스터 파일 추가**:
   ```cpp
   struct CoroutineFrame {
       // 기존 필드들
       std::vector<Value> regs;  // 레지스터 파일 (uses_register_mode 시 사용)
       size_t ip_index;          // 현재 instruction 인덱스
       bool uses_register_mode;
   };
   ```

2. **yield 시 레지스터 저장**:
   ```cpp
   case R_YIELD: {
       Value yield_val = regs[instr.src1];
       // 현재 ip, regs 전체를 CoroutineFrame에 저장
       frame->regs = regs;
       frame->ip_index = ip - func->instructions.data();
       return yield_val;
   }
   ```

3. **resume 시 레지스터 복원**:
   ```cpp
   // resume 시 frame->uses_register_mode 체크
   if (frame->uses_register_mode) {
       regs = frame->regs;
       ip = func->instructions.data() + frame->ip_index;
       // execute_register_bytecode 루프 재진입
   }
   ```

4. **코루틴 가드 해제**:
   - 컴파일러에서 코루틴 포함 모듈도 `uses_register_mode = true`로 설정
   - (이제 코루틴 프레임이 레지스터 파일 저장/복원 지원하므로)

5. **R_YIELD opcode 추가** (없으면):
   ```cpp
   R_YIELD src   // yield locals[src]
   ```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과 (코루틴 테스트 포함)
- 벤치마크: coroutine_yield_resume 개선 확인
  ```powershell
  x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
  ```
- 목표: coroutine 878 ns → ~300~400 ns

---

## 최종 완료 후 처리

1. **전체 빌드 + 테스트**:
   ```powershell
   & 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
   x64\Release\zephyr_tests.exe
   ```

2. **최종 벤치마크**:
   ```powershell
   x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
   x64\Release\zephyr_bench.exe --output bench\results\wave_h0_optimized.json
   ```

3. **process.md 업데이트**:
   ```
   | H.0 | Register-VM 최적화 | ✅ 완료 | R_SI superinstruction, PGO, 레지스터 재사용, 코루틴 통합 |
   ```

## 주의사항
- 각 최적화 단계 완료 후 빌드 + 테스트 필수
- 벤치마크 게이트 5/5 PASS 유지
- ⑤ 코루틴 통합 실패 시 가드를 유지하고 나머지 최적화만 적용
- 브랜치: `feature/register-vm` (master에 merge 금지)
