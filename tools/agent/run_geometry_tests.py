#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Run geometry SDK tests against an explicit native runtime directory."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", nargs="?", choices=["windows-x86_64"], help=argparse.SUPPRESS)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--native", action="store_true", help="also require the native validation executables")
    parser.add_argument("--dev", action="store_true", help="also require and test debugger bindings")
    parser.add_argument("--target", help="run one unittest target instead of discovery")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    runtime = args.runtime.resolve()
    if not any(runtime.glob("_omni_solid*.pyd")) and not any(runtime.glob("_omni_solid*.so")):
        parser.error(f"stable Python extension is missing: {runtime}")
    env = os.environ.copy()
    test_paths = (runtime, root / "source/SmPyLib/tests", root / "source/SmPyDevLib/tests")
    env["PYTHONPATH"] = os.pathsep.join(map(str, test_paths))
    env["PATH"] = str(runtime) + os.pathsep + env.get("PATH", "")
    for key in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH"):
        env[key] = str(runtime) + os.pathsep + env.get(key, "")
    env.pop("OMNISOLID_PLUGIN_PATH", None)
    env.pop("PXR_PLUGINPATH_NAME", None)
    # Subprocesses isolate native lifetime/error state between independent
    # suites. A missing required binary is a failure, never a silent skip.
    def run(command, cwd=root):
        subprocess.run(command, cwd=cwd, env=env, check=True)

    if args.native:
        suffix = ".exe" if sys.platform == "win32" else ""
        run([str(runtime / f"planar_regions{suffix}")])
        run([str(runtime / f"SM_API_test_app{suffix}")], root / "tests/SM_API_test")
        run([str(runtime / f"prog_test_app{suffix}"), "--suite", "primitives"], root / "tests/prog_test")
    if args.target:
        run([sys.executable, "-m", "unittest", args.target, "-v"])
    else:
        run([sys.executable, "-m", "unittest", "discover", "-s", "source/SmPyLib/tests", "-p", "test_*.py", "-v"])
    if args.dev:
        run([sys.executable, "-c", "import _omni_solid; import _smlib_dev; import _smlib_tests"])
        run([sys.executable, "-m", "unittest", "discover", "-s", "source/SmPyDevLib/tests", "-p", "test_*.py", "-v"])


if __name__ == "__main__":
    main()
