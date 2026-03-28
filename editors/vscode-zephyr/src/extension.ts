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
