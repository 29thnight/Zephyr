# Wave I — VS Code Zephyr 확장 구현

## 전제조건
`wave_i_lsp_server.md` 완료 후 진행 (`zephyr_cli.exe lsp` 동작 확인 후).

## 목표
VS Code에서 Zephyr 언어 지원:
- 문법 하이라이트 (TextMate grammar)
- LSP 연동 (Diagnostics, Hover, Completion, Go-to-Definition)

---

## 디렉토리 구조

```
editors/vscode-zephyr/
├── package.json
├── tsconfig.json
├── src/
│   └── extension.ts
└── syntaxes/
    └── zephyr.tmLanguage.json
```

---

## Step 1: 디렉토리 생성 및 package.json 작성

`editors/vscode-zephyr/package.json` 생성:

```json
{
  "name": "vscode-zephyr",
  "displayName": "Zephyr Language",
  "description": "Zephyr scripting language support",
  "version": "0.1.0",
  "engines": { "vscode": "^1.85.0" },
  "categories": ["Programming Languages"],
  "activationEvents": ["onLanguage:zephyr"],
  "main": "./out/extension.js",
  "contributes": {
    "languages": [{
      "id": "zephyr",
      "aliases": ["Zephyr", "zephyr"],
      "extensions": [".zph"],
      "configuration": "./language-configuration.json"
    }],
    "grammars": [{
      "language": "zephyr",
      "scopeName": "source.zephyr",
      "path": "./syntaxes/zephyr.tmLanguage.json"
    }],
    "configuration": {
      "title": "Zephyr",
      "properties": {
        "zephyr.serverPath": {
          "type": "string",
          "default": "",
          "description": "Path to zephyr_cli.exe (auto-detected if empty)"
        }
      }
    }
  },
  "scripts": {
    "compile": "tsc -p ./",
    "watch": "tsc -watch -p ./"
  },
  "dependencies": {
    "vscode-languageclient": "^9.0.1"
  },
  "devDependencies": {
    "@types/vscode": "^1.85.0",
    "typescript": "^5.3.0"
  }
}
```

---

## Step 2: language-configuration.json

`editors/vscode-zephyr/language-configuration.json` 생성:

```json
{
  "comments": {
    "lineComment": "//",
    "blockComment": ["/*", "*/"]
  },
  "brackets": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"]
  ],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"" },
    { "open": "'", "close": "'" }
  ],
  "surroundingPairs": [
    ["{", "}"],
    ["[", "]"],
    ["(", ")"],
    ["\"", "\""]
  ],
  "indentationRules": {
    "increaseIndentPattern": "\\{\\s*$",
    "decreaseIndentPattern": "^\\s*\\}"
  }
}
```

---

## Step 3: TextMate Grammar

`editors/vscode-zephyr/syntaxes/zephyr.tmLanguage.json` 생성:

```json
{
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "Zephyr",
  "scopeName": "source.zephyr",
  "patterns": [
    { "include": "#comments" },
    { "include": "#strings" },
    { "include": "#keywords" },
    { "include": "#types" },
    { "include": "#literals" },
    { "include": "#functions" },
    { "include": "#operators" }
  ],
  "repository": {
    "comments": {
      "patterns": [
        {
          "name": "comment.line.double-slash.zephyr",
          "match": "//.*$"
        },
        {
          "name": "comment.block.zephyr",
          "begin": "/\\*", "end": "\\*/"
        }
      ]
    },
    "strings": {
      "patterns": [
        {
          "name": "string.quoted.double.zephyr",
          "begin": "f\"", "end": "\"",
          "patterns": [
            { "name": "meta.embedded.zephyr", "begin": "\\{", "end": "\\}" }
          ]
        },
        {
          "name": "string.quoted.double.zephyr",
          "begin": "\"", "end": "\"",
          "patterns": [
            { "name": "constant.character.escape.zephyr", "match": "\\\\." }
          ]
        }
      ]
    },
    "keywords": {
      "patterns": [
        {
          "name": "keyword.control.zephyr",
          "match": "\\b(if|else|while|for|return|break|continue|match|yield|import|export)\\b"
        },
        {
          "name": "keyword.declaration.zephyr",
          "match": "\\b(fn|let|trait|impl|struct|class|type)\\b"
        },
        {
          "name": "keyword.operator.zephyr",
          "match": "\\b(and|or|not|in|is)\\b"
        }
      ]
    },
    "types": {
      "patterns": [
        {
          "name": "support.type.zephyr",
          "match": "\\b(Int|Float|String|Bool|Nil|Any|List|Map|Coroutine)\\b"
        }
      ]
    },
    "literals": {
      "patterns": [
        {
          "name": "constant.language.zephyr",
          "match": "\\b(true|false|nil)\\b"
        },
        {
          "name": "constant.numeric.zephyr",
          "match": "\\b[0-9]+(\\.[0-9]+)?\\b"
        }
      ]
    },
    "functions": {
      "patterns": [
        {
          "name": "entity.name.function.zephyr",
          "match": "\\b([a-zA-Z_][a-zA-Z0-9_]*)(?=\\s*\\()"
        }
      ]
    },
    "operators": {
      "patterns": [
        {
          "name": "keyword.operator.zephyr",
          "match": "(->|=>|\\?\\.|\\+=|-=|\\*=|/=|==|!=|<=|>=|<|>|\\+|-|\\*|/|%|=)"
        }
      ]
    }
  }
}
```

---

## Step 4: extension.ts (LSP 클라이언트)

`editors/vscode-zephyr/src/extension.ts` 생성:

```typescript
import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient;

function findServerPath(context: vscode.ExtensionContext): string {
    // 1. 설정에서 명시적 경로
    const config = vscode.workspace.getConfiguration('zephyr');
    const configPath = config.get<string>('serverPath');
    if (configPath && fs.existsSync(configPath)) return configPath;

    // 2. 워크스페이스 루트의 x64/Release
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders) {
        const candidate = path.join(workspaceFolders[0].uri.fsPath, 'x64', 'Release', 'zephyr_cli.exe');
        if (fs.existsSync(candidate)) return candidate;
    }

    // 3. PATH에서
    return 'zephyr_cli';
}

export function activate(context: vscode.ExtensionContext) {
    const serverPath = findServerPath(context);

    const serverOptions: ServerOptions = {
        command: serverPath,
        args: ['lsp'],
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'zephyr' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.zph')
        }
    };

    client = new LanguageClient('zephyr', 'Zephyr Language Server', serverOptions, clientOptions);
    client.start();
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
```

---

## Step 5: tsconfig.json

`editors/vscode-zephyr/tsconfig.json` 생성:

```json
{
  "compilerOptions": {
    "module": "commonjs",
    "target": "ES2020",
    "outDir": "out",
    "lib": ["ES2020"],
    "sourceMap": true,
    "rootDir": "src",
    "strict": true
  },
  "exclude": ["node_modules", ".vscode-test"]
}
```

---

## Step 6: 빌드 및 설치

```powershell
Set-Location editors\vscode-zephyr
npm install
npm run compile
```

### VS Code에 개발 버전 설치

`F1` → `Developer: Install Extension from Location...` → `editors/vscode-zephyr` 폴더 선택.

또는 `.vsix` 패키징:
```powershell
npx vsce package
code --install-extension vscode-zephyr-0.1.0.vsix
```

---

## Step 7: 동작 확인

1. VS Code에서 `.zph` 파일 열기
2. 문법 하이라이트 확인 (키워드 색상)
3. 오타 입력 후 빨간 밑줄 확인 (Diagnostics)
4. `fn` 위에 마우스 올리면 호버 설명 확인
5. `Ctrl+Space`로 자동완성 목록 확인
6. 함수명에서 `F12`로 go-to-definition 확인

---

## 주의사항
- `vscode-languageclient` v9는 Node.js 18+ 필요
- Windows에서 `zephyr_cli.exe lsp` 실행 시 stdout binary mode 필수 (서버 측에서 설정)
- 확장 개발 중 `F5`로 Extension Development Host 실행 가능
- `outputChannel`에 LSP 로그 출력: `client.outputChannel.show()`
