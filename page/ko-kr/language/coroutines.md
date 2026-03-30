# 코루틴 (Coroutines)

코루틴은 실행 중 특정 지점에서 중지(`yield`)하고, 나중에 중지된 지점부터 다시 실행할 수 있는 함수입니다. Zephyr에서 비동기 상태 머신, AI 로직 및 순차적 애니메이션을 관리하는 핵심 메커니즘입니다.

## 선언 및 생성

코루틴은 `coroutine fn` 키워드를 사용하여 선언합니다. 코루틴 함수를 호출하면 본문이 즉시 실행되는 대신, 함수의 중단된 상태를 나타내는 코루틴 객체가 반환됩니다.

```zephyr
coroutine fn task() -> void {
    print("1단계");
    yield;
    print("2단계");
}

let c = task(); // 함수가 생성되었으나 아직 실행되지는 않음
```

## 재개와 중지

실행은 `resume` 연산자가 코루틴 객체에 적용될 때만 시작되거나 계속됩니다.

```zephyr
resume c; // "1단계"를 출력하고 yield에서 중지
resume c; // "2단계"를 출력하고 리턴
```

### 값 반환 (Yield values)

`yield` 구문은 선택적으로 표현식을 포함할 수 있습니다. 이 값은 `resume` 연산의 결과로 호출자에게 반환됩니다.

```zephyr
coroutine fn counter(n: int) -> int {
    let mut i = 0;
    while i < n {
        yield i;
        i += 1;
    }
}

let c = counter(3);
print(resume c); // 0
print(resume c); // 1
print(resume c); // 2
```

## 코루틴 속성

코루틴 객체는 실행 상태를 추적하기 위해 두 가지 내장 속성을 제공합니다:

| 속성 | 타입 | 설명 |
|---|---|---|
| `.done` | `bool` | 함수가 실행을 완료(리턴)했으면 `true`를 반환합니다. |
| `.suspended` | `bool` | 함수가 현재 `yield` 지점에서 일시 중지된 상태이면 `true`를 반환합니다. |

```zephyr
while !c.done {
    resume c;
}
```

## 중첩 호출 및 Yield 전파 (Deep Yielding)

Zephyr는 딥 일딩(Deep yielding)을 지원합니다. 코루틴에서 일반 함수(`fn`)를 호출하고, 그 함수(또는 콜 스택 더 아래의 함수)에서 `yield`를 실행하면 전체 호출 체인이 중지됩니다. 코루틴 객체는 재개될 때까지 이 깊은 콜 스택 상태를 유지합니다.

```zephyr
fn helper() -> void {
    print("헬퍼 내부");
    yield;
}

coroutine fn main_task() -> void {
    helper();
    print("메인으로 복귀");
}
```

## 구현 세부사항

모든 코루틴은 힙(Heap)에 상주합니다. 각 인스턴스는 다음과 같은 정보를 포함하는 `CoroutineFrame`에 의해 지원됩니다:
- 모든 레지스터(0-255)의 스냅샷
- 현재 피연산자 스택(Operand stack)
- 명령어 포인터(PC)
- 소속된 클로저의 업밸류(Upvalues) 포인터

코루틴이 중지될 때 프레임은 메모리 오버헤드를 줄이기 위해 선택적으로 압축됩니다. 다시 재개될 때 VM은 레지스터 뱅크를 복원하고 저장된 명령어 포인터부터 실행을 이어갑니다.
확장을 통해, C++ 호스트 틱(Tick) 단위에서 관리 중인 모든 스크립트 객체들의 진행 여부를 유연하게 파악할 수 있도록 진화하고 있습니다.
