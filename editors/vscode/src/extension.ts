import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind,
} from 'vscode-languageclient/node';
import { findMora } from './findMora';

let client: LanguageClient | undefined;

export async function activate(context: vscode.ExtensionContext): Promise<void> {
    const moraPath = findMora();
    if (!moraPath) {
        vscode.window.showErrorMessage(
            'Mora language server not found. Set "mora.path" or install `mora` on PATH.',
        );
        return;
    }

    const serverOptions: ServerOptions = {
        run:   { command: moraPath, args: ['lsp'], transport: TransportKind.stdio },
        debug: { command: moraPath, args: ['lsp', '--log', '/tmp/mora-lsp.log'], transport: TransportKind.stdio },
    };

    const config = vscode.workspace.getConfiguration('mora');

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'mora' }],
        initializationOptions: {
            dataDir: config.get<string>('dataDir', ''),
            // relationsDir defaults to <workspace>/data/relations on the server side.
        },
        synchronize: {
            // Notify the server when relation YAML files change so the
            // SchemaRegistry can be reloaded. (Phase 3 — for now this is
            // a no-op on the server side.)
            fileEvents: vscode.workspace.createFileSystemWatcher('**/data/relations/**/*.yaml'),
        },
        outputChannelName: 'Mora',
    };

    client = new LanguageClient('mora', 'Mora Language Server', serverOptions, clientOptions);
    await client.start();
    context.subscriptions.push({ dispose: () => client?.stop() });
}

export function deactivate(): Thenable<void> | undefined {
    return client?.stop();
}
