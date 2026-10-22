"""envcheck - the read-only environment gate of a bench session (03_WorkPlan M8 task 2, 02_Architecture 12.5 step 1).

Three checks, none of which changes anything on the machine (01 Q9, 04 Q9: the user switches the power plan):
  power_plan   `powercfg /getactivescheme` must name High performance or Ultimate Performance (by GUID; the plan name is
               localised). Balanced is refused unless --allow-balanced; a bypassed session is marked in every table.
  idle         the whole system must be below 3 % busy over a 2 s window (GetSystemTimes). Windows are tried one after the
               other until one passes, at least IDLE_MIN_WINDOWS of them and for up to --idle-wait seconds (default 120 s:
               this desktop idles at 2.5-4.9 %, so a fixed five windows refused one or two starts in three); the
               time waited and every window are recorded. --allow-busy turns a failure into a recorded warning; --skip-idle
               (tests) does not measure and is recorded as a bypass of this check too.
  processes    no other PhysicsTest process (PhysicsTestBench, PhysicsTestViewer, PhysicsTestAutomation, ...) may run.
               --allow-concurrent turns a failure into a recorded warning (CTest runs its tests in parallel).
The result is a dict (session.json "environment"): every check with its value, pass flag and whether it was bypassed;
"refused" is true when a check failed without its bypass.
"""
from __future__ import annotations

import ctypes
import os
import platform
import re
import subprocess
import sys
import time
from ctypes import wintypes

from .common import system32_executable, utc_now

# Power scheme GUIDs (powercfg /list; the personalities are documented by Microsoft as these fixed GUIDs).
HIGH_PERFORMANCE_GUIDS = {
    "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c": "High performance",
    "e9a42b02-d5df-448d-aa00-03f14749eb61": "Ultimate Performance",
}
KNOWN_GUIDS = {
    "381b4222-f694-41f0-9685-ff5bb260df2e": "Balanced",
    "a1841308-3541-4fab-bc81-f71556f20b4a": "Power saver",
    **HIGH_PERFORMANCE_GUIDS,
}
IDLE_THRESHOLD_PCT = 3.0
IDLE_WINDOW_S = 2.0
IDLE_MIN_WINDOWS = 5
IDLE_WAIT_S = 120.0
PROCESS_PREFIXES = ("physicstest", "ptsmoke")

_GUID_RE = re.compile(r"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})")


def _decode_console(data: bytes) -> str:
    """powercfg writes the console code page (CP949 on a Korean Windows); try it, then UTF-8."""
    for encoding in ("mbcs", "oem", "utf-8"):
        try:
            return data.decode(encoding)
        except (UnicodeDecodeError, LookupError):
            continue
    return data.decode("utf-8", errors="replace")


def parse_power_scheme(text: str, returncode: int = 0) -> dict:
    """The active scheme from `powercfg /getactivescheme` text: {"guid", "name", "kind", "high_performance", "error"}."""
    result = {"guid": None, "name": None, "kind": None, "high_performance": False, "source": "powercfg /getactivescheme", "raw": text.strip(), "error": ""}
    match = _GUID_RE.search(text)
    if returncode != 0 or not match:
        result["error"] = f"powercfg exit {returncode}: no scheme GUID in '{text.strip()[:200]}'"
        return result
    guid = match.group(1).lower()
    name_match = re.search(r"\(([^()]*)\)\s*$", text.strip())
    result["guid"] = guid
    result["name"] = name_match.group(1).strip() if name_match else None
    result["kind"] = KNOWN_GUIDS.get(guid, "custom")
    result["high_performance"] = guid in HIGH_PERFORMANCE_GUIDS
    return result


def read_power_scheme() -> dict:
    """The active power scheme (read-only: powercfg /getactivescheme). A custom plan counts as not High performance.
    powercfg.exe is started by its full path in System32, never found through the current directory."""
    try:
        completed = subprocess.run([system32_executable("powercfg.exe"), "/getactivescheme"], capture_output=True, timeout=30, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        result = parse_power_scheme("", 1)
        result["error"] = f"powercfg failed: {error}"
        return result
    return parse_power_scheme(_decode_console(completed.stdout), completed.returncode)


class _FileTime(ctypes.Structure):
    _fields_ = [("low", wintypes.DWORD), ("high", wintypes.DWORD)]

    def value(self) -> int:
        return (self.high << 32) | self.low


class _ProcessEntry32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_wchar * 260),
    ]


_KERNEL32 = None


def _kernel32():
    """kernel32 with use_last_error, so ctypes.get_last_error() reports the failing call's code (windll does not keep it),
    and with argtypes / restype for every call ptbench makes: a HANDLE is pointer-sized, never passed as a C int."""
    global _KERNEL32
    if _KERNEL32 is None:
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        filetime = ctypes.POINTER(_FileTime)
        kernel32.GetSystemTimes.argtypes = [filetime, filetime, filetime]
        kernel32.GetSystemTimes.restype = wintypes.BOOL
        kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
        kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
        entry = ctypes.POINTER(_ProcessEntry32W)
        kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, entry]
        kernel32.Process32FirstW.restype = wintypes.BOOL
        kernel32.Process32NextW.argtypes = [wintypes.HANDLE, entry]
        kernel32.Process32NextW.restype = wintypes.BOOL
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel32.CloseHandle.restype = wintypes.BOOL
        _KERNEL32 = kernel32
    return _KERNEL32


def _system_times() -> tuple[int, int, int]:
    idle, kernel, user = _FileTime(), _FileTime(), _FileTime()
    if not _kernel32().GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
        raise OSError(ctypes.get_last_error(), "GetSystemTimes failed")
    return idle.value(), kernel.value(), user.value()


class SystemBusySampler:
    """System-wide busy share between start() and stop(), in % of all logical CPUs (kernel time includes idle time)."""

    def __init__(self) -> None:
        self._begin = None

    def start(self) -> None:
        self._begin = _system_times()

    def stop(self) -> float:
        end = _system_times()
        idle = end[0] - self._begin[0]
        total = (end[1] - self._begin[1]) + (end[2] - self._begin[2])
        if total <= 0:
            return 0.0
        return max(0.0, 100.0 * (total - idle) / total)


def measure_system_busy(seconds: float = IDLE_WINDOW_S) -> float:
    sampler = SystemBusySampler()
    sampler.start()
    time.sleep(seconds)
    return sampler.stop()


def list_processes() -> list[tuple[int, str]]:
    """(pid, image name) of every process (Toolhelp32 snapshot, read-only)."""
    kernel32 = _kernel32()
    snapshot = kernel32.CreateToolhelp32Snapshot(0x00000002, 0)   # TH32CS_SNAPPROCESS
    if snapshot in (None, wintypes.HANDLE(-1).value):
        raise OSError(ctypes.get_last_error(), "CreateToolhelp32Snapshot failed")
    out = []
    try:
        entry = _ProcessEntry32W()
        entry.dwSize = ctypes.sizeof(_ProcessEntry32W)
        ok = kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            out.append((int(entry.th32ProcessID), entry.szExeFile))
            ok = kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snapshot)
    return out


def find_physicstest_processes() -> list[dict]:
    me = os.getpid()
    return [{"pid": pid, "name": name} for pid, name in list_processes() if pid != me and name.lower().startswith(PROCESS_PREFIXES)]


def read_cpu_name() -> str:
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
    except OSError:
        return platform.processor()


def host_facts() -> dict:
    return {
        "cpu": read_cpu_name(),
        "logical_processors": os.cpu_count(),
        "os": platform.platform(),
        "python": sys.version.split()[0],
        "machine": platform.node(),
    }


def measure_idle(threshold_pct: float = IDLE_THRESHOLD_PCT, window_s: float = IDLE_WINDOW_S, min_windows: int = IDLE_MIN_WINDOWS,
                 wait_s: float = IDLE_WAIT_S, measure=measure_system_busy, clock=time.monotonic) -> dict:
    """2 s windows one after the other until one is below the threshold: at least min_windows, and more while less than
    wait_s has passed. {"pass", "samples_pct", "windows", "waited_s"}."""
    samples = []
    started = clock()
    while True:
        samples.append(round(measure(window_s), 3))
        if samples[-1] < threshold_pct:
            break
        if len(samples) >= max(1, min_windows) and clock() - started >= wait_s:
            break
    return {"pass": samples[-1] < threshold_pct, "samples_pct": samples, "windows": len(samples), "waited_s": round(clock() - started, 3)}


def run_envcheck(allow_balanced: bool = False, allow_busy: bool = False, allow_concurrent: bool = False, idle_threshold_pct: float = IDLE_THRESHOLD_PCT,
                 idle_window_s: float = IDLE_WINDOW_S, idle_min_windows: int = IDLE_MIN_WINDOWS, idle_wait_s: float = IDLE_WAIT_S,
                 skip_idle: bool = False, scheme_reader=None, process_finder=None) -> dict:
    """The environment gate. Nothing is changed on the machine. scheme_reader / process_finder replace the powercfg query and
    the process snapshot (tests: a Balanced or custom plan on a machine that runs High performance)."""
    checks = []
    scheme = (scheme_reader or read_power_scheme)()
    power_ok = bool(scheme["high_performance"])
    checks.append({
        "id": "power_plan",
        "pass": power_ok,
        "bypassed": (not power_ok) and allow_balanced,
        "bypass_flag": "--allow-balanced",
        "detail": f"{scheme.get('name')} ({scheme.get('kind')}, {scheme.get('guid')})" + (f"; {scheme['error']}" if scheme.get("error") else ""),
    })
    if skip_idle:
        # Not measured is not a pass: the check counts as bypassed, so --skip-idle appears in "bypasses", session.json and
        # the report head like --allow-busy.
        idle = {"pass": False, "samples_pct": [], "windows": 0, "waited_s": 0.0}
        checks.append({"id": "idle", "pass": False, "bypassed": True, "bypass_flag": "--skip-idle", "detail": "not measured (--skip-idle)"})
    else:
        idle = measure_idle(idle_threshold_pct, idle_window_s, idle_min_windows, idle_wait_s)
        samples = idle["samples_pct"]
        shown = samples if len(samples) <= 12 else samples[:6] + ["..."] + samples[-5:]
        idle_detail = (f"system busy {samples[-1]:.2f}% over {idle_window_s:g} s (limit {idle_threshold_pct:g}%; {idle['windows']} window(s) in "
                       f"{idle['waited_s']:.1f} s, wait limit {idle_wait_s:g} s: {', '.join(s if isinstance(s, str) else f'{s:.2f}' for s in shown)})")
        checks.append({"id": "idle", "pass": idle["pass"], "bypassed": (not idle["pass"]) and allow_busy, "bypass_flag": "--allow-busy", "detail": idle_detail})
    others = (process_finder or find_physicstest_processes)()
    processes_ok = not others
    checks.append({
        "id": "processes",
        "pass": processes_ok,
        "bypassed": (not processes_ok) and allow_concurrent,
        "bypass_flag": "--allow-concurrent",
        "detail": "no other PhysicsTest process" if processes_ok else "running: " + ", ".join(f"{p['name']} ({p['pid']})" for p in others[:10]),
    })
    refused = any(not c["pass"] and not c["bypassed"] for c in checks)
    return {
        "schema": "physicstest.envcheck/1",
        "utc": utc_now(),
        "power_scheme": scheme,
        "power_plan_high_performance": power_ok,
        "idle": {"threshold_pct": idle_threshold_pct, "window_s": idle_window_s, "min_windows": idle_min_windows, "wait_limit_s": idle_wait_s,
                 "samples_pct": idle["samples_pct"], "windows": idle["windows"], "waited_s": idle["waited_s"], "pass": idle["pass"], "skipped": skip_idle},
        "physicstest_processes": others,
        "checks": checks,
        "bypasses": [c["bypass_flag"] for c in checks if c["bypassed"]],
        "refused": refused,
        "host": host_facts(),
        "note": "read-only: the gate never changes the power plan or any other system setting (01 Q9)",
    }


def describe(result: dict) -> str:
    lines = []
    for check in result["checks"]:
        state = "PASS" if check["pass"] else ("BYPASSED " + check["bypass_flag"] if check["bypassed"] else "FAIL")
        lines.append(f"  {check['id']:<10} {state:<28} {check['detail']}")
    verdict = "REFUSED" if result["refused"] else ("PASS (with bypass: " + ", ".join(result["bypasses"]) + ")" if result["bypasses"] else "PASS")
    return "environment gate: " + verdict + "\n" + "\n".join(lines)
