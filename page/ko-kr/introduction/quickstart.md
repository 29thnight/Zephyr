# Quickstart

현재 `Project Zephyr`의 CLI 도구를 이용하거나 벤치마크 툴을 통해 스크립트를 즉시 실행하고 바이트코드 결과를 분석해결 수 있습니다.

## 스크립트 검사 (Check)
코드를 작성한 후, 문법 오류 및 심볼 검증을 수행합니다.
```bash
zephyr check my_script.zph
```

## 실행 (Run)
`ZephyrVM` 기반의 독립 실행 유틸리티를 호출하여 스크립트를 평가합니다.
```bash
zephyr run examples/state_machine.zph
```

## 엔진 연동
게임 엔진 내부에서 `ZephyrVM`을 생성하고 통신하고자 한다면, `api.hpp` 인터페이스를 사용하여 직접 C++ 포인터를 바인딩해야 합니다. 자세한 가이드는 [ADVANCED - Embedding ZephyrVM](../advanced/embedding) 섹션을 참고하세요.
