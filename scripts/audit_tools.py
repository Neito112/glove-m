"""Report which peripheral drivers the repository already ships.

The zclaw repository already contains a set of built-in tools. This script
lists them so that new drivers are only added when the repository does not
cover the hardware yet.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL_FILE = ROOT / "main" / "builtin_tools.def"

TOOL_RE = re.compile(r'TOOL_ENTRY\(\s*"([^"]+)"')


def tools_from_registry():
    if not TOOL_FILE.exists():
        return []
    return TOOL_RE.findall(TOOL_FILE.read_text(encoding="utf-8"))


def tools_from_git_head():
    try:
        out = subprocess.run(
            ["git", "show", "HEAD:main/builtin_tools.def"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
    except (subprocess.CalledProcessError, FileNotFoundError):
        return []
    return TOOL_RE.findall(out)


def main():
    current = tools_from_registry()
    original = tools_from_git_head()

    added = [t for t in current if t not in original]
    print(f"Tools in HEAD (upstream): {len(original)}")
    for tool in original:
        print(f"  - {tool}")
    print()
    print(f"Tools added in this worktree: {len(added)}")
    for tool in added:
        print(f"  + {tool}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
