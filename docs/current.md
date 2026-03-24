# Zephyr 현재 구현 스냅샷

## 1. 개요

`Project Zephyr`는 C++로 구현된 스크립팅 언어 런타임/VM 프로젝트다. 저장소는 Visual Studio 솔루션(`Zephyr.sln`)과 여러 `.vcxproj` 프로젝트로 구성되어 있으며, 스크립트 실행기, 벤치마크, 테스트, 임베딩 샘플을 함께 제공한다.

- **주요 언어/도구**: C++, Visual Studio, MSBuild
- **런타임 성격**: GC 관리형 스크립트 VM
- **목표 사용처**: 게임/호스트 애플리케이션 임베딩
- **핵심 구현 파일**: `src\zephyr.cpp`, `src\zephyr_types.inl`, `src\zephyr_lexer.inl`, `src\zephyr_parser.inl`, `src\zephyr_compiler.inl`, `src\zephyr_gc.inl`

`README.md`와 `include\zephyr\api.hpp` 기준으로 보면, 이 프로젝트는 단순 인터프리터 수준을 넘어 바이트코드 실행, 코루틴, 호스트 핸들 정책, 직렬화, GC 진단, 프로파일링/디버깅 보조 API까지 포함한 비교적 큰 런타임이다.

## 2. 프로젝트 구조

- **`src\`**
  런타임 핵심 구현이 있다. `zephyr.cpp`가 중심 번역 단위이고, 주요 서브시스템이 `.inl` 파일로 분리되어 포함된다.

- **`include\zephyr\api.hpp`**
  외부 애플리케이션이 Zephyr VM을 임베딩할 때 사용하는 공용 API다. 스크립트 실행, 함수 호출, 호스트 객체 바인딩, 코루틴 제어, GC 제어, 직렬화, 프로파일링, 스냅샷 API가 선언되어 있다.

- **`ZephyrRuntime.vcxproj`**
  정적 라이브러리 `ZephyrRuntime.lib`를 빌드한다.

- **`zephyr_cli.vcxproj`**
  CLI 실행 파일 `zephyr_cli.exe`를 빌드한다. 단순 실행기만이 아니라 `run`, `check`, `repl`, `stats`, `dump-bytecode`, `bench` 하위 명령을 제공한다.

- **`zephyr_bench.vcxproj`**
  별도 벤치마크 실행 파일 `zephyr_bench.exe`를 빌드한다. 결과는 기본적으로 `bench\results\latest.json`에 기록된다.

- **`zephyr_tests.vcxproj`**
  테스트 실행 파일 `zephyr_tests.exe`를 빌드한다. 언어 기능, GC, 직렬화, 코루틴, 바이트코드 경로, 벤치마크 게이트, 프로파일링 등 폭넓은 회귀 테스트가 `tests\tests.cpp`에 모여 있다.

- **`samples\engine_sample\`**
  Zephyr를 호스트 애플리케이션에 임베딩하는 실제 예제다. `ZephyrHostClass`, 모듈 등록, 전역 함수 등록, 스크립트 함수 호출 흐름을 보여 준다.

- **`examples\`**
  Zephyr 스크립트 예제가 있다. `arrays.zph`, `closures.zph`, `loops.zph`, `structs.zph`, `match_values.zph`, `import_main.zph`, `host_module.zph`, `event_handling.zph`, `state_machine.zph`, `simple_ai.zph` 등으로 언어 기능과 호스트 연동 방식을 확인할 수 있다.

- **`bench\`**
  벤치마크 러너와 JSON 리포트 로직이 있다. baseline 비교와 strict gate 평가를 지원한다.

- **`docs\`**
  현재 문서를 포함한 설계/작업 메모와 운영 스크립트가 있다.

- **`x64\Debug`, `x64\Release`**
  현재 저장소에는 `zephyr_cli.exe`, `zephyr_bench.exe`, `zephyr_tests.exe`, `zephyr_engine_sample.exe`, `ZephyrRuntime.lib` 출력물이 존재한다.

## 3. 빌드와 실행

### 빌드

- Visual Studio에서 `Zephyr.sln`을 열고 솔루션 전체를 빌드할 수 있다.
- 또는 `README.md` 기준으로 다음과 같이 빌드할 수 있다.

```powershell
MSBuild.exe Zephyr.sln /p:Configuration=Debug /p:Platform=x64
```

### CLI 사용

`cli\main.cpp` 기준으로 CLI는 다음 명령을 제공한다.

```text
zephyr run <file>
zephyr check <file>
zephyr repl
zephyr stats <file>
zephyr dump-bytecode <file> [function]
zephyr bench [output.json] [--baseline <json>] [--strict]
```

예시:

```powershell
x64\Debug\zephyr_cli.exe run examples\loops.zph
x64\Debug\zephyr_cli.exe check examples\import_main.zph
x64\Debug\zephyr_cli.exe stats examples\engine_demo.zph
x64\Debug\zephyr_cli.exe dump-bytecode examples\import_main.zph main
```

현재 구현에서 이전 문서처럼 `zephyr_cli.exe examples\loops.zph` 형태로 바로 실행하는 인터페이스는 맞지 않는다. `run` 서브커맨드를 붙여야 한다.

### 벤치마크

- `zephyr_cli.exe bench ...`와 `zephyr_bench.exe` 둘 다 벤치마크 스위트를 실행할 수 있다.
- 기본 출력 경로는 `bench\results\latest.json`이다.
- `bench\bench_runner.hpp`와 `bench\bench_runner.cpp` 기준으로 baseline 비교와 strict gate를 지원하며, 기본 baseline 경로는 `bench\results\v1_baseline.json`이다.

## 4. 현재 언어/런타임 기능 요약

`README.md`, `tests\tests.cpp`, `src\` 구현을 종합하면 현재 구현 상태는 다음과 같다.

- Rust 스타일 문법을 따르는 스크립트 언어
- `fn`, `let`, `mut`, `struct`, `enum`, `match`, `trait`, `impl`
- `break`, `continue`, `+=`, `-=`, `*=`, `/=`
- 배열, 문자열, 클로저, 구조체, 열거형
- `import`/`export` 기반 모듈 로딩
- 코루틴 문법과 런타임(`yield`, `resume`, `.done`, `.suspended`)
- 선택적 체이닝 `?.`
- 문자열 보간(f-string)
- 호스트 모듈 등록과 호스트 객체 바인딩
- 직렬화/역직렬화와 저장용 envelope 스키마
- 런타임 통계, 바이트코드 덤프, 벤치마크 게이트, 프로파일링 리포트

## 5. 공용 API와 호스트 통합

`include\zephyr\api.hpp`에는 다음과 같은 공개 기능이 있다.

- **스크립트 검증/실행**
  - `check_string`, `check_file`
  - `execute_string`, `execute_file`
  - `get_function`, `call`

- **호스트 통합**
  - `register_module`
  - `register_global_function`
  - `make_host_object`
  - `bind<T>()`

- **직렬화/콜백**
  - `call_serialized`
  - `serialize_value`, `deserialize_value`
  - `capture_callback`, `release_callback`

- **코루틴 제어**
  - `spawn_coroutine`
  - `resume`
  - `cancel`
  - `query_coroutine`

- **GC/진단**
  - `gc_step`, `set_gc_stress`
  - `advance_frame`, `advance_tick`, `advance_scene`
  - `collect_young`, `collect_garbage`, `compact_old_generation`
  - `gc_verify_young`, `gc_verify_full`
  - `runtime_stats`, `debug_dump_coroutines`

- **개발 보조 기능**
  - `start_profiling`, `stop_profiling`
  - `start_dap_server`, `stop_dap_server`
  - `snapshot`, `restore_snapshot`

특히 호스트 객체는 원시 포인터를 그대로 노출하는 방식이 아니라, 세대/수명 정책을 갖는 핸들(`Frame`, `Tick`, `Persistent`, `Stable`) 기반으로 관리된다.

## 6. 핵심 구현 분석

### 6.1 `src\zephyr_types.inl`

- **`Value`**
  Zephyr의 핵심 값 타입이다. 48비트 NaN-boxing을 사용하며 `Nil`, `Bool`, `Int`, `Pointer`, `HostHandle` 등을 태그로 구분한다.

- **`GcObject` / `GcHeader`**
  GC 대상 힙 객체의 공통 기반이다. 객체 종류, 색상, 플래그, 세대/공간 정보를 담는다.

- **GC 공간 모델**
  `GcSpaceKind`에는 `Nursery`, `OldSmall`, `LargeObject`, `Pinned`가 정의되어 있으며, `EnvArena`, `CoroArena`는 예약 상태다.

- **카드 기반 쓰기 장벽 보조 구조**
  값 카드(span 16)와 비트맵 기반 dirty-card 유틸리티가 존재한다. 이는 minor GC에서 old-to-young 참조를 더 좁은 범위로 추적하기 위한 기반이다.

- **런타임 객체 종류**
  `StringObject`, `ArrayObject`, `ScriptFunctionObject`, `StructTypeObject`, `StructInstanceObject`, `EnumInstanceObject`, `CoroutineObject`, `ModuleNamespaceObject`, `UpvalueCellObject` 등이 구현돼 있다.

### 6.2 `src\zephyr_lexer.inl`

- 토큰 스트림 생성기 역할을 한다.
- 공백과 `//` 한 줄 주석을 건너뛴다.
- 단일/복합 연산자 토큰을 스캔한다.
- 문자열, 숫자, 식별자, 예약어를 처리한다.
- f-string 스캔 로직이 있다.
- 현재 구현은 `+=`, `-=`, `*=`, `/=`, `&&`, `||`, `%`, `?.` 같은 최신 문법 요소를 뒷받침한다.

### 6.3 `src\zephyr_parser.inl`

- 렉서 결과를 AST로 변환한다.
- **Pratt 파서라고 보기보다 우선순위 단계별 재귀 하강(precedence climbing) 구조**에 가깝다.
- `parse_or()`, `parse_and()`, `parse_equality()`, `parse_comparison()`, `parse_term()`, `parse_factor()`, `parse_unary()`, `parse_call()`, `parse_primary()` 계층으로 식 우선순위를 처리한다.
- `let`, `if`, `while`, `for`, `return`, `yield`, `import`, `export`, `trait`, `impl`, `struct`, `enum`, `match`를 파싱한다.
- 선택적 체이닝과 패턴 매칭도 포함한다.

이전 문서의 "Pratt 파싱" 설명은 현재 구현 기준으로는 부정확하다.

### 6.4 `src\zephyr_compiler.inl`

- AST를 바이트코드로 컴파일한다.
- `BytecodeOp`, `BytecodeFunction`, `CompactInstruction`, `InstructionMetadata` 등 바이트코드 실행을 위한 핵심 자료구조가 정의되어 있다.
- 전역/로컬/클로저 변수 처리와 함수/코루틴 리터럴 컴파일 경로가 있다.
- 구조체 멤버 접근 최적화를 위한 shape 기반 inline cache 필드(`ic_slot`, `ic_shape`)가 포함된다.
- 디버그 빌드에서는 일부 AST fallback 경로를 허용하는 설계 흔적이 남아 있다.

### 6.5 `src\zephyr_gc.inl`

- 증분 GC와 세대별 GC 로직의 중심 구현이다.
- 루트 환경은 pinned 공간에 놓이고, nursery/old/large object 공간이 분리되어 관리된다.
- young collection, full collection, dirty root/object 추적, remembered owner/card 기반 재스캔 흐름이 구현돼 있다.
- `compact_old_generation()` 같은 old generation compaction 경로가 공용 API에 노출된다.
- 코루틴 프레임, 업밸류, 환경 체인, 배열/구조체 필드 등 다양한 힙 루트를 정확히 추적하도록 구현되어 있다.

## 7. 테스트가 보여주는 현재 보장 범위

`tests\tests.cpp`는 단순 smoke test가 아니라 현재 기능 범위를 보여 주는 구현 지표다. 확인 가능한 테스트 범주는 다음과 같다.

- 기본 실행/함수 호출
- enum/match, guard, OR-pattern
- trait/impl 디스패치
- DAP 서버 smoke test
- snapshot/restore
- 호스트 객체 바인딩과 핸들 수명 정책
- incremental GC 진행/검증
- stdlib helper
- import 순환 오류 보고
- serialization schema 및 stable handle 제한
- 바이트코드 루프/분기/클로저/전역 바인딩 캐시
- 코루틴 lazy resume, GC 생존성, runtime stats, dump
- benchmark baseline/strict gate
- old generation compaction
- 문자열 보간, optional chaining, class binder, profiler report

즉 현재 프로젝트는 "간단한 스크립트 실행기"보다 훨씬 넓은 기능을 이미 포함한다.

## 8. 요약

현재 구현 기준으로 Zephyr는 다음 성격의 프로젝트로 보는 것이 가장 정확하다.

- C++ 기반 GC 관리형 스크립트 VM
- 바이트코드 컴파일/실행 경로를 갖춘 언어 런타임
- 코루틴, 직렬화, 호스트 핸들 정책, GC 진단까지 포함한 임베딩 지향 엔진
- CLI, 벤치마크, 테스트, 샘플을 함께 제공하는 통합 저장소

기존 `docs\current.md`는 큰 틀에서는 맞았지만, CLI 인터페이스와 실제 런타임 기능 범위를 충분히 반영하지 못했고, 파서 설명 중 일부는 현재 구현과 달랐다. 이 문서는 그 차이를 반영해 현재 코드 기준으로 다시 정리한 버전이다.
