#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Suggest SMLib validation commands from changed paths."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Recommendation:
    name: str
    first: tuple[str, ...]
    broader: tuple[str, ...]


BUILD_COMMAND = "cmake --build _build/cam-tools --config Release --parallel 4"
TEST_COMMAND = "ctest --test-dir _build/cam-tools -C Release --output-on-failure"
DOC_CHECKS = Recommendation("Documentation", ("git diff --check", "check changed local links and paths"), ())

RECOMMENDATIONS = (
    (
        "prog_test focused suites",
        ("tests/prog_test/",),
        Recommendation(
            "prog_test focused suites",
            (
                BUILD_COMMAND + " --target prog_test_app",
                "./tests/prog_test/prog_test.sh linux-x86_64 --list-suites",
            ),
            ("./tests/prog_test/prog_test.sh linux-x86_64",),
        ),
    ),
    (
        "SmPyLib / Python bindings",
        (
            "source/SmPyLib/",
            "source/SmPyDevLib/",
        ),
        Recommendation(
            "SmPyLib / Python bindings",
            (BUILD_COMMAND, TEST_COMMAND + " -R python_geometry"),
            (TEST_COMMAND, "use run_geometry_tests.py --dev against the selected runtime for dev binding changes"),
        ),
    ),
    (
        "SM_API",
        ("source/SM_API/",),
        Recommendation(
            "SM_API",
            (
                BUILD_COMMAND,
                "run a focused Python repro or API-level test for the touched operation",
            ),
            (TEST_COMMAND + " -R sm_api", TEST_COMMAND),
        ),
    ),
    (
        "SMLib Python GUI",
        (
            "tools/smlib_gui/",
            "tools/scripts/smlib_gui",
        ),
        Recommendation(
            "SMLib Python GUI",
            ("uv run --locked --group gui python tools/scripts/smlib_gui_smoke.py",),
            (
                "uv run --locked --group gui python tools/scripts/smlib_gui_render.py INPUT "
                "--output-dir /tmp/smlib_gui_renders",
            ),
        ),
    ),
    (
        "SMLib kernel",
        ("source/SMLib/",),
        Recommendation(
            "SMLib kernel",
            (BUILD_COMMAND, "run a minimal repro through SM_API or _omni_solid"),
            ("run the relevant C++ suite", TEST_COMMAND),
        ),
    ),
    (
        "Native STEP import and evidence tools",
        (
            "source/STEP_SOURCE/",
            "source/STEP_SEMANTICS/",
            "source/STEP_PLANAR/",
            "source/STEP_IMPORT/",
            "tools/step_",
        ),
        Recommendation(
            "Native STEP import and evidence tools",
            (BUILD_COMMAND, TEST_COMMAND + " -R step_"),
            ("run the owning tools/step_* tests from its README",),
        ),
    ),
    (
        "Agent docs / tooling",
        (
            ".agents/",
            "AGENTS.md",
        ),
        DOC_CHECKS,
    ),
    (
        "Agent tooling",
        ("tools/agent/",),
        Recommendation("Agent tooling", ("run the changed tool on focused inputs", "git diff --check"), ()),
    ),
    (
        "Build / packaging",
        (
            "CMakeLists.txt",
            "repo.toml",
            "repo.sh",
            "repo.bat",
            "build.sh",
            "build.bat",
            "tests.sh",
            "tests.bat",
            "repo_tools.toml",
            "prebuild",
            "deps/",
            "cmake/",
            "tools/scripts/smlib_build.py",
            "tools/repoman/",
            "tools/pyproject/",
        ),
        Recommendation(
            "Build / packaging",
            (BUILD_COMMAND,),
            (TEST_COMMAND,),
        ),
    ),
)


PROG_TEST_SUITE_RULES = (
    (("merge", "boolean", "classif"), ("booleans",)),
    (("fillet", "blend", "chamfer"), ("fillets", "local-ops")),
    (("offset", "shell"), ("offset",)),
    (("sweep",), ("sweeps",)),
    (("primitive", "cone", "cylinder", "sphere", "torus"), ("primitives",)),
    (("tess", "poly", "raytracer", "grid"), ("tessellation",)),
    (("heal",), ("healing",)),
    (
        ("trim", "topolog", "brep", "face", "edge", "vertex", "loop"),
        ("topology", "healing", "trimmed-surfaces", "brep-import"),
    ),
    (("surfaceintersector", "ssi"), ("ssi-analytic", "ssi-advanced")),
    (("curve", "cci"), ("cci-advanced",)),
    (("section",), ("section-advanced",)),
    (("silhouette",), ("silhouette-advanced",)),
)


def changed_paths_from_git() -> list[str]:
    commands = (
        ("git", "diff", "--name-only", "HEAD", "--"),
        ("git", "diff", "--name-only", "--"),
        (
            "git",
            "-c",
            "filter.lfs.process=",
            "-c",
            "filter.lfs.clean=cat",
            "-c",
            "filter.lfs.required=false",
            "diff",
            "--name-only",
            "HEAD",
            "--",
        ),
        (
            "git",
            "-c",
            "filter.lfs.process=",
            "-c",
            "filter.lfs.clean=cat",
            "-c",
            "filter.lfs.required=false",
            "diff",
            "--name-only",
            "--",
        ),
    )
    paths: list[str] = []
    for command in commands:
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        if result.returncode == 0:
            paths.extend(line.strip() for line in result.stdout.splitlines() if line.strip())
            break

    untracked = subprocess.run(
        ("git", "ls-files", "--others", "--exclude-standard"),
        text=True,
        capture_output=True,
        check=False,
    )
    if untracked.returncode == 0:
        paths.extend(line.strip() for line in untracked.stdout.splitlines() if line.strip())

    return list(dict.fromkeys(paths))


def classify(path: str) -> list[Recommendation]:
    if Path(path).suffix.lower() in {".md", ".rst"}:
        return [DOC_CHECKS]
    matches: list[Recommendation] = []
    for _label, prefixes, recommendation in RECOMMENDATIONS:
        for prefix in prefixes:
            if prefix.endswith("/"):
                if path.startswith(prefix):
                    matches.append(recommendation)
                    break
            elif path == prefix or path.startswith(prefix):
                matches.append(recommendation)
                break
    return matches


def focused_prog_test_suites(path: str) -> list[str]:
    if not path.startswith("source/SMLib/"):
        return []

    lowered = Path(path).name.lower()
    suites: list[str] = []
    for needles, rule_suites in PROG_TEST_SUITE_RULES:
        if any(needle in lowered for needle in needles):
            suites.extend(rule_suites)
    return suites


def unique(items: list[str]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for item in items:
        if item not in seen:
            seen.add(item)
            result.append(item)
    return result


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--paths", nargs="*", help="Changed paths to classify instead of reading git diff")
    args = parser.parse_args(argv)

    paths = args.paths if args.paths is not None else changed_paths_from_git()
    paths = [path for path in paths if path]

    if not paths:
        print("No changed paths found. Use --paths to classify explicit files.")
        return 0

    matched: list[Recommendation] = []
    unknown: list[str] = []
    focused_suite_commands: list[str] = []
    for path in paths:
        path_matches = classify(path)
        if path_matches:
            matched.extend(path_matches)
        else:
            unknown.append(path)
        focused_suite_commands.extend(
            f"./tests/prog_test/prog_test.sh linux-x86_64 --suite {suite}" for suite in focused_prog_test_suites(path)
        )

    focused_suite_commands = unique(focused_suite_commands)
    if focused_suite_commands:
        matched.append(
            Recommendation(
                "Focused prog_test suites",
                tuple(focused_suite_commands),
                ("./tests/prog_test/prog_test.sh linux-x86_64",),
            )
        )

    matched_names = unique([item.name for item in matched])
    first = unique([command for item in matched for command in item.first])
    broader = unique([command for item in matched for command in item.broader])

    print("Changed paths:")
    for path in paths:
        print(f"  - {path}")

    if matched_names:
        print("\nMatched areas:")
        for name in matched_names:
            print(f"  - {name}")

    if unknown:
        print("\nUnclassified paths:")
        for path in unknown:
            print(f"  - {path}")
        if not first:
            first.append(BUILD_COMMAND)

    print("\nFirst validation:")
    if first:
        for command in first:
            print(f"  - {command}")
    else:
        print("  - no validation required")

    print("\nBroader validation:")
    if broader:
        for command in broader:
            print(f"  - {command}")
    else:
        print("  - none")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
