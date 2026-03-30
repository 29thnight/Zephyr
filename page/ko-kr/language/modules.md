# Modules & Imports

길어지는 로직과 소스들을 역할 단위 객체로 패키징할 수 있습니다.

## 스크립트 모듈 연동
# 모듈과 패키지 (Modules and Packages)

Zephyr는 코드를 모듈 단위로 구성합니다. 각 `.zph` 파일은 독립적인 모듈이며, 다른 모듈에서 사용할 수 있도록 자신의 바인딩을 내보낼 수 있습니다.

## 내보내기 (Exporting)

`export` 키워드를 사용하여 함수, 변수, 구조체, 열거형 등 모든 최상위 선언을 내보낼 수 있습니다.

```zephyr
// math.zph
export const PI = 3.14159;
export fn square(x: float) -> float {
    return x * x;
}
```

## 가져오기 (Importing)

모듈은 여러 가지 구문을 통해 가져올 수 있습니다.

### 기본 가져오기 (Default Import)
대상 모듈에서 내보낸 모든 바인딩을 현재 스코프로 가져옵니다.

```zephyr
import "math";
print(square(PI));
```

### 선택적 가져오기 (Named Import)
특정 바인딩만 선택해서 가져옵니다.

```zephyr
import { square } from "math";
```

### 네임스페이스 에일리언스 (Namespace Alias)
모든 바인딩을 특정 접두사(Namespace) 아래로 가져옵니다.

```zephyr
import "math" as m;
print(m.square(10.0));
```

## 재해출 (Re-exporting)

모듈은 다른 모듈의 바인딩을 다시 내보냄으로써 게이트웨이 역할을 할 수 있습니다.

```zephyr
// utils.zph
export { square } from "math";
export { Vec2 } from "geometry";
```

## 호스트 모듈 (Host Modules)

C++ 호스트는 Zephyr 스크립트에서 일반 모듈처럼 가져올 수 있는 네이티브 모듈을 등록할 수 있습니다. 주로 엔진 API를 제공하는 데 사용됩니다.

```zephyr
import "engine"; // C++ VM API를 통해 등록됨
let entity = engine.spawn("player");
```

## 패키지 해결 (Package Resolution)

여러 파일로 구성된 프로젝트의 경우, 루트에 있는 `package.toml` 파일이 패키지 구조를 설명합니다.

```toml
[package]
name = "game_core"
version = "1.0.0"
entry = "src/main.zph"
```

VM은 호스트 API(`set_package_root`)를 통해 정의된 패키지 루트를 기준으로 임포트 경로를 해결합니다.

## 바이트코드 캐싱

로드 시간을 최적화하기 위해 Zephyr는 소스 파일을 바이트코드로 자동 컴파일하고 `.zphc` 파일로 캐싱합니다. 소스 파일의 수정 시간이 변경되면 캐시는 자동으로 무효화됩니다.
