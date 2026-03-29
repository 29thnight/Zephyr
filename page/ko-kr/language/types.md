# Types & Value Model

Zephyr는 동적 런타임 언어로서 유연함을 갖추면서도, 함수 규격이나 호스트 연동 시 발생하는 런타임 타입 캐스팅 구조를 안전하게 방어하도록 지원합니다.

## 기본 타입 (Primitives)
가장 작은 단위의 값 타입니다.

- `int`: 64비트 정수
- `float`: 배정밀도 부동 소수점
- `bool`: `true` / `false` 논리값
- `string`: GC를 통해 추적-수집(Garbage Collected)되는 문자열 개체
- `nil`: 할당된 내용이 없거나 실패함을 의미
- `any`: 강제 캐스팅이나 동적 우회를 허용하는 다형성 컨테이너 타입

## 복합 타입 (Complex Types)
메모리에 할당되며 가비지 컬렉터의 4단계 라이프사이클 관리(Nursery -> Old 등)를 받는 객체들입니다.

- **Array (배열)**
- **Struct / Enum Instance (구조체 및 열거형 인스턴스)**
- **Function / Closure (함수 및 클로저 참조 객체)**
- **Coroutine Object (활성화된 힙 기반 스테이트 머신)**

> [!NOTE] 런타임 타입 검증
> 엄격한 함수 시그니처(`-> int` 등) 및 구조체 초기화, C++ 네이티브 `Host Handle` 매핑 시점에 타입 추론 시스템과 VM이 강력하게 불일치를 감지해 엔진 크래시를 막고 내부 예외(`Result Error`)로 전파합니다.
