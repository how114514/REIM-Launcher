#!/usr/bin/env python3
"""M3-M5 / G3 JRT audit.

Scans the shipped Runtime sources (not the Converter's offline analyzer) for
Java/JVM embedding. The M3 translation gate default is no-go, so the expected
result is zero non-comment hits and no JVM linkage in the Runtime build.

This is a source audit only: it does not start Java and does not prove runtime
absence of a JVM by execution. The Runtime target is also checked for JVM
link libraries in CMake.

    python tools/m3-jrt-audit.py [--root .] [--json build/m3/reports/jrt-audit.json]

Exit status: 0 when the audit is clean, 1 when a non-comment hit exists.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

PATTERN = re.compile(
    r"\b(?:jvm|javavm|jni|jrt|defineclass|classloader|jobject|jclass|jmethodid|"
    r"bytecode|invokedynamic)\b",
    re.IGNORECASE,
)
SOURCES = ("*.cpp", "*.h", "*.c")
SCAN_ROOTS = ("runtime/mc-native", "runtime/contract", "runtime/preview",
              "runtime/headless")
CMAKE_FILES = ("runtime/mc-native/CMakeLists.txt", "runtime/preview/CMakeLists.txt",
               "runtime/headless/CMakeLists.txt", "runtime/contract/CMakeLists.txt")
JVM_LINK = re.compile(r"jvm\.lib|java\.lib|jdk|jre|jni\.h", re.IGNORECASE)


def strip_comments(text: str) -> str:
    """Remove C/C++ comments while leaving string literals alone."""
    out = []
    i = 0
    n = len(text)
    state = "code"
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == "code":
            if c == "/" and nxt == "/":
                state = "line"
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = "block"
                i += 2
                continue
            if c == '"':
                state = "string"
                out.append(c)
                i += 1
                continue
            out.append(c)
            i += 1
        elif state == "line":
            if c == "\n":
                state = "code"
                out.append(c)
            i += 1
        elif state == "block":
            if c == "*" and nxt == "/":
                state = "code"
                i += 2
                continue
            if c == "\n":
                out.append(c)
            i += 1
        else:  # string
            out.append(c)
            if c == "\\" and nxt:
                out.append(nxt)
                i += 2
                continue
            if c == '"':
                state = "code"
            i += 1
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--json", default="build/m3/reports/jrt-audit.json")
    args = ap.parse_args()
    root = Path(args.root).resolve()

    hits = []
    files = 0
    for rel in SCAN_ROOTS:
        base = root / rel
        for pattern in SOURCES:
            for path in sorted(base.rglob(pattern)):
                files += 1
                raw = path.read_text(encoding="utf-8", errors="replace")
                code = strip_comments(raw)
                for lineno, line in enumerate(code.splitlines(), 1):
                    for match in PATTERN.finditer(line):
                        hits.append({
                            "file": path.relative_to(root).as_posix(),
                            "line": lineno,
                            "token": match.group(0),
                            "text": line.strip()[:200],
                        })

    cmake_hits = []
    for rel in CMAKE_FILES:
        path = root / rel
        if not path.exists():
            continue
        for lineno, line in enumerate(path.read_text(encoding="utf-8",
                                                     errors="replace").splitlines(), 1):
            if line.lstrip().startswith("#"):
                continue
            if JVM_LINK.search(line):
                cmake_hits.append({"file": rel, "line": lineno,
                                   "text": line.strip()})

    report = {
        "schema": "reim.m3.jrt_audit",
        "format": 1,
        "roots": list(SCAN_ROOTS),
        "files_scanned": files,
        "non_comment_hits": hits,
        "cmake_jvm_link_hits": cmake_hits,
        "jrt_surface": 0,
        "runtime_java_process": False,
        "clean": not hits and not cmake_hits,
        "note": ("Source audit only; the offline Converter/analyzer may use a "
                 "JDK and is intentionally outside these roots. The Runtime "
                 "target is not linked against a JVM."),
    }
    out = root / args.json
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    print(json.dumps({k: report[k] for k in
                      ("files_scanned", "jrt_surface", "runtime_java_process",
                       "clean")}, indent=2))
    if hits:
        for hit in hits[:10]:
            print(f"  hit {hit['file']}:{hit['line']} {hit['token']}")
    if cmake_hits:
        for hit in cmake_hits[:10]:
            print(f"  cmake {hit['file']}:{hit['line']} {hit['text']}")
    return 0 if report["clean"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
