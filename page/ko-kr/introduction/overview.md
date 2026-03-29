# Overview

Zephyr는 Rust 스타일 문법을 참고한 게임 엔진용 고성능 임베디드 스크립트 언어입니다.

## 핵심 특징

- **지원 기능**: 함수, 구조체, 열거형, trait/impl, match 패턴 매칭, 코루틴, 모듈 import/export
- **흐름 제어**: `if let`, `while let`, 범위 기반 `for i in 0..n`, `for i in 0..=n` 완벽 지원
- **오류 전파**: 가상머신 내부는 Result 계열 흐름으로 제한된 예외 처리를 수행합니다.
- **런타임 아키텍처**: 바이트코드(Bytecode) 파이프라이닝 우선의 가상 머신(Register 기반).
- **게임 특화 시스템**: 호스트 C++와 완전히 격리되어 엔진 틱(Tick) 단위로 쉽게 제어 가능한 전용 코루틴을 제공합니다.

## 미지원 항목 (Unsupported)
현재 아키텍처 구조상 미지원 상태이거나 지원 예정이 없는 기능입니다.

- 제네릭(Type Parameter) 및 `where` 절
- 매크로(Macro) 시스템
- Rust 수준의 소유권(Ownership) 및 라이프타임 시스템 (명시적인 GC 시스템이 대신 관리)
- 네이티브 C++ 경계를 넘나드는 `async/await` (내장 코루틴만 사용)

<div class="custom-features-wrapper">
  <h2>Zephyr Core Architecture</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>⚡ 캐시 친화적 가상머신 (VM)</h3>
      <p>가상 레지스터 할당 및 슈퍼인스트럭션 융합(Fusion) 기술을 통해 초저지연 실행 속도를 제공합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>♻️ 세대별 가비지 컬렉터</h3>
      <p>카드 트래킹이 적용된 4-Space 힙(Heap) 구조가 무거운 게임 로직에서도 프레임 드랍 없는 일관된 속도를 보장합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🎮 일급 객체 코루틴</h3>
      <p>호스트(C++) 콜스택과 완벽하게 완전히 분리된 독립 힙 기반 상태 머신으로 구현된 즉각적인 제어 역전(Yield/Resume)을 지원합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🛡️ 강력한 호스트 바인딩 정책</h3>
      <p>메모리 누수와 댕글링 포인터로부터 C++ 네이티브 메모리를 완벽하게 수호하는 4단계 핸들 수명 주기 시스템.</p>
    </div>
  </div>
</div>
