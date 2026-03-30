# Coroutines

Zephyr의 코루틴은 특정 시점(`yield`)에 실행을 일시 중단하고 보존된 힙(Heap) 상태를 유지한 채 호스트로 제어권을 반환할 수 있는 일급 객체입니다. 게임 AI 상태 머신, 애니메이션 시퀀싱 전개, 비동기적인 타임라인 제어에 매우 효과적입니다.

## 코루틴 생성 및 실행

일반적인 `fn` 대신 `coroutine fn` 키워드를 사용하여 선언합니다. 코루틴 함수를 호출하더라도 코드가 즉시 실행되지는 않으며, `resume` 키워드로 깨워주어야만 첫 `yield` 구문(또는 함수 종료 지점) 까지 진행됩니다.

```zephyr
coroutine fn hello() -> void {
    print("A");
    yield;
    print("B");
}

let h = hello();
resume h;   // "A" 출력 후 중단
resume h;   // "B" 출력 후 종료
print(h.done); // true
```

## 코루틴 상태 조회

모든 코루틴 인스턴스(CoroutineFrame)는 내장 속성을 제공합니다.
- `.done` : 함수가 완전히 반환(`return`)되어 종료되었는지 확인하는 bool 값입니다.
- `.suspended` : 현재 코루틴이 진행 중이 아니라 `yield` 상태로 대기 중인지 확인하는 bool 값입니다.

## 양방향 값 전달

`yield <expr>` 을 통해 호출자(resume하는 쪽)에게 값을 즉각 반환할 수 있습니다.

```zephyr
coroutine fn squares(n: int) -> int {
    mut i = 1;
    while i <= n {
        yield i * i;
        i += 1;
    }
}

let sq = squares(4);
while !sq.done {
    print(resume sq); // 1, 4, 9, 16 출력
}
```

## 중첩 및 헬퍼 계층

코루틴 내에서 호출된 일반 함수 내부에서 `yield`를 호출할 수 있으며, 이 경우 부모 코루틴을 포함한 전체 호출 스택이 동시에 안전하게 중단(Suspend)됩니다.

```zephyr
fn wait_and_log(msg: string) -> void {
    print(msg);
    yield;   // 부모 코루틴의 흐름을 통째로 중단시킵니다.
}

coroutine fn sequence() -> void {
    wait_and_log("step 1");
    wait_and_log("step 2");
}
```

## 구현 구조 (Architecture)

OS 쓰레드 방식과 달리 콜스택의 포인터를 보존하기 위한 불필요한 메모리 낭비가 전혀 없습니다.
모든 코루틴은 독립적인 레지스터 뱅크 스냅샷과 PC(Program Counter)를 담는 가벼운 `CoroutineFrame`으로 힙 영역에 거주하며, 호스트 엔진(C++) 코드는 메인루프 틱(Tick)마다 단순히 `VM.resume()`을 호출하는 것으로 코루틴을 재가동합니다.
