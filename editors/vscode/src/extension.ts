import * as vscode from 'vscode';

// Phase 1: no activation work is required. The grammar and language
// configuration are wired up entirely through `contributes` in
// package.json. Phase 2 will add LSP client startup here.
export function activate(_context: vscode.ExtensionContext): void {
  // intentionally empty
}

export function deactivate(): void {
  // intentionally empty
}
