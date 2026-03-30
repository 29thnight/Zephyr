# Garbage Collection Architecture

> [!IMPORTANT] 프레임 드랍(Hitch) 스파이크를 방어하는 게임 최적화
> 언어 런타임 차원에서의 전형적인 Stop-The-World 방식 GC 풀 프로세스는 메인스레드를 장기간 멈추게 하므로 게임 루프에서 심각한 시각적 버벅임(Frame Hitch)을 유발합니다. 

Zephyr의 GC(가리비지 컬렉터)는 게임 엔진 특화 구조를 띄고 있어 **세대별(Generational)** 레이아웃과 함께 목표 데드라인 내에서만 작동하는 분절형, **증분식(Incremental)** 파이프라인으로 구축되었습니다.

<div class="custom-features-wrapper">
  <h2>힙 공간 세대 분리 (Generations)</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>🌱 Nursery (Young)</h3>
      <p>모든 스크립트 객체가 최초 할당되는 영역입니다. 빈도가 높은 가벼운 `Young GC`를 통해 수명이 짧은 데드 레퍼런스를 신속하게 폐기합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🌳 Old Generation</h3>
      <p>일정 크기의 GC 사이클 이상 살아남은 객체들이 진급(Tenured)하여 영구적으로 상주하는 영역입니다. 무거운 구역이므로 평소에는 스윕을 회피합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>🔗 Write Barrier & Card Table</h3>
      <p>C++ 코어 런타임의 최적화 구조로, Old 객체가 Young 객체를 가리키는 변형(Mutation) 포인터 연결을 추적하여 GC 스캔 속도를 획기적으로 가속합니다.</p>
    </div>
  </div>
</div>

## 호스트(C++) GC 매뉴얼 제어

매 엔진 메인 루프 프레임 업데이트마다 일정 마이크로초(µs)의 GC 비용 예산(Budget)을 제공하여 GC 작업을 증분식으로 진행(Step)하게 둘 수 있습니다.

```cpp
rt.advance_tick();

// 이번 프레임(Tick)에서는 가비지컬렉터에 최대 1,000 µs (1ms) 예산만 할당
rt.gc_step(1000); 
```

만약 스테이지 간의 이동처럼 씬(Scene) 전환 작업으로 인해 로딩 스크린이 표시되어 로드율이 비는 시점이 생기거나 대량의 찌꺼기 메모리를 강제로 일괄 환수해야 할 경우, 풀 스윕(Stop-The-World Full GC)을 곧바로 발생시킵니다.

```cpp
rt.advance_scene();

// 전면 스윕을 통한 수거로 쾌적화
rt.collect_garbage(); 
// 또는 비교적 가벼운 Young 전용 수집
// rt.collect_young();
```

## 디버깅 및 분석 툴 내장

추가적으로, C++ 엔진 통신 API 중 하나인 `rt.start_gc_trace()` 및 `rt.get_gc_trace_json()` 를 통해 프로파일링 캡처링를 유도할 수 있습니다. 런타임 환경의 GC Pause Length 히스토그램 내역(p50/p95/p99) 등을 JSON으로 뽑아내 게임 최적화 측정기로 분석(크롬 Trace 포맷 연동)하여 메모리 부하 지점(Bottlenecks)을 사전에 발본색원할 수 있습니다.
