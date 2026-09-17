"""Run clang-tidy on changed lines belonging to compiled translation units."""

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def changed_ranges(diff: str) -> dict[str, list[list[int]]]:
    ranges: dict[str, list[list[int]]] = {}
    current: str | None = None
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            candidate = line[6:]
            current = candidate if Path(candidate).suffix in {".c", ".cc", ".cpp", ".cxx"} else None
        elif current and (match := re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", line)):
            start, count = int(match[1]), int(match[2] or 1)
            if count:
                ranges.setdefault(current, []).append([start, start + count - 1])
    return ranges


def analyze(build: Path, base: str, head: str, root: Path) -> None:
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    compiled = set()
    for entry in database:
        directory = Path(entry["directory"])
        if not directory.is_absolute():
            directory = build / directory
        compiled.add(os.path.normcase(str((directory / entry["file"]).resolve())))
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    result = subprocess.run(
        [
            "git",
            "-c",
            "core.quotepath=false",
            "diff",
            "--no-ext-diff",
            "--unified=0",
            "--diff-filter=ACMR",
            base,
            head,
            "--",
            "src",
            "tests",
        ],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
        timeout=60,
        creationflags=flags,
    )
    sources: list[str] = []
    filters: list[dict[str, str | list[list[int]]]] = []
    for file, ranges in changed_ranges(result.stdout).items():
        absolute = str((root / file).resolve())
        if os.path.normcase(absolute) not in compiled:
            print(f"Skipping changed source not present in compile_commands.json: {file}")
            continue
        sources.append(absolute)
        filters.append({"name": absolute, "lines": ranges})
    if not sources:
        print("No changed compiled C/C++ lines require clang-tidy.")
        return
    subprocess.run(
        [
            "clang-tidy",
            f"-p={build}",
            f"--line-filter={json.dumps(filters)}",
            "--warnings-as-errors=*",
            *sources,
        ],
        cwd=root,
        check=True,
        timeout=1200,
        creationflags=flags,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-directory", type=Path, required=True)
    parser.add_argument("--base-revision", required=True)
    parser.add_argument("--head-revision", default="HEAD")
    args = parser.parse_args()
    try:
        analyze(
            args.build_directory.resolve(),
            args.base_revision,
            args.head_revision,
            Path(__file__).resolve().parents[2],
        )
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"clang-tidy failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
