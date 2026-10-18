# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Measure one Windows command tree, including short-lived compiler children."""

import argparse
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid


class _BasicLimits(ctypes.Structure):
    _fields_ = [
        ("user", ctypes.c_longlong), ("kernel", ctypes.c_longlong), ("flags", wintypes.DWORD),
        ("minimum_working_set", ctypes.c_size_t), ("maximum_working_set", ctypes.c_size_t),
        ("active_process_limit", wintypes.DWORD), ("affinity", ctypes.c_size_t),
        ("priority", wintypes.DWORD), ("scheduling", wintypes.DWORD),
    ]


class _IoCounters(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in (
        "read_operations", "write_operations", "other_operations", "read_bytes", "write_bytes", "other_bytes",
    )]


class _ExtendedLimits(ctypes.Structure):
    _fields_ = [
        ("basic", _BasicLimits), ("io", _IoCounters), ("process_limit", ctypes.c_size_t),
        ("job_limit", ctypes.c_size_t), ("peak_process_commit_bytes", ctypes.c_size_t),
        ("peak_job_commit_bytes", ctypes.c_size_t),
    ]


class _Accounting(ctypes.Structure):
    _fields_ = [
        ("user", ctypes.c_longlong), ("kernel", ctypes.c_longlong), ("period_user", ctypes.c_longlong),
        ("period_kernel", ctypes.c_longlong), ("faults", wintypes.DWORD), ("total", wintypes.DWORD),
        ("active", wintypes.DWORD), ("terminated", wintypes.DWORD),
    ]


class _Startup(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD), ("reserved", wintypes.LPWSTR), ("desktop", wintypes.LPWSTR),
        ("title", wintypes.LPWSTR), ("x", wintypes.DWORD), ("y", wintypes.DWORD),
        ("width", wintypes.DWORD), ("height", wintypes.DWORD), ("x_characters", wintypes.DWORD),
        ("y_characters", wintypes.DWORD), ("fill", wintypes.DWORD), ("flags", wintypes.DWORD),
        ("show", wintypes.WORD), ("reserved_size", wintypes.WORD),
        ("reserved_bytes", ctypes.POINTER(wintypes.BYTE)),
        ("stdin", wintypes.HANDLE), ("stdout", wintypes.HANDLE), ("stderr", wintypes.HANDLE),
    ]


class _Process(ctypes.Structure):
    _fields_ = [
        ("process", wintypes.HANDLE), ("thread", wintypes.HANDLE),
        ("pid", wintypes.DWORD), ("tid", wintypes.DWORD),
    ]


def _kernel():
    if os.name != "nt":
        raise OSError("Process commit measurement requires Windows Job Objects")
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    signatures = {
        "CreateJobObjectW": ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
        "CreateProcessW": ([
            wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL,
            wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(_Startup), ctypes.POINTER(_Process),
        ], wintypes.BOOL),
        "SetInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
        "AssignProcessToJobObject": ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
        "QueryInformationJobObject": ([
            wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p,
        ], wintypes.BOOL),
        "TerminateJobObject": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
        "TerminateProcess": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
        "ResumeThread": ([wintypes.HANDLE], wintypes.DWORD),
        "WaitForSingleObject": ([wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
        "GetExitCodeProcess": ([wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
        "GetStdHandle": ([wintypes.DWORD], wintypes.HANDLE),
        "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(kernel, name)
        function.argtypes, function.restype = arguments, result
    return kernel


def _checked(value):
    if not value:
        raise ctypes.WinError(ctypes.get_last_error())
    return value


def measure(argv, timeout=600.0):
    """Run argv without a shell; report OS commit peaks and wall/CPU seconds.

    The per-process peak is the largest individual process in the tree. The job
    peak is simultaneous tree commit, not the sum of individual lifetime peaks.
    Neither is resident RAM, cumulative allocation, or per-import net memory.
    """
    if not argv or not math.isfinite(timeout) or not 0 < timeout <= 3600:
        raise ValueError("A command and finite timeout in (0, 3600] seconds are required")
    argv = [os.fspath(argument) for argument in argv]
    kernel = _kernel()
    job = _checked(kernel.CreateJobObjectW(None, None))
    process = _Process()
    assigned = finished = False
    try:
        limits = _ExtendedLimits()
        # KILL_ON_JOB_CLOSE also covers Python exceptions or interrupted builds.
        limits.basic.flags = 0x2000
        _checked(kernel.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
        startup = _Startup(cb=ctypes.sizeof(_Startup), flags=0x100)
        startup.stdin, startup.stdout, startup.stderr = [
            kernel.GetStdHandle(number & 0xffffffff) for number in (-10, -11, -12)
        ]
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline(argv))
        # Assign before resuming: even compiler children that live less than a
        # sampling interval belong to this job from their first instruction.
        _checked(kernel.CreateProcessW(
            None, command, None, None, True, 4, None, None, ctypes.byref(startup), ctypes.byref(process),
        ))
        _checked(kernel.AssignProcessToJobObject(job, process.process))
        assigned = True
        begin = time.perf_counter()
        if kernel.ResumeThread(process.thread) == 0xffffffff:
            raise ctypes.WinError(ctypes.get_last_error())
        wait = kernel.WaitForSingleObject(process.process, math.ceil(timeout * 1000))
        timed_out = wait == 258
        if timed_out:
            _checked(kernel.TerminateJobObject(job, 124))
            wait = kernel.WaitForSingleObject(process.process, 10000)
        if wait != 0:
            raise OSError("Measured process did not terminate")
        elapsed = time.perf_counter() - begin
        code = wintypes.DWORD()
        _checked(kernel.GetExitCodeProcess(process.process, ctypes.byref(code)))
        # Job handles do not signal normal empty-tree completion. Query only
        # completion here; peak memory is retained by the kernel, not sampled.
        # Include children that outlive the root in both the bound and timing.
        deadline = begin + timeout
        kill_deadline = time.perf_counter() + 10 if timed_out else None
        accounting = _Accounting()
        while True:
            _checked(kernel.QueryInformationJobObject(
                job, 1, ctypes.byref(accounting), ctypes.sizeof(accounting), None,
            ))
            if not accounting.active:
                break
            now = time.perf_counter()
            if kill_deadline is not None and now >= kill_deadline:
                raise OSError("Measured command tree did not terminate")
            if kill_deadline is None and now >= deadline:
                timed_out = True
                _checked(kernel.TerminateJobObject(job, 124))
                kill_deadline = now + 10
            time.sleep(0.01)
        elapsed = time.perf_counter() - begin
        extended, accounting = _ExtendedLimits(), _Accounting()
        _checked(kernel.QueryInformationJobObject(job, 9, ctypes.byref(extended), ctypes.sizeof(extended), None))
        _checked(kernel.QueryInformationJobObject(job, 1, ctypes.byref(accounting), ctypes.sizeof(accounting), None))
        if accounting.active:
            raise OSError("Measured command left live children")
        finished = True
        return {
            "format": "step-catalog-process-measure.v1", "argv": argv, "cwd": os.getcwd(),
            "exit_code": 124 if timed_out else code.value, "timed_out": timed_out, "wall_seconds": elapsed,
            "peak_process_commit_bytes": extended.peak_process_commit_bytes,
            "peak_job_commit_bytes": extended.peak_job_commit_bytes,
            "tree_user_seconds": accounting.user / 1e7, "tree_kernel_seconds": accounting.kernel / 1e7,
            "tree_processes": accounting.total,
        }
    finally:
        if process.process and not finished:
            if assigned:
                kernel.TerminateJobObject(job, 124)
            else:
                kernel.TerminateProcess(process.process, 124)
            kernel.WaitForSingleObject(process.process, 10000)
        for handle in (process.thread, process.process, job):
            if handle:
                kernel.CloseHandle(handle)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-directory", required=True, type=Path)
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("A command after -- is required")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    row = measure(command, args.timeout)
    # Unique, exclusive files preserve all invocations under parallel launchers.
    path = args.output_directory / (uuid.uuid4().hex + ".json")
    with path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(row, output, indent=2, ensure_ascii=False)
        output.write("\n")
    return row["exit_code"]


if __name__ == "__main__":
    sys.exit(main())
