# Overview

Zephyr는 Rust 스타일 문법을 채택하고 차용 검사(Borrow checking)나 수명(Lifetime) 어노테이션의 복잡성을 제거한, 게임 엔진용 고성능 임베디드 스크립트 언어입니다.

## 핵심 특징

- **풍부한 언어 기능**: 함수(`fn`), 구조체(`struct`), 열거형(`enum`), 트레이트(`trait`) 및 구현(`impl`), 패턴 매칭(`match`), 제네릭(`<T>`) 및 `where` 절, 모듈 `import`/`export` 완벽 지원.
- **고급 제어 흐름 및 연산자**: `if/else`, `while`, `for in`, `break`, `continue`, `yield` 등을 지원하며, `Result<T>` 기반의 오류 전파(`?` 연산자) 및 옵셔널 체이닝(`?.`)을 제공합니다.
- **런타임 아키텍처**: 슈퍼인스트럭션 융합(Superinstruction fusion)이 적용된 레지스터 기반(Register-based)의 고성능 바이트코드 가상 머신(VM). 릴리스 빌드에서는 AST를 건너뛰고 바이트코드만 사용하여 실행됩니다.
- **안전한 호스트 연동**: 명시적인 C++ 런타임 호스트 바인딩 및 4단계 생명주기(`Frame`, `Tick`, `Persistent`, `Stable`)를 갖춘 핸들 시스템.

<div class="custom-features-wrapper">
  <h2>Zephyr Core Architecture</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>⚡ 초고속 가상머신 (VM)</h3>
      <p>가상 레지스터 할당, Copy propagation, 슈퍼인스트럭션 융합(Fusion) 등의 최적화 기술이 적용된 바이트코드 인터프리터입니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>♻️ 세대별 가비지 컬렉터 (Generational GC)</h3>
      <p>Nursery(Young) 구조와 Old Generation 기반의 증분형(Incremental) GC입니다. 카드 테이블 및 Write barrier를 활용해 게임 루프 내 퍼즈(Pause) 시간을 최소화합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🎮 일급 객체 코루틴 (Coroutines)</h3>
      <p>호스트(C++) 콜스택과 무관하게 힙(Heap)에 상주하는 독립된 프레임을 가지며, `yield`와 `resume`를 통한 유연하고 즉각적인 제어 흐름 전환을 지원합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🛡️ 강력한 호스트 바인딩 정책</h3>
      <p>직렬화 규격(ZephyrSaveEnvelope)과 세대별 안전 검증 체계를 통해 댕글링 참조를 방지하고 엔진 객체 생명주기를 완벽히 분리합니다.</p>
    </div>
  </div>
</div>
