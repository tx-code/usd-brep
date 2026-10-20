# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Build a fresh Windows onedir distribution; never collect model directories."""
import argparse
import importlib.metadata
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True,
                        help="Release step_view_export.exe with adjacent matching SDK DLLs")
    parser.add_argument("--crt", type=Path, required=True,
                        help="Matching redistributable x64 Microsoft.VC*.CRT directory")
    parser.add_argument("--output", type=Path, required=True, help="New local build directory")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This portable distribution targets Windows x64")
    repo = Path(__file__).resolve().parents[2]
    worker = args.worker.resolve()
    binaries = [worker, *(worker.parent / name for name in ("SMLib.dll", "SM_API.dll")),
                *args.crt.resolve().glob("*.dll")]
    if len(binaries) < 4 or any(not path.is_file() for path in binaries):
        parser.error("Worker, both matching SDK DLLs and redistributable CRT DLLs are required")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    command = [sys.executable, "-m", "PyInstaller", "--onedir", "--name", "brep-view", "--console",
               "--hide-console", "hide-early", "--noupx", "--noconfirm", "--distpath", str(output / "dist"),
               "--workpath", str(output / "work"), "--specpath", str(output), "--paths", str(repo),
               "--paths", str(repo / "tools/scripts"), "--collect-all", "vtkmodules", "--collect-all", "pyvista",
               "--hidden-import", "smlib_runtime", "--exclude-module", "_omni_solid",
               "--exclude-module", "_smlib_dev", "--exclude-module", "_smlib_tests"]
    for binary in binaries:
        command.extend(["--add-binary", f"{binary}:native"])
    command.append(str(repo / "tools/scripts/brep_view.py"))
    # No model, census, receipt, environment dump or SDK Python extension is an
    # input to packaging. All output goes to a newly owned local directory.
    # An inherited developer PATH can supply unrelated ICU/OpenSSL/API-set DLLs
    # to dependency collection (for example a document tool's ICU). Qt then
    # fails on a clean machine despite matching Qt DLL bytes. Resolve only from
    # this Python environment, the selected CRT and the operating system.
    environment = dict(os.environ)
    for name in list(environment):
        if name.upper().startswith(("PYTHONPATH", "PYTHONHOME", "SMLIB", "QT_", "PYVISTA", "BREP_")):
            del environment[name]
    system = Path(os.environ["SystemRoot"])
    environment["PATH"] = os.pathsep.join(map(str, (
        Path(sys.executable).parent, Path(sys.base_prefix), args.crt.resolve(), system / "System32", system,
    )))
    subprocess.run(command, cwd=repo, env=environment, check=True)
    distribution = output / "dist/brep-view"
    # PyInstaller may select Python's older vcruntime while collecting a newer
    # Qt/MSVCP runtime. Use one matching, backward-compatible redistributable in
    # the GUI loader directory as well as the isolated worker directory.
    for binary in args.crt.resolve().glob("*.dll"):
        shutil.copy2(binary, distribution / "_internal" / binary.name)
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md"):
        shutil.copy2(repo / name, distribution / name)
    # Keep installed dependency notices beside the portable app. Restrict this
    # collection to wheel-declared license text, never arbitrary local data.
    for dependency in importlib.metadata.distributions():
        for entry in dependency.files or ():
            path = Path(entry)
            words = str(path).lower()
            if (any(word in words for word in ("license", "copying", "copyright", "notice", "lgpl", "gpl"))
                    and path.suffix.lower() in ("", ".txt", ".md", ".rst")):
                source = Path(dependency.locate_file(entry))
                if not source.is_file() or ".." in path.parts or path.is_absolute():
                    continue
                target = distribution / "licenses" / dependency.metadata["Name"] / path
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    python_license = Path(sys.base_prefix) / "LICENSE.txt"
    if python_license.is_file():
        shutil.copy2(python_license, distribution / "licenses/Python-LICENSE.txt")
    shutil.copy2(Path(__file__).with_name("README.md"), distribution / "README.md")
    print(distribution / "brep-view.exe")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
