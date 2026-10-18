#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Shared CMake entry point for the shell wrappers and repo commands."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]


def replace_directory(source: Path, target: Path) -> None:
    """Bound retries for Windows readers finishing with freshly installed files."""
    for attempt in range(8):
        try:
            source.replace(target)
            return
        except OSError as error:
            # Scanner/read handles can briefly prevent directory renames. Do not
            # retry unrelated failures or turn a persistent denial into success.
            if getattr(error, "winerror", None) not in {5, 32, 33} or attempt == 7:
                raise
            time.sleep(min(0.05 * 2**attempt, 1.0))


def read_cache(build_dir: Path) -> dict[str, str]:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return {}
    values = {}
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line and not line.startswith(("#", "//")) and ":" in line and "=" in line:
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def runtime_directory(build_dir: Path, config: str) -> Path:
    """Resolve the CMake layout, including single-config generators on Windows."""
    cache = read_cache(build_dir)
    multi_config = bool(cache.get("CMAKE_CONFIGURATION_TYPES"))
    if not cache:
        multi_config = sys.platform == "win32"
    runtime = build_dir / "bin"
    return runtime / config.capitalize() if multi_config else runtime


def cache_enabled(cache: dict[str, str], key: str) -> bool:
    value = cache.get(key, "").upper()
    return value not in {"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"} and not value.endswith("-NOTFOUND")


def host_platform() -> str:
    machine = platform.machine().lower()
    if machine not in {"amd64", "x86_64", "arm64", "aarch64"}:
        raise ValueError(f"no pinned dependency package for host architecture {machine}")
    arch = "aarch64" if machine in {"arm64", "aarch64"} else "x86_64"
    system = "windows" if sys.platform == "win32" else "macos" if sys.platform == "darwin" else "linux"
    return f"{system}-{arch}"


def fetch_dependencies() -> None:
    """Fetch Python/pybind11 without project generation or compiler discovery."""
    suffix = ".cmd" if sys.platform == "win32" else ""
    manifest = "target-deps-python-macos.packman.xml" if sys.platform == "darwin" else "target-deps-python.packman.xml"
    target = host_platform()
    # The pinned Linux interpreter uses the existing manylinux 2.35 artifact
    # identity. This selects fetched Python, not the caller's CMake compiler ABI.
    interpreter_platform = f"manylinux_2_35_{target.split('-', 1)[1]}" if sys.platform == "linux" else target
    subprocess.run(
        [
            str(ROOT / "tools/packman" / f"packman{suffix}"),
            "pull",
            str(ROOT / "deps" / manifest),
            "-p",
            target,
            "-t",
            f"platform_target_abi={interpreter_platform}",
        ],
        cwd=ROOT,
        check=True,
    )


def add_arguments(parser: argparse.ArgumentParser, action: str) -> None:
    parser.set_defaults(action=action)
    if action == "fetch":
        return
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path(os.environ.get("SMLIB_CMAKE_BUILD_DIR", ROOT / "_build/cam")),
        help="CMake build tree (or SMLIB_CMAKE_BUILD_DIR)",
    )
    parser.add_argument("-c", "--config", type=str.lower, choices=("debug", "release"), default="release")
    parser.add_argument("-r", "--release", dest="config", action="store_const", const="release")
    parser.add_argument("-d", "--debug", dest="config", action="store_const", const="debug")
    parser.add_argument("-j", "--parallel", type=int, default=4)
    parser.add_argument("--target", nargs="+", help="build selected CMake targets without staging a full SDK")
    parser.add_argument("-G", "--generator", help="CMake generator for a new build tree")
    parser.add_argument("-D", dest="definitions", action="append", default=[], metavar="NAME=VALUE")
    for name in ("python", "dev", "validation", "examples"):
        parser.add_argument(f"--{name}", action=argparse.BooleanOptionalAction, default=None)
    parser.add_argument("--fetch", action="store_true", help="fetch Python/pybind11 before configuring")
    parser.add_argument("--fetch-only", action="store_true", help="fetch dependencies without configuring")
    parser.add_argument("-g", "--configure-only", action="store_true")
    parser.add_argument("--no-install", action="store_true", help="skip the native SDK archive staging tree")
    parser.add_argument("-R", "--tests-regex", help="CTest name filter")


def run(args: argparse.Namespace) -> None:
    if args.action == "fetch" or args.fetch_only or args.fetch:
        fetch_dependencies()
    if args.action == "fetch" or args.fetch_only:
        return

    build_dir = args.build_dir.resolve()
    config = args.config.capitalize()
    if args.parallel < 1:
        raise ValueError("--parallel must be positive")
    if args.action == "test":
        cache = read_cache(build_dir)
        if not cache_enabled(cache, "SMLIB_BUILD_VALIDATION"):
            raise ValueError("configure/build with --validation before running tests")
        if args.dev and not cache_enabled(cache, "SMLIB_BUILD_DEV"):
            raise ValueError("configure/build with --dev before requesting developer tests")
        command = ["ctest", "--test-dir", str(build_dir), "-C", config, "--output-on-failure", "--no-tests=error"]
        if args.tests_regex:
            command += ["-R", args.tests_regex]
        subprocess.run(command, cwd=ROOT, check=True)
        return

    command = ["cmake", "-S", str(ROOT), "-B", str(build_dir), f"-DCMAKE_BUILD_TYPE={config}"]
    if args.generator:
        command += ["-G", args.generator]
    # Unspecified feature flags retain an existing cache's choices. A focused
    # rebuild must not silently disable the validation/dev targets it relies on.
    for name in ("python", "dev", "validation", "examples"):
        value = getattr(args, name)
        if value is not None:
            command += [f"-DSMLIB_BUILD_{name.upper()}={'ON' if value else 'OFF'}"]
    if os.environ.get("SMLIB_PYTHON_ROOT"):
        command += [f"-DPython3_ROOT_DIR={os.environ['SMLIB_PYTHON_ROOT']}"]
    command += [f"-D{definition}" for definition in args.definitions]
    subprocess.run(command, cwd=ROOT, check=True)
    if args.action == "configure" or args.configure_only:
        return

    command = ["cmake", "--build", str(build_dir), "--config", config, "--parallel", str(args.parallel)]
    if args.target:
        command += ["--target", *args.target]
    subprocess.run(command, cwd=ROOT, check=True)
    if not args.no_install and not args.target:
        # Publish a complete fresh install so failed installs or retired optional
        # outputs cannot contaminate SDK archives. Only the checkout-owned staging
        # tree is replaceable; arbitrary CMake install prefixes are untouched.
        staging_root = (ROOT / "_build/install").resolve()
        stage = (staging_root / args.config).resolve()
        if not staging_root.is_relative_to(ROOT.resolve()) or stage.parent != staging_root:
            raise ValueError(f"SDK staging path escapes its owning directory: {stage}")
        staging_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".sdk-", dir=staging_root) as temporary:
            subprocess.run(
                ["cmake", "--install", str(build_dir), "--config", config, "--prefix", temporary],
                cwd=ROOT,
                check=True,
            )
            # On Windows a deleted directory can remain pending while readers
            # release handles. Move the old SDK aside instead of deleting and
            # immediately reusing its path; restore it if publication fails.
            previous = staging_root / f".previous-{uuid.uuid4().hex}"
            if stage.exists():
                replace_directory(stage, previous)
            try:
                replace_directory(Path(temporary), stage)
            except OSError:
                if previous.exists():
                    replace_directory(previous, stage)
                raise
            if previous.exists():
                try:
                    shutil.rmtree(previous)
                except OSError as error:
                    # The new SDK is complete; an in-use previous copy is only
                    # cleanup debt and is outside the archive's selected tree.
                    print(f"Previous SDK retained at {previous}: {error}", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    for action in ("build", "configure", "fetch", "test"):
        add_arguments(commands.add_parser(action), action)
    try:
        run(parser.parse_args(argv))
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"smlib_build: {error}", file=sys.stderr)
        return error.returncode if isinstance(error, subprocess.CalledProcessError) else 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
