# Host Handle Policy

> [!WARNING] C++ 네이티브 포인터의 위험성(Memory Leak) 방어
> C++ 엔진에서는 스크립트 쪽으로 포인터를 넘길 때 메모리 댕글링(Dangling) 사고가 자주 발생합니다.

Zephyr는 원시 메모리 포인터 접근 대신 4가지 단계(`Frame`, `Tick`, `Persistent`, `Stable`)의 유연하고 강력한 핸들 생명 주기를 통해 게임의 크래시를 방지합니다.

(세부 핸들 규칙 매뉴얼이 추가될 예정입니다)
