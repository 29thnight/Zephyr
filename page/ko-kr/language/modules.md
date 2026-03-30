# 모듈 및 패키지 (Modules & Packages)

Zephyr는 코드를 모듈 단위로 조직화합니다. 각 `.zph` 파일은 독립적인 모듈이며, `export` 키워드를 통해 다른 모듈에서 사용할 수 있도록 바인딩을 외부로 노출할 수 있습니다.

## 엑스포트 (Exporting)

최상위 수준의 선언(함수, 변수, 구조체, 열거형 등)에 `export` 키워드를 붙여 내보낼 수 있습니다.

```zephyr
// math.zph
export const PI = 3.14159;
export fn square(x: float) -> float {
    return x * x;
}
```

### 선택적 가져오기 (Named Import)
특정 바인딩만 선택해서 가져옵니다.

```zephyr
import { square } from "math";
```

### 네임스페이스 에일리어스 (Namespace Alias)
모든 바인딩을 특정 접두사(Namespace) 아래로 가져옵니다.

```zephyr
import "math" as m;
print(m.square(10.0));
```

## 재수출 (Re-exporting)

모듈은 다른 모듈의 바인딩을 다시 내보냄으로써 게이트웨이 역할을 할 수 있습니다.

```zephyr
// utils.zph
export { square } from "math";
export { Vec2 } from "geometry";
```

## 호스트 C++ 제공 모듈

C++ 엔진 코드에서 동적으로 모듈을 주입할 수 있습니다. 바인딩된 모듈은 Zephyr 스크립트 상에서 동일하게 `import` 할 수 있습니다.

```cpp
vm.register_module("engine", [](ZephyrRuntime& rt) {
    rt.set_function("spawn", spawn_entity);
});
```

```zephyr
import "engine";
let e = spawn("player");
```

## 모듈 바이트코드 캐싱

Zephyr 컴파일러는 `.zph` 모듈을 파싱한 후 동일한 디렉토리에 `.zphc` 형태의 바이트코드 캐시 파일을 생성합니다. 소스 파일의 mtime(수정 시간)을 비교해 변경사항이 없을 경우 구문 분석(AST 변환)과 타입을 재검증하지 않고 캐시된 바이트코드를 즉각 로딩하여 매우 빠른 구동 속도를 보장합니다.

## Package 구성 (`package.toml`)

다중 패키지 프로젝트를 구성할 때는 패키지 매니페스트(`package.toml`)가 사용됩니다.

```toml
[package]
name = "my_game"
version = "0.1.0"
entry = "src/main.zph"

[dependencies]
math = "std/math"
utils = "src/utils"
```

호스트에서 `ZephyrVM::set_package_root()`를 호출하면 매니페스트를 참고하여 최상위 진입점을 설정하게 됩니다.

## 표준 라이브러리 (`std/*`)

Zephyr는 게임 스크립팅에 필수적인 표준 라이브러리를 기본 제공합니다.
- `std/math`: `sqrt`, `abs`, `lerp`, `clamp`, `sin`, `cos` (수치 연산)
- `std/string`: `split`, `trim`, `replace`, `to_upper` (텍스트 가공)
- `std/collections`: `Map<K,V>`, `Set<T>`, `Queue<T>`, `Stack<T>` (자료구조)
- `std/json`: `parse(s: string) -> Result<any>`, `stringify(v: any) -> string`
- `std/io`: `read_file`, `write_file`
- `std/gc`, `std/profiler`: 프로세스 상태 수명 제어 및 벤치마킹 도구
>>>>>>> ccc482111dc8b21cc19b373b1b97e7c97dcc1dbc
