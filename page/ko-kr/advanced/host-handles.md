# Host Handle Policy

> [!WARNING] C++ 네이티브 포인터의 위험성 방어 (Memory Leak & Dangling)
> 동적인 로직이 담긴 스크립트 환경에 C++ 원시 바운드 포인터를 곧바로 노출하는 것은 Use-After-Free(UAF) 등의 문제를 야기하여 게임 크래시를 유발하는 흔한 원인 중 하나입니다.

Zephyr는 게임 엔진 객체(Entity, Component 등)를 스크립트로 전달하기 위해 내부적으로 **강력한 4단계 계층의 핸들 세대 주기(Generation Check) 시스템**을 구축하고 있습니다. 
주어진 시점(ex: 씬 교체)에 맞추어 모든 관련 하위 그룹 핸들의 참조 연결을 일괄 파기 처리하여 메모리를 안전하게 방어합니다.

## 핸들 등급 정책

<div class="custom-features-wrapper">
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>🔴 Frame</h3>
      <p>가장 짧은 생명주기. 현재 콜스택 프레임 반환 시 함께 소거됩니다. (C++ API: `create_frame_handle`)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟠 Tick</h3>
      <p>엔진 렌더링 루프의 한 틱이 끝날 때 (`advance_tick()`) 무효화 처리됩니다. (C++ API: `create_tick_handle`)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟡 Persistent</h3>
      <p>씬(Scene)이 교체되거나 단계가 전환될 때 (`advance_scene()`) 환수됩니다. (C++ API: `create_persistent_handle`)</p>
    </div>
    <div class="custom-feature-card">
      <h3>🟢 Stable</h3>
      <p>명시적 해제 (`invalidate_host_handle()`) 전까지 유지됩니다. <b>직렬화(세이브)가 가능한 유일한 등급입니다.</b></p>
    </div>
  </div>
</div>

## 시나리오별 런타임 제어 방식

C++ 엔진 코어 개발자는 엔진 업데이트 메인 루프에서 단계별 전이를 수행해야만 핸들 정책이 완벽히 작동합니다.

```cpp
// 씬(Scene / Level)의 변경: Persistent 이하 핸들이 모두 무효화됨
rt.advance_scene();

// 매 렌더링 프레임(Tick) 시작: Tick 이하 생명주기의 핸들을 소거함
rt.advance_tick();
```

> [!NOTE] 가상 머신 행동 규칙
> 호스트 엔진에 의해 무효화(Invalidated)된 핸들을 스크립트가 호출(접근) 시도할 경우, **디버그(Debug) 빌드에서는 즉각적인 엔진 Trap(오류 파악용)이 발생**하며, **릴리스(Release) 빌드에서는 폴백(Fallback) 모드로 들어가 핸들 예외 런타임 오류로 중단되는 등 지정된 Handle Recovery Policy에 따라 동작**시킵니다.

## 핸들 직렬화 (Save / Load)

게임의 플레이 상태, 즉 데이터 구조체를 JSON 포맷 등의 규격으로 직렬화할 때(Save Envelope v2), VM은 객체 트리를 완전 순회합니다.

```cpp
std::string json = rt.serialize_value(my_script_root_value);
```

이 과정에서 오직 가장 영속도가 짙은 `Stable` 등급의 핸들 레퍼런스만이 올바르게 파일 내 포맷으로 담기를 허가받습니다. 반대로 수명이 짧은 일회성 요소인 `Persistent`, `Tick` 등의 인스턴스가 섞여 있다면 `serialize_value()` 진입 시점에 예외로 가로막히며 더미 핸들의 무단 데이터 오염을 예방합니다.

## 스마트 핸들 레퍼런스 (`ZephyrHandle<T>`)

단순한 무효화 메커니즘을 넘어, C++ 호스트 측에서 특정 스크립트 객체(`ZephyrValue`)가 의도치 않게 `Frame`이나 `Tick` 수집 주기에 의해 만료(Expired)되는 것을 방지하기 위해 **RAII 패턴 기반의 `ZephyrHandle<T>` 스마트 래퍼(Wrapper)** 클래스를 제공합니다.

해당 래퍼는 본체 소멸자(Destructor)에 도달하기 전까지 백그라운드에서 `vm.pin_value()`와 `vm.unpin_value()`를 연계 호출하여 가비지 컬렉터와 핸들 파기 시스템 앞의 절대적인 고정 보호막을 제공합니다.

```cpp
// 호스트 C++ 객체 포인터를 바탕으로 스마트 핸들 활성화.
// RAII 스코프를 유지하는 동안 포인터는 파기 시점으로부터 절대적으로 안전(Pinning)합니다.
auto h = ZephyrHandle<Player>(vm, vm.make_host_object(player_ptr));

// C++ 측에서 내부 포인터 멤버 규격에 안전하게 바로 접근할 수 있습니다.
if (h) {
    h.get()->damage(10);
}
```
