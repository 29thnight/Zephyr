# C++ Class Bindings

가상머신 외부(Host C++)단에 존재하는 사용자 지정 C++ 함수나 복합적인 클래스/구조체의 규격을 스크립트 단으로 브릿징(Bridging) 할 수 있습니다.

## 호스트 전역 함수 바인딩

엔진 로그 출력이나 타이머 같은 전역 함수를 등록하여 Zephyr 모듈 생태계(`import`)로 캡슐화할 수 있습니다. 호스트 측에서는 `vm.register_function`을 사용합니다.

```cpp
vm.register_function("log_message", [](ZephyrRuntime& rt, ZephyrArgs args) {
    printf("[script log] %s\n", args[0].as_string().c_str());
    return ZephyrValue::nil();
});
```

스크립트에서의 활용 방법:

```zephyr
import "engine"; // 기본 글로벌 호스트 모듈
log_message("Hello from script!");
```

## 클래스 객체 메서드 바인딩 (`ZephyrClassBinder<T>`)

`ZephyrClassBinder<T>` 템플릿 메타프로그래밍 유틸리티를 활용하여 C++ 인스턴스 내부의 멤버 함수나 필드를 스크립트의 체이닝 가능한 메서드로 개방(Export)할 수 있습니다.

```cpp
ZephyrClassBinder<Entity> binder("Entity");

// 구조적인 메서드 바인딩 (람다 활용)
binder.method("get_x", [](Entity& e, ZephyrArgs) {
    return ZephyrValue::from_float(e.transform.x);
});

binder.method("set_x", [](Entity& e, ZephyrArgs args) {
    e.transform.x = args[0].as_float();
    return ZephyrValue::nil(); // void return을 위한 nil 처리
});

binder.method("destroy", [](Entity& e, ZephyrArgs) {
    e.destroy();
    return ZephyrValue::nil();
});

// 바인더 세팅 완료 후 VM에 클래스 공식 등록
vm.register_class(binder);
```

이후 스크립트 내에서 외부 핸들 포인터를 바탕으로 `player.set_x(100.0);` 과 같이 직접 C++ 인스턴스를 읽고 쓰는 제어가 가능해집니다.

```zephyr
import "engine";
let player = get_entity("player");

player.set_x(100.0);
print(player.get_x());
player.destroy();
```
