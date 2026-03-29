# Garbage Collection Architecture

> [!IMPORTANT] 프레임 드랍(Hitch)을 방어하는 게임 최적화
> 일반적인 언어의 Stop-The-World 방식은 게임 루프에서 심각한 시각적 버벅임을 유발합니다. 

Zephyr는 `Nursery`, `Small Old`, `Large Object(LOS)`, `Pinned` 4단계 힙 분리 스페이스와 증분식(Incremental) 마킹/조각모음 환경을 채택해 퍼포먼스를 유지합니다.

(내부 GC 힙 메모리 파이프라인과 트래블슈팅 방법 문서화 예정)
