# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Windows worker-tree lifetime and commit-memory cap (not a GUI memory cap)."""
import ctypes
from ctypes import wintypes
import os


class _Basic(ctypes.Structure):
    _fields_ = [("user", ctypes.c_longlong), ("kernel", ctypes.c_longlong), ("flags", wintypes.DWORD),
                ("minimum", ctypes.c_size_t), ("maximum", ctypes.c_size_t), ("active", wintypes.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD), ("scheduling", wintypes.DWORD)]


class _Limits(ctypes.Structure):
    _fields_ = [("basic", _Basic), ("io", ctypes.c_ulonglong * 6), ("process", ctypes.c_size_t),
                ("job", ctypes.c_size_t), ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]


class ProcessGuard:
    def __init__(self, pid, maximum_bytes=2 * 1024**3):
        self.handle = None
        self.kernel = None
        if os.name != "nt":
            return  # Windows portable edition is the qualified distribution.
        kernel = self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            "OpenProcess": ([wintypes.DWORD, wintypes.BOOL, wintypes.DWORD], wintypes.HANDLE),
            "SetInformationJobObject": (
                [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            "AssignProcessToJobObject": ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (args, result) in signatures.items():
            function = getattr(kernel, name)
            function.argtypes, function.restype = args, result
        process = None
        try:
            self.handle = self._checked(kernel.CreateJobObjectW(None, None))
            limits = _Limits()
            limits.basic.flags = 0x2000 | 0x200  # KILL_ON_JOB_CLOSE | JOB_MEMORY
            limits.job = maximum_bytes
            self._checked(kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
            process = self._checked(kernel.OpenProcess(0x100 | 0x1, False, pid))  # SET_QUOTA | TERMINATE
            self._checked(kernel.AssignProcessToJobObject(self.handle, process))
        except Exception:
            self.close()
            raise
        finally:
            if process:
                kernel.CloseHandle(process)

    @staticmethod
    def _checked(value):
        if not value:
            raise ctypes.WinError(ctypes.get_last_error())
        return value

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None
