# GC 및 프로파일러 (GC & Profiler)

인게임 디버그 오버레이, 콘솔 창 등에서 스크립트의 성능 상태나 병목(Bottleneck) 구간을 모니터링하거나, 메모리 압박이 심할 때 직접 가비지 수집 메커니즘을 통제할 수 있는 시스템 직결 모듈들입니다.

## GC 인터페이스 연동 (`std/gc`)

대부분의 증분형 GC 사이클 관리는 엔진 루프(C++) 단에서 수행되지만, 로딩 스크린이나 대화형 씬 등 메모리 처리가 시급한 기점에서 스크립트 측이 강제 신호를 발생시킬 수 있습니다.

```zephyr
import "std/gc";

gc.collect_young();    // Nursery (Young generation) 전용 가벼운 수집 수행
gc.collect();          // 힙 전체를 수집하는 풀 사이클 강제 수행
gc.step(500);          // 특정 예산(예: 500 마이크로초) 내에서만 증분형 수집 전진
let s = gc.stats();    // 메모리 할당량 및 퍼즈 타임 히스토리가 담긴 상태 객체 Map 반환
```

## 프로파일러 추적기 (`std/profiler`)

엔진의 특정 부분에서 CPU 스파이크가 발생하는지 정밀하게 분석하기 위한 실행 샘플링 프로파일러를 스크립트 코드 사이클에서 직접 토글(Toggle) 할 수 있습니다. 완료 후 JSON 포맷 등으로 가시화된 보고서를 생성합니다.

```zephyr
import "std/profiler";

profiler.start(); // 기록 시작

// ... 수많은 반복 렌더링 호출이나 네트워크 파싱 ...

profiler.stop();  // 기록 중단
profiler.report(); // 표준 출력(Stdout)으로 분석 트레이스 결과 덤프 출력
```

> CLI 명령 기반의 전역 측정이 필요하다면 코드 삽입 없이 `--profile` 인자를 터미널에 주입하여 전체 프로그램 라이프사이클 측정 데이터(`zephyr_profile.json`)를 뽑아낼 수도 있습니다.

```bash
zephyr run --profile mygame.zph
```
