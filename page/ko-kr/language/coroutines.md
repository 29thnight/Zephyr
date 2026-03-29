# Coroutines (코루틴 상태 머신)

게임 개발에서 수많은 AI 엔티티나 로직의 프레임 진행 딜레이(Wait), 조건형 이벤트 트리거를 제로-오버헤드에 가깝게 만들기 위한 코어 시스템입니다.

## `coroutine` 및 `yield`, `resume` 사용

콜스택(Call Stack) 보존이라는 제약 없이 엔진 가비지 컬렉터의 Heap 영역에 로컬 환경 변수들이 유지되며 프레임 드랍 없는 빠른 제어 역전(Yield/Resume)을 수행합니다.

```zephyr
// yield를 포함하는 상태 머신 생성 함수 선언
coroutine fn worker(limit: int) -> int {
  let mut i: int = 0;
  while i < limit {
    yield i;      // 호출자나 엔진으로 제어권을 넘기며 i를 리듀스(Suspend)
    i = i + 1;
  }
  return i;       // 파이프라인의 최종 종료(Done)
}

fn run() -> int {
  // `worker(n)` 호출 시 즉시 실행되지 않고, Coroutine Object 프레임 리터럴 반환
  let c = worker(3);
  
  let a = resume c;  // 첫 번째 yield 단계 진행 -> 0
  let b = resume c;  // 두 번째 yield 단계 진행 -> 1
  
  return a + b;      // 결과 -> 1
}
```

## 활용 정책 및 주의점
향후 `c.done`, `c.suspended` 같은 내장 메서드의 확장을 통해, C++ 호스트 틱(Tick) 단위에서 관리 중인 모든 스크립트 객체들의 진행 여부를 유연하게 파악할 수 있도록 진화하고 있습니다.
