#!/usr/bin/env python3
# Set each tracked file's mtime to the timestamp of the mainline commit that
# last modified it. Walks `git log --first-parent --name-only` once, stamping
# each file the first time it appears (log iterates newest → oldest).
#
# Invoked from CI after `actions/checkout` — which stamps every file with
# mtime=now — so that xmake's mtime-based incremental-build detection lines
# up with cached object files from prior runs.
#
# Run from a git work tree. No submodule recursion here — loop over
# submodules at the caller with `git submodule foreach`.

import os
import subprocess
import sys


def main() -> int:
    # Output is line-oriented blocks:
    #   <unix-timestamp>\n
    #   \n
    #   path/one\n
    #   path/two\n
    #   \n
    #   <next-timestamp>\n
    #   ...
    proc = subprocess.run(
        ["git", "log", "--first-parent",
         "--format=format:%ct", "--name-only"],
        check=True, stdout=subprocess.PIPE, text=True,
        errors="surrogateescape",
    )

    stamped: dict[str, int] = {}
    cur_ts: int | None = None
    for line in proc.stdout.splitlines():
        if not line:
            continue
        if line.isdigit():
            cur_ts = int(line)
            continue
        if cur_ts is not None and line not in stamped:
            stamped[line] = cur_ts

    applied = 0
    for path, ts in stamped.items():
        try:
            os.utime(path, (ts, ts), follow_symlinks=False)
            applied += 1
        except (FileNotFoundError, NotADirectoryError, PermissionError, OSError):
            pass
    print(f"git-restore-mtime: stamped {applied}/{len(stamped)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main())
