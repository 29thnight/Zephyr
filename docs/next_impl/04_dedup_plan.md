# VM 코드 중복 로직 제거 계획

## 분석 결과: 총 ~533줄 중복

### 1. R_CALL/R_RETURN 핸들러 중복 (~250줄)
- `execute_register_bytecode` / `resume_register_coroutine_fast` / `execute_coroutine_iterator`에 동일한 ~120줄 핸들러 반복
- **수정**: `handle_r_call()` / `handle_r_return()` 헬퍼 함수로 추출

### 2. Frame Stack Push/Pop 중복 (~120줄)
- Same-function recursion push, Cross-function push, Pop+restore 패턴이 4-8곳에서 반복
- **수정**: `push_call_frame_same()` / `push_call_frame_cross()` / `pop_call_frame()` 추출

### 3. Chunk 포인터 리로딩 (~40줄)
- instructions_ptr/hot_instrs/metadata_ptr/constants_ptr 재설정이 6줄씩 반복
- **수정**: `reload_chunk_pointers(ctx)` 인라인 헬퍼

### 4. ZDispatchState 초기화 (~55줄)
- 55줄 초기화 블록 + CDispatchHelpers 구조체
- **수정**: `init_c_dispatch_state()` 팩토리 함수

### 5. Global Read Helper (~30줄)
- 동일한 `read_global_value` 람다가 2곳에 정의
- **수정**: `resolve_global_slot()` 멤버 함수로 추출

### 6. Cross-File Unpack 함수 (~30줄)
- C++ `unpack_r_*()` vs C `unpack_*()` — 동일 로직, 다른 이름
- **수정**: 의도적 미러링이므로 주석으로 명시 (C는 독립 컴파일 필요)

### 7. hot_instructions 빌드 (~8줄)
- `ensure_hot_instructions()` 3회 호출
- **수정**: 버전 카운터로 중복 빌드 방지

## 우선순위
1. Chunk 포인터 리로딩 (낮은 위험, 즉시 가능)
2. Frame push/pop 헬퍼 (중간 위험)
3. R_CALL/R_RETURN 추출 (높은 위험 — 성능 영향 주의)
4. ZDispatchState 팩토리 (낮은 위험)
