# Struct & Enum

Rust 스타일의 구조체와 Algebraic Data Type 형 열거형을 적극 지원하여 스크립트 코드의 안정성을 확보합니다.

## 구조체 (`struct`)
객체 지향 모델의 클래스 또는 데이터 버퍼를 그룹화하는 타입입니다. 초기화 구문에서 필드 단축 문법(Shorthand)을 제공합니다.

```zephyr
struct Player {
  hp: int,
  name: string,
}

let x = 3;
let y = 4;
let p1 = Player { hp: 10, name: "neo" };
let p2 = Vec2 { x, y }; // 필드 단축 초기화
```

## 열거형 (`enum`)
단순한 상수 나열이 아닌, 상태와 파라미터(Payload)를 동적으로 담아둘 수 있는 가장 강력한 흐름 분기 디자인 패턴 도구입니다.

```zephyr
enum State {
  Idle,
  Hurt(int), // 타격 데미지를 담고 있는 페이로드
}
```
