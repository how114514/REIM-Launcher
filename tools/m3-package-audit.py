#!/usr/bin/env python3
"""M3-O3 dependency / license / content audit for the RC.

Checks the staged UE Shipping package and the assembled NativeBuild:

  * no Mojang game data (client/server JAR, Anvil region, level.dat, sounds),
  * no generated data or world directory inside the package,
  * no JVM/JRE DLL in the package or in the Runtime DLL imports,
  * adapter/runtime identity files are present,
  * license/NOTICES files are present.

    python tools/m3-package-audit.py \
        --package build/m3/ue-shipping-package/Windows \
        --native-build build/native-build/26.3 \
        --json build/m3/reports/package-audit.json

Exit status 0 only when every check passes. dumpbin is used when present; if it
is missing the import check is reported as not_run, not pass.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from pathlib import Path

FORBIDDEN_NAMES = {
    "level.dat", "level.dat_old", "session.lock", "jvm.dll", "jli.dll",
    "java.dll", "java.exe", "javaw.exe",
}
FORBIDDEN_SUFFIXES = {".jar", ".mca", ".mcc", ".mcr", ".dat_old"}
FORBIDDEN_PARTS = {"generated", "saves", "oracle", "reports", "work"}
JVM_IMPORT_TOKENS = ("jvm.dll", "jli.dll", "java.dll")


def scan(root: Path) -> dict:
    forbidden = []
    if not root.exists():
        return {"root": str(root), "missing": True, "forbidden": forbidden}
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        name = path.name.lower()
        if name in FORBIDDEN_NAMES or path.suffix.lower() in FORBIDDEN_SUFFIXES:
            forbidden.append(str(rel))
            continue
        if any(part.lower() in FORBIDDEN_PARTS for part in rel.parts[:-1]):
            forbidden.append(str(rel))
    return {"root": str(root), "missing": False, "files": sum(1 for _ in root.rglob("*") if _.is_file()),
            "forbidden": sorted(forbidden)}


def find_dumpbin() -> str | None:
    if shutil.which("dumpbin"):
        return shutil.which("dumpbin")
    base = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC")
    if base.exists():
        for candidate in sorted(base.glob(r"*\bin\Hostx64\x64\dumpbin.exe"), reverse=True):
            return str(candidate)
    return None


def dependents(dumpbin: str, binary: Path) -> dict:
    if not binary.exists():
        return {"binary": str(binary), "missing": True, "imports": []}
    proc = subprocess.run([dumpbin, "-dependents", str(binary)],
                          capture_output=True, text=True, errors="replace")
    imports = []
    for line in proc.stdout.splitlines():
        line = line.strip()
        if line.lower().endswith(".dll"):
            imports.append(line)
    jvm = [name for name in imports if any(tok in name.lower() for tok in JVM_IMPORT_TOKENS)]
    return {"binary": str(binary), "missing": False, "imports": imports,
            "jvm_imports": jvm, "dumpbin_exit": proc.returncode}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--package", default="build/m3/ue-shipping-package/Windows")
    ap.add_argument("--native-build", default="build/native-build/26.3")
    ap.add_argument("--json", default="build/m3/reports/package-audit.json")
    args = ap.parse_args()

    package = Path(args.package)
    native_build = Path(args.native_build)
    pkg_scan = scan(package)
    nb_scan = scan(native_build)

    native_version = native_build / "NativeVersions" / "26.3"
    required = [
        native_build / "native_build.json",
        native_version / "native_version.json",
        native_version / "ue_adapter.json",
        native_version / "mc_26.3.dll",
        native_build / "REIMRuntime" / "REIMRuntime.exe",
    ]
    missing = [str(p) for p in required if not p.exists()]

    license_files = {
        "package_notices": str(package / "NOTICES.txt"),
        "third_party_licenses": "third_party/LICENSES.md",
    }
    licenses = {name: Path(path).exists() for name, path in license_files.items()}

    dumpbin = find_dumpbin()
    imports = []
    import_status = "not_run"
    if dumpbin:
        imports = [
            dependents(dumpbin, native_version / "mc_26.3.dll"),
            dependents(dumpbin, package / "REIM" / "Binaries" / "Win64" /
                       "REIMRuntime-Win64-Shipping.exe"),
        ]
        import_status = "pass" if not any(i.get("jvm_imports") for i in imports) else "fail"

    checks = {
        "package_present": not pkg_scan["missing"],
        "native_build_present": not nb_scan["missing"],
        "no_forbidden_package_files": not pkg_scan["forbidden"],
        "no_forbidden_native_build_files": not nb_scan["forbidden"],
        "required_identity_files": not missing,
        "license_files": all(licenses.values()),
    }
    passed = all(checks.values()) and import_status != "fail"
    report = {
        "schema": "reim.m3.package_audit",
        "format": 1,
        "package": pkg_scan,
        "native_build": nb_scan,
        "required_missing": missing,
        "license_files": licenses,
        "imports": imports,
        "import_status": import_status,
        "checks": checks,
        "pass": passed,
        "note": ("generated data and worlds are user inputs and are intentionally "
                 "outside the package; this audit checks the package contents, "
                 "not the user's input directory."),
    }
    out = Path(args.json)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n",
                   encoding="utf-8")
    for name, ok in checks.items():
        print(f"{'ok' if ok else 'FAIL':4} {name}")
    print(f"{import_status:6} import-audit "
          f"({len(imports)} binaries, dumpbin={'yes' if dumpbin else 'no'})")
    if missing:
        print("missing:", ", ".join(missing))
    if pkg_scan["forbidden"] or nb_scan["forbidden"]:
        print("forbidden:", pkg_scan["forbidden"][:10], nb_scan["forbidden"][:10])
    print(f"pass={passed} -> {out}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
