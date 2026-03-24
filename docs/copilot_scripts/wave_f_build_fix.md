# Wave F — 빌드 수정 및 완료

## 상황
Wave F 구현이 이전 세션에서 +830줄 변경과 함께 대부분 완료됐지만, 빌드 검증 단계에서 중단됐다.
vcxproj 파일 수정 작업 중 세션이 종료됐으므로, 지금 빌드가 실패할 수 있다.

## 목표
1. Release x64 빌드 성공
2. `zephyr_tests.exe` 전체 통과
3. 벤치마크 실행 및 결과 저장
4. process.md Wave F 항목 ✅ 완료로 업데이트

## MSBuild 경로
```
C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe
```
또는 vswhere로 탐색:
```powershell
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe'
```

## Step 1: 빌드 시도
```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' Zephyr.sln /p:Configuration=Release /p:Platform=x64 /v:minimal
```

빌드 실패 시 오류를 보고 수정한다. 일반적인 오류 유형:
- 선언 없는 함수/구조체 → api.hpp 또는 관련 .inl에 선언 추가
- 재정의 오류 → 헤더 가드 확인 또는 중복 선언 제거
- vcxproj AdditionalIncludeDirectories 누락 → 각 .vcxproj에 추가

## Step 2: 테스트 실행
```powershell
x64\Release\zephyr_tests.exe
```
실패 시 오류 수정 후 재빌드/재테스트.

## Step 3: 벤치마크 실행
```powershell
x64\Release\zephyr_bench.exe --baseline bench\results\wave_d_baseline.json
```

## Step 4: 벤치마크 저장
```powershell
x64\Release\zephyr_bench.exe --save bench\results\wave_f_baseline.json
```

## Step 5: process.md 업데이트
`docs\process.md`의 Wave F 섹션에서 모든 항목을 ✅ 완료로 변경:

```
| F.1 | PGO (Profile-Guided Optimization) | ✅ 완료 | /GL+/LTCG 설정 + run_pgo_build.ps1 작성 |
| F.2 | 모듈 바이트코드 캐싱 | ✅ 완료 | enable_bytecode_cache(), 파일 mtime 기반 무효화 |
| F.3 | Superinstruction 계측 | ✅ 완료 | fusion 카운터, 벤치 hit rate 포함 |
| F.4 | GC Pause Time 계측 | ✅ 완료 | p50/p95/p99, frame_budget_miss 카운터 |
| F.5 | GC 이벤트 스트림 Export | ✅ 완료 | start/stop_gc_trace(), get_gc_trace_json() |
| F.6 | Coroutine Flame/Trace | ✅ 완료 | Created/Resumed/Yielded/Completed/Destroyed 이벤트 |
```

또한 하단 _마지막 업데이트_ 줄을 업데이트:
```
_마지막 업데이트: 2026-03-24 — Wave F 완료. 벤치 결과는 wave_f_baseline.json 참조._
```

## 주의사항
- 기존 5/5 벤치마크 게이트 유지 필수
- 각 새 기능(GC trace, coroutine trace)은 비활성 시 overhead 0 확인
