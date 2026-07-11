#!/usr/bin/env python3

from pathlib import Path
import re
import subprocess


LINK = re.compile(r"\[[^]]+\]\((?!https?://|#|mailto:)([^)#]+)(?:#[^)]+)?\)")


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    failures = []
    for document in [root / "README.md", *sorted((root / "docs").rglob("*.md"))]:
        text = document.read_text(encoding="utf-8")
        for target in LINK.findall(text):
            resolved = (document.parent / target).resolve()
            if not resolved.exists():
                failures.append(f"{document.relative_to(root)}: missing {target}")
    for script in sorted((root / "scripts").rglob("*.sh")):
        result = subprocess.run(["bash", "-n", script], capture_output=True, text=True)
        if result.returncode:
            failures.append(f"{script.relative_to(root)}: {result.stderr.strip()}")
    if failures:
        raise SystemExit("\n".join(failures))


if __name__ == "__main__":
    main()
