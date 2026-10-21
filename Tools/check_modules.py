"""check_modules.py - DLL hygiene of backend modules (gate A9, 03_WorkPlan M2, 02_Architecture 7.1 / 14.2).

A backend DLL must export exactly one symbol, PT_GetBackendApi, and import only the C runtime and KERNEL32 (the
Chaos DLL later adds the UE runtime it is built from; that check arrives with M2.5 / M13). The script reads the
module with dumpbin (from the MSVC toolset of the build) and prints one JSON report per module.

"C runtime" means the VC runtime (vcruntime140*), the UCRT (ucrtbase*, api-ms-win-crt-*) and, for SDK backends that use
the C++ standard library, the C++ runtime msvcp140*. --strict-crt drops msvcp140* from the allow-list: the Null backend,
the Chaos template, must not depend on the C++ library DLL (docs/Abi.md 7, CTest PhysicsTest.Probe.CheckModules).

    python -I Tools/check_modules.py out/dev/bin/PtBackend_Null.dll --strict-crt --dumpbin <dir>/dumpbin.exe
    python -I Tools/check_modules.py <dll>... [--strict-crt] [--dumpbin PATH] [--json]

Exit code 0 when every module passes, 1 when one fails, 2 on a usage or tool error. Standard library only.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys

REQUIRED_EXPORTS = {"PT_GetBackendApi"}

# The dynamic C runtime (release and debug): VC runtime, UCRT forwarders and the UCRT itself.
CRT_PATTERNS = [
    re.compile(r"^vcruntime140(_1)?d?\.dll$", re.IGNORECASE),
    re.compile(r"^ucrtbased?\.dll$", re.IGNORECASE),
    re.compile(r"^api-ms-win-crt-[a-z0-9-]+\.dll$", re.IGNORECASE),
]
# The C++ standard library DLLs: allowed for SDK backends, not with --strict-crt.
CXX_RUNTIME_PATTERNS = [
    re.compile(r"^msvcp140(_1|_2|_atomic_wait|_codecvt_ids)?d?\.dll$", re.IGNORECASE),
]
SYSTEM_ALLOWED = {"kernel32.dll"}


def run_dumpbin(dumpbin: str, flag: str, path: str) -> str:
    completed = subprocess.run([dumpbin, "/nologo", flag, path], capture_output=True, text=True, errors="replace", check=False)
    if completed.returncode != 0:
        raise RuntimeError(f"dumpbin {flag} {path} failed ({completed.returncode}): {completed.stderr.strip() or completed.stdout.strip()}")
    return completed.stdout


def parse_exports(text: str) -> list[str]:
    # The export table lines look like: "          1    0 00001230 PT_GetBackendApi = PT_GetBackendApi"
    names = []
    in_table = False
    for line in text.splitlines():
        if re.match(r"^\s*ordinal\s+hint\s+RVA\s+name", line):
            in_table = True
            continue
        if in_table:
            if not line.strip():
                if names:
                    break
                continue
            match = re.match(r"^\s*\d+\s+[0-9A-Fa-f]+\s+(?:[0-9A-Fa-f]{8}\s+)?(\S+)", line)
            if match:
                names.append(match.group(1))
            elif line.strip().startswith("Summary"):
                break
    return names


def parse_dependents(text: str) -> list[str]:
    dependents = []
    in_list = False
    for line in text.splitlines():
        if "Image has the following dependencies" in line or "Image has the following delay load dependencies" in line:
            in_list = True
            continue
        if in_list:
            stripped = line.strip()
            if not stripped:
                if dependents:
                    in_list = False
                continue
            if stripped.startswith("Summary"):
                break
            if stripped.lower().endswith(".dll"):
                dependents.append(stripped)
    return dependents


def is_allowed_dependency(name: str, strict_crt: bool) -> bool:
    if name.lower() in SYSTEM_ALLOWED:
        return True
    if any(pattern.match(name) for pattern in CRT_PATTERNS):
        return True
    return not strict_crt and any(pattern.match(name) for pattern in CXX_RUNTIME_PATTERNS)


def check_module(dumpbin: str, path: str, strict_crt: bool = False) -> dict:
    exports = parse_exports(run_dumpbin(dumpbin, "/EXPORTS", path))
    dependents = parse_dependents(run_dumpbin(dumpbin, "/DEPENDENTS", path))
    problems = []
    if set(exports) != REQUIRED_EXPORTS or len(exports) != len(REQUIRED_EXPORTS):
        problems.append(f"exports {exports} (expected exactly {sorted(REQUIRED_EXPORTS)})")
    disallowed = [name for name in dependents if not is_allowed_dependency(name, strict_crt)]
    if disallowed:
        allowed = "the VC runtime, the UCRT and KERNEL32 (--strict-crt: no C++ library DLL)" if strict_crt else "the C runtime and KERNEL32"
        problems.append(f"imports outside {allowed}: {disallowed}")
    if not any(name.lower() == "kernel32.dll" for name in dependents):
        problems.append("no KERNEL32.dll import (unexpected for an MSVC DLL; the dumpbin output may not have been parsed)")
    return {
        "module": os.path.basename(path),
        "path": os.path.abspath(path),
        "exports": exports,
        "dependents": dependents,
        "pass": not problems,
        "problems": problems,
        "strict_crt": strict_crt,
    }


def find_dumpbin(explicit: str | None) -> str | None:
    if explicit:
        return explicit if os.path.isfile(explicit) else None
    return shutil.which("dumpbin") or shutil.which("dumpbin.exe")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Backend DLL hygiene (gate A9)")
    parser.add_argument("modules", nargs="+", help="DLL files to check")
    parser.add_argument("--dumpbin", help="path of dumpbin.exe (default: PATH, e.g. after vcvars64.bat)")
    parser.add_argument("--json", action="store_true", help="print only the JSON lines")
    parser.add_argument("--strict-crt", action="store_true", help="reject the C++ standard library DLLs (msvcp140*) too")
    args = parser.parse_args(argv)
    dumpbin = find_dumpbin(args.dumpbin)
    if dumpbin is None:
        print("check_modules: dumpbin.exe not found (pass --dumpbin or run inside vcvars64)", file=sys.stderr)
        return 2
    failed = False
    for module in args.modules:
        if not os.path.isfile(module):
            print(f"check_modules: {module}: no such file", file=sys.stderr)
            return 2
        try:
            report = check_module(dumpbin, module, args.strict_crt)
        except RuntimeError as error:
            print(f"check_modules: {error}", file=sys.stderr)
            return 2
        print("PT_MODULE_CHECK " + json.dumps(report, sort_keys=True))
        if not args.json:
            status = "PASS" if report["pass"] else "FAIL"
            print(f"{status} {report['module']}: exports {report['exports']}, imports {report['dependents']}")
            for problem in report["problems"]:
                print(f"  - {problem}")
        failed = failed or not report["pass"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
