# 코루틴 성능 최적화

## 브랜치
현재 브랜치: `master`

## 현재 성능
- coroutine_yield_resume: 254,270 ns total (~635 ns/resume, 401 resumes)
- 목표: ~300~400 ns/resume (목표 달성 시 ~120,000~160,000 ns total)

## 빌드 및 테스트 명령
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
x64\Release\zephyr_tests.exe
```

---

## 최적화 ①: 레지스터 파일 고정 배열화

### 현재 구조 파악
`src/zephyr_gc.inl` 또는 `src/zephyr_compiler.inl`에서 `CoroutineFrameState` 구조체와 `reg_file` 필드를 읽어라.

### 구현

1. **SmallVector 또는 고정 배열 도입**:
   ```cpp
   // 현재
   struct CoroutineFrameState {
       std::vector<Value> reg_file;
       // ...
   };

   // 개선: 64개 이하 레지스터는 인라인 저장 (heap 할당 없음)
   struct CoroutineFrameState {
       static constexpr int kInlineRegs = 64;
       uint8_t reg_count = 0;
       Value inline_regs[kInlineRegs];  // 인라인 저장소
       std::vector<Value> overflow_regs; // 64개 초과 시만 사용
       // ...
   };
   ```

2. **yield 시 레지스터 저장 최적화**:
   ```cpp
   // reg_count <= kInlineRegs: memcpy로 인라인 배열에 저장
   if (func->max_regs <= kInlineRegs) {
       frame->reg_count = func->max_regs;
       std::memcpy(frame->inline_regs, regs.data(), func->max_regs * sizeof(Value));
   } else {
       frame->overflow_regs.assign(regs.begin(), regs.end());
   }
   ```

3. **resume 시 레지스터 복원 최적화**:
   ```cpp
   if (frame->reg_count > 0 && frame->reg_count <= kInlineRegs) {
       std::memcpy(regs.data(), frame->inline_regs, frame->reg_count * sizeof(Value));
   } else {
       regs = frame->overflow_regs;
   }
   ```

4. **execute_register_bytecode_coro() 내부**:
   - `regs`를 `std::vector<Value>` 대신 고정 크기로 선언 가능한지 확인
   - 함수의 `max_regs`가 64 이하이면 스택에 직접 할당:
   ```cpp
   Value reg_storage[64]; // 스택 할당
   Value* regs = reg_storage;
   // max_regs > 64 시: heap 할당 fallback
   ```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과
- 벤치마크 확인

---

## 최적화 ②: 코루틴 프레임 메모리 풀링

### 구현

1. **CoroutineFramePool 추가** (`src/zephyr_gc.inl`):
   ```cpp
   struct CoroutineFramePool {
       static constexpr size_t kPoolSize = 64;
       std::vector<CoroutineFrameState*> free_frames;

       CoroutineFrameState* acquire() {
           if (!free_frames.empty()) {
               auto* f = free_frames.back();
               free_frames.pop_back();
               f->reset(); // 초기화
               return f;
           }
           return new CoroutineFrameState();
       }

       void release(CoroutineFrameState* f) {
           if (free_frames.size() < kPoolSize) {
               free_frames.push_back(f);
           } else {
               delete f;
           }
       }
   };
   ```

2. **VM에 풀 인스턴스 추가**:
   ```cpp
   CoroutineFramePool coroutine_frame_pool_;
   ```

3. **코루틴 생성 시 풀에서 획득**:
   - `spawn_coroutine()` 또는 코루틴 프레임 생성 코드에서
   - `new CoroutineFrameState()` → `coroutine_frame_pool_.acquire()`

4. **코루틴 소멸 시 풀에 반환**:
   - GC에서 코루틴 수집 시 → `coroutine_frame_pool_.release(frame)`
   - 단, GC 트레이스 중에는 안전한 시점에서만 반환

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## 최적화 ③: Resume 진입 경로 단순화

### 현재 구조 파악
`src/zephyr_gc.inl`에서 `resume_coroutine_*()` 함수 읽기.
특히 레지스터 모드 진입 시 거치는 체크 항목들 확인.

### 구현

1. **레지스터 모드 fast path 추가**:
   ```cpp
   Value resume_coroutine(CoroutineObject* coro, Value arg) {
       auto* frame = coro->top_frame();

       // Fast path: 레지스터 모드 코루틴 (가장 흔한 경우)
       if (LIKELY(frame->uses_register_mode && !frame->is_nested_call)) {
           return execute_register_bytecode_coro_resume(frame, arg);
       }

       // Slow path: 스택 모드 또는 nested call
       return resume_coroutine_full(coro, arg);
   }
   ```

2. **execute_register_bytecode_coro_resume() 추가**:
   - 코루틴 재진입 전용 함수 (체크 최소화)
   - 레지스터 복원 → ip 복원 → dispatch 루프 바로 진입
   - 불필요한 GC safepoint 체크, epoch 업데이트 등 skip (yield point에서만 수행)

3. **LIKELY/UNLIKELY 힌트 추가**:
   ```cpp
   #define LIKELY(x)   __builtin_expect(!!(x), 1)  // GCC/Clang
   // MSVC: [[likely]] (C++20) 또는 __assume()
   ```
   MSVC의 경우:
   ```cpp
   #if defined(_MSC_VER)
   #define LIKELY(x) (x)
   #define UNLIKELY(x) (x)
   #else
   #define LIKELY(x) __builtin_expect(!!(x), 1)
   #define UNLIKELY(x) __builtin_expect(!!(x), 0)
   #endif
   ```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## 최적화 ④: 저장 상태 최소화 (Live Register Save)

### 구현

1. **BytecodeFunction에 yield point별 live register 정보 추가**:
   ```cpp
   struct YieldPointInfo {
       size_t ip_index;          // yield instruction 위치
       uint8_t live_reg_count;   // 살아있는 레지스터 수
       uint8_t live_regs[64];    // 살아있는 레지스터 인덱스 목록
   };
   std::vector<YieldPointInfo> yield_points;
   ```

2. **컴파일 시 yield point별 live set 계산**:
   - R_YIELD 전후로 실제로 사용되는 레지스터만 기록
   - 예: `i`, `total`만 yield 이후 사용되면 그 두 레지스터만 저장

3. **yield 시 live 레지스터만 저장**:
   ```cpp
   case R_YIELD: {
       const auto& yp = func->yield_points[yield_idx];
       frame->reg_count = yp.live_reg_count;
       for (int i = 0; i < yp.live_reg_count; i++) {
           frame->inline_regs[i] = regs[yp.live_regs[i]];
       }
       // ...
   }
   ```

4. **resume 시 live 레지스터만 복원**:
   ```cpp
   const auto& yp = func->yield_points[frame->yield_idx];
   for (int i = 0; i < yp.live_reg_count; i++) {
       regs[yp.live_regs[i]] = frame->inline_regs[i];
   }
   ```

   단순 구현이 어려우면 대안:
   - yield 시 전체 저장하되, Value::nil()인 레지스터는 스킵
   ```cpp
   // nil 레지스터는 저장/복원 불필요 (nil이 기본값)
   for (int i = 0; i < max_regs; i++) {
       if (!regs[i].is_nil()) frame->inline_regs[i] = regs[i];
   }
   ```

### 검증
- `msbuild` + `zephyr_tests.exe` 통과

---

## 최종 벤치마크

```powershell
x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
x64\Release\zephyr_bench.exe --output bench\results\coroutine_optimized.json
```

목표:
- coroutine_yield_resume: 254,270 ns → ~120,000~160,000 ns (~300~400 ns/resume)
- hot_arithmetic: 2.17ms 유지 (퇴보 없음)
- 5/5 PASS 유지

## 주의사항
- 각 최적화 완료 후 빌드 + 테스트 필수
- 코루틴 테스트 전체 (nested yield, GC stress, suspend/resume 등) 통과 필수
- 퇴보 발생 시 해당 최적화만 되돌리고 나머지 적용
- 브랜치: master (직접 커밋)
