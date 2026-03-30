# Getting Started

Zephyr 컴파일러 및 VM을 빌드하고 공식 확장 프로그램을 연동하여 스크립트를 작성하는 과정을 안내합니다.

## 설치 및 빌드

CMake를 통해 크로스 플랫폼 빌드를 수행할 수 있습니다 (C++20 컴파일러 필수, 외부 의존성 없음).

```bash
git clone https://github.com/29thnight/Zephyr.git
cd Zephyr
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

또는 Windows 환경에서는 Visual Studio 18을 통해 `Zephyr.sln`을 열어 빌드할 수도 있습니다.

> 빌드된 파일들이 위치한 디렉토리를 시스템의 **PATH**에 추가하면 `zephyr` CLI를 전역적으로 사용할 수 있습니다.

## 에디터 설정 (VS Code)

공식 VS Code 확장 프로그램은 구문 강조, 코드 자동 완성, 호버링(Hover), 정의로 이동, 인라인 진단 등을 완벽히 지원합니다.

```bash
cd editors/vscode-zephyr
npm install
npm run package
code --install-extension zephyr-*.vsix
```

`.zph` 확장자를 가진 스크립트 파일을 열면 Zephyr 내장 언어 서버(LSP, `zephyr lsp`)가 자동으로 백그라운드에서 시작됩니다.

<div class="custom-features-wrapper">
  <h2>CLI 명령어 모음</h2>
  <div class="custom-features-grid">
    <div class="custom-feature-card">
      <h3>zephyr run</h3>
      <p>스크립트를 즉시 컴파일 및 실행합니다. `--profile` 인자를 뒤에 붙여 성능 트레이싱 결과를 뽑아낼 수 있습니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr check</h3>
      <p>가상머신 실행 없이 구문 분석(Lexing/Parsing) 및 타입 검사(Semacheck) 단계만 수행하여 오류를 진단합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr repl / dump</h3>
      <p>`repl` 명령어로 대화형 세션을 시작하거나, `dump-bytecode` 명령어로 스크립트를 분석용 OP 코드로 디스어셈블합니다.</p>
    </div>
    <div class="custom-feature-card">
      <h3>zephyr lsp / dap</h3>
      <p>VS Code 등의 외부 에디터와 연동하기 위해 백그라운드에서 표준 언어 서버(LSP) 및 디버거 어댑터를 호스팅합니다.</p>
    </div>
  </div>
</div>

프로파일링이 필요하다면 `--profile` 플래그를 추가하세요.
```bash
zephyr run --profile mygame.zph
# 실행 결과로 샘플링 정보가 담긴 zephyr_profile.json 파일이 생성됩니다.
```

## Hello World

간단한 스크립트를 작성해 동작을 테스트해 보세요:

```zephyr
// hello.zph
fn main() -> void {
    print("Hello, World!");
}
```

```bash
zephyr run hello.zph
# 출력: Hello, World!
```
