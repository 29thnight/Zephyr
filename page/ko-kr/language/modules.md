# Modules & Imports

길어지는 로직과 소스들을 역할 단위 객체로 패키징할 수 있습니다.

## 스크립트 모듈 연동
타 파일들이나 코어 엔진에서 심볼이나 함수를 가져옵니다.

```zephyr
// 외부 파일의 Namespace를 한 번에 인클루드
import "foo.zph";

// Alias(별칭) 단위로 종속성 호출 축소
import "foo.zph" as foo;

// C++ 혹은 Zephyr 코어 시스템 단에 구축된 전역 내장 모듈
import "engine";

// 외부에서 이 모듈의 함수를 Call할 수 있도록 개방
export fn run() -> int {
  return 1;
}
```

> [!NOTE] 정적 분석 방어
> 스크립트 컴파일 트리 파싱 단계에서 존재하지 않는 파일이나, `export` 처리가 되지 않은 비공개 멤버 함수를 호출하려 들면 엔진 시스템이 종속성 누락 오류를 친절하게 반환합니다.
