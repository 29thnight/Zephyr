# Overview

Zephyr는 Rust 스타일 문법을 채택하고 차용 검사(Borrow checking)나 수명(Lifetime) 어노테이션의 복잡성을 제거한, 게임 엔진용 고성능 임베디드 스크립트 언어입니다.

## 핵심 기능 (Core Features)
- **지원 패러다임**: 함수, 구조체, 열거형, 트레이트/구현(Impls), 패턴 매칭(`match`), 제네릭, 코루틴, 모듈 내보내기/가져오기.
- **제어 흐름**: `if let`, `while let` 및 범위 기반 반복자(`0..n`, `0..=n`) 지원.
- **런타임 아키텍처**: 슈퍼인스트럭션 융합(Superinstruction fusion)이 적용된 레지스터 기반(Register-based)의 고성능 바이트코드 가상 머신(VM). 릴리스 빌드에서는 AST를 건너뛰고 바이트코드만 사용하여 실행됩니다.
- **가비지 컬렉션**: 카드 트래킹 및 쓰기 장벽(Write Barriers)이 포함된 4단계 세대별 GC.
- **코루틴**: C++ 네이티브 콜 스택과 완전히 분리된 독립적인 힙 예약 메모리 프레임(`CoroutineObject`) 기반 동작.
- **안전한 호스트 연동**: 명시적인 C++ 런타임 호스트 바인딩 및 4단계 생명주기(`Frame`, `Tick`, `Persistent`, `Stable`)를 갖춘 핸들 시스템.

## 제약 사항
- 매크로 시스템 미지원.
- Rust 수준의 소유권/수명 검증 미지원 (세대별 GC를 통한 메모리 관리).
- C++와의 경계를 넘나드는 `async/await`는 코루틴 중단(Suspension)을 통해 처리됨.

<div class="custom-features-wrapper">
  <h2>기술 사양 (Technical Specifications)</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>⚡ VM 실행</h3>
      <p>최적화된 명령어 디스패치를 위한 슈퍼인스트럭션 퓨전 및 가상 레지스터 할당.</p>
    </div>
    <div class="custom-feature-card">
      <h3>♻️ 메모리 관리</h3>
      <p>결정론적인 프레임 타이밍 보장을 위한 쓰기 장벽 기반 4단계 세대별 GC.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🎮 상태 관리</h3>
      <p>스택 오버헤드 없는 복잡한 비동기 로직 처리를 위한 힙 보존 코루틴 프레임.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🛡️ 호스트 통합</h3>
      <p>안전한 C++ 네이티브 메모리 교환 및 포인터 보호를 위한 수명 관리 핸들 시스템.</p>
    </div>
  </div>
</div>
