# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Version contract through the wheel converter and an actual CMake consumer."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools.repoman import wheel_version as versions


ROOT = Path(__file__).resolve().parents[3]


class ReleaseVersionTests(unittest.TestCase):
    def test_canonical_prereleases_map_to_wheel_versions(self):
        for source, wheel in (
            ("1.0.1", "1.0.1"),
            ("2.0.0-alpha.1", "2.0.0a1"),
            ("2.0.0-beta.2", "2.0.0b2"),
            ("2.0.0-rc.3", "2.0.0rc3"),
        ):
            with self.subTest(source=source):
                self.assertEqual((wheel, False), versions.wheel_version(source))

    def test_documented_legacy_packaging_channels_remain_supported(self):
        for source, expected in (
            ("0.10", ("0.10", False)),
            ("0.15-EA", ("0.15a0", False)),
            ("0.16-EA-internal", ("0.16a0", True)),
            ("0.2.0-RC2", ("0.2.0rc2", False)),
        ):
            self.assertEqual(expected, versions.wheel_version(source))

    def test_repository_versions_are_strict_and_expose_one_numeric_core(self):
        parsed = versions.repository_version("2.0.0-alpha.1\n")
        self.assertEqual(("2.0.0-alpha.1", "2.0.0", "2.0.0a1"), (parsed.version, parsed.core, parsed.wheel))
        for invalid in ("2.0", "02.0.0", "2.0.0-alpha.01", "2.0.0+fork", "2.0.0-internal", "2.0.0\n1.0.0"):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                versions.repository_version(invalid)

    @unittest.skipUnless(shutil.which("cmake"), "CMake is required for the native version contract")
    def test_cmake_reads_full_version_and_reconfigures_after_version_changes(self):
        with tempfile.TemporaryDirectory(prefix="fork version ") as temporary:
            root = Path(temporary)
            version = root / "VERSION"
            version.write_text("1.0.1\n", encoding="utf-8")
            module = ROOT / "cmake/RepositoryVersion.cmake"
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.24)\n'
                f'include("{module.as_posix()}")\n'
                'smlib_read_repository_version("${CMAKE_CURRENT_SOURCE_DIR}/VERSION")\n'
                'project(Consumer VERSION "${SMLIB_VERSION_CORE}" LANGUAGES NONE)\n'
                'file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/observed.txt" "${SMLIB_VERSION}|${PROJECT_VERSION}")\n',
                encoding="utf-8",
            )
            build = root / "build"
            subprocess.run(["cmake", "-S", str(root), "-B", str(build)], check=True, capture_output=True)
            self.assertEqual("1.0.1|1.0.1", (build / "observed.txt").read_text())
            version.write_text("2.0.0-alpha.1\n", encoding="utf-8")
            subprocess.run(["cmake", "--build", str(build)], check=True, capture_output=True)
            self.assertEqual("2.0.0-alpha.1|2.0.0", (build / "observed.txt").read_text())


if __name__ == "__main__":
    unittest.main()
