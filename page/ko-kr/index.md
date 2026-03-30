---
layout: page
sidebar: false
---

<AntigravityBg
  :count="300"
  :magnetRadius="14"
  :ringRadius="12"
  :waveSpeed="0.4"
  :waveAmplitude="1"
  :particleSize="0.4"
  :lerpSpeed="0.05"
  :autoAnimate="true"
  :particleVariance="1"
  :rotationSpeed="0"
  :depthFactor="1"
  :pulseSpeed="3"
  particleShape="capsule"
  :fieldStrength="10"
/>

<CustomHero
  locale="ko"
  tagline="게임 엔진을 위한 초고속 스크립트 언어"
  primaryText="문서 읽어보기"
  primaryLink="/Zephyr/ko-kr/introduction/overview"
  secondaryText="GitHub 방문하기"
  secondaryLink="https://github.com/29thnight/Zephyr"
/>

<ScrollFeatures
  :features="[
    { title: '⚡ 캐시 친화적 가상머신', description: '가상 레지스터 할당 및 슈퍼인스트럭션 융합(Fusion) 기술을 통해 초저지연 다운타임과 극한의 데이터 처리 속도를 제공합니다.' },
    { title: '♻️ 세대별 가비지 컬렉터', description: '카드 트래킹이 세밀하게 적용된 4-Space 힙(Heap) 구조가, 매우 무거운 타격 및 AI 게임 로직에서도 1ms 프레임 드랍 조차 허용하지 않는 일관적인 속도를 보장합니다.' },
    { title: '🎮 일급 객체 코루틴', description: '호스트(C++) 콜스택과 완벽하게 격리된 독립 힙 기반 상태 머신(State Machine)으로 구축된 스레드를 활용하여 한계 없이 즉각적인 제어 역전(Yield/Resume)을 지원합니다.' },
    { title: '🛡️ 강력한 호스트 바인딩 정책', description: '골치아프고 위험한 메모리 누수와 댕글링 포인터로부터 C++ 네이티브 메모리 경계를 완벽하게 수호하며 분리하는 4단계의 고립된 핸들러 수명 주기 생태계.' }
  ]"
/>
