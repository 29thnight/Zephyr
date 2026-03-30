# Embedding ZephyrVM

Zephyr는 C++ 환경(게임 엔진 등)에 가볍게 임베딩(Embedding)되도록 설계되었습니다. 모든 빌트인 API는 `include/zephyr/api.hpp` 헤더를 통해 노출됩니다.

## 최소 연동 예제 (Minimal Example)

`ZephyrVM` 인스턴스를 통제하고 스크립트 바이트코드를 컴파일하여 실행하는 기본적인 예시입니다.

```cpp
#include <zephyr/api.hpp>

int main() {
    // 1. VM 초기화 및 런타임 생성
    ZephyrVM vm;
    auto rt = vm.create_runtime();

    // 2. 실행할 스크립트 코드 스니펫
    const char* source = R"(
        fn add(a: int, b: int) -> int { return a + b; }
        fn main() -> int { return add(10, 20); }
    )";

    // 3. 바이트코드 컴파일 및 특정 함수 진입점으로 실행
    auto chunk = vm.compile_bytecode_function(source, "main");
    auto result = rt.execute(chunk);

    // 4. 결과값(`ZephyrValue`) 추출
    printf("Result: %lld\n", result.as_int()); // 30

    return 0;
}
```

## 파일 로드 및 특정 함수 반복 호출

특정한 패키지 루트 디렉토리를 설정하여 스크립트 파일을 실행하거나 가져올 수 있습니다.

```cpp
ZephyrVM vm;
vm.set_package_root("game/scripts/");
auto rt = vm.create_runtime();

// 즉시 로드 후 실행 (평가)
rt.run_file("game/scripts/main.zph");
```

이미 로드된 모듈 내부의 특정 함수를 엔진 루프단에서 실시간으로 호출하려면 전역 심볼 리플렉션을 사용할 수 있습니다:

```cpp
auto chunk = vm.compile_module_bytecode(source, "game");
rt.load_module(chunk, "game");

// 스크립트 내의 'update' 함수 핸들을 찾아 매 라운드 호출
ZephyrValue fn_val = rt.get_value("update");
ZephyrValue arg = ZephyrValue::from_float(0.016f);   // delta time 전달
rt.call(fn_val, {arg});
```
