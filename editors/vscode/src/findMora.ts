import * as fs from 'node:fs';
import * as path from 'node:path';
import * as vscode from 'vscode';

/**
 * Resolve the path to the `mora` binary.
 *
 * Resolution order:
 *   1. `mora.path` configuration setting (if absolute and exists, used as-is;
 *      if relative or bare name, looked up on PATH).
 *   2. `mora` on PATH.
 *
 * Returns the absolute path to the binary, or null if not found.
 */
export function findMora(): string | null {
    const config = vscode.workspace.getConfiguration('mora');
    const setting = config.get<string>('path', 'mora');

    // If the setting is an absolute path, accept it iff it exists.
    if (path.isAbsolute(setting)) {
        return fs.existsSync(setting) ? setting : null;
    }

    // Otherwise (bare name like "mora", or relative path), search PATH.
    const PATH = process.env.PATH ?? '';
    const sep = process.platform === 'win32' ? ';' : ':';
    const exts = process.platform === 'win32'
        ? (process.env.PATHEXT ?? '.EXE;.CMD;.BAT').split(';')
        : [''];
    for (const dir of PATH.split(sep)) {
        for (const ext of exts) {
            const candidate = path.join(dir, setting + ext);
            try {
                if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
                    return candidate;
                }
            } catch { /* ignore */ }
        }
    }
    return null;
}
