# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Build-entry contracts: cache choices, subprocess failures, and SDK publication."""

import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.scripts import smlib_build as build


class BuildEntryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="smlib build ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.root_patch = patch.object(build, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def args(self, *values, action="build"):
        parser = argparse.ArgumentParser()
        build.add_arguments(parser, action)
        return parser.parse_args(["--build-dir", str(self.root / "tree with spaces"), *values])

    def test_configure_preserves_cached_features_and_argv_boundaries(self):
        with patch.object(build.subprocess, "run") as command:
            build.run(self.args("--no-python", "-DSOME_PATH=path with spaces", action="configure"))
        argv = command.call_args.args[0]
        self.assertIn(str(self.root / "tree with spaces"), argv)
        self.assertIn("-DSOME_PATH=path with spaces", argv)
        self.assertIn("-DSMLIB_BUILD_PYTHON=OFF", argv)
        self.assertFalse(any(arg.startswith("-DSMLIB_BUILD_DEV=") for arg in argv))
        self.assertEqual(1, command.call_count)

    def test_target_build_does_not_install_an_incomplete_sdk(self):
        with patch.object(build.subprocess, "run") as command:
            build.run(self.args("--target", "SMLib"))
        self.assertEqual(2, command.call_count)
        self.assertEqual(["--target", "SMLib"], command.call_args.args[0][-2:])

    def test_build_failure_never_stages(self):
        with patch.object(
            build.subprocess, "run", side_effect=[None, subprocess.CalledProcessError(7, "cmake")]
        ) as command:
            with self.assertRaises(subprocess.CalledProcessError):
                build.run(self.args())
        self.assertEqual(2, command.call_count)
        self.assertFalse((self.root / "_build/install").exists())

    def stage_old_sdk(self):
        stage = self.root / "_build/install/release"
        stage.mkdir(parents=True)
        (stage / "obsolete-dev.pyd").write_text("old", encoding="utf-8")
        return stage

    @staticmethod
    def install_output(argv, **_kwargs):
        if argv[:2] == ["cmake", "--install"]:
            stage = Path(argv[argv.index("--prefix") + 1])
            (stage / "sdk.txt").write_text("complete", encoding="utf-8")

    def test_successful_install_replaces_stale_optional_outputs(self):
        stage = self.stage_old_sdk()
        with patch.object(build.subprocess, "run", side_effect=self.install_output):
            build.run(self.args())
        self.assertEqual(["sdk.txt"], [path.name for path in stage.iterdir()])

    def test_failed_install_preserves_previous_sdk_and_cleans_partial_stage(self):
        stage = self.stage_old_sdk()

        def fail_install(argv, **kwargs):
            self.install_output(argv, **kwargs)
            if argv[:2] == ["cmake", "--install"]:
                raise subprocess.CalledProcessError(9, argv)

        with patch.object(build.subprocess, "run", side_effect=fail_install):
            with self.assertRaises(subprocess.CalledProcessError):
                build.run(self.args())
        self.assertTrue((stage / "obsolete-dev.pyd").exists())
        self.assertEqual([stage], list(stage.parent.iterdir()))

    def test_staging_escape_is_rejected_before_removal(self):
        args = self.args()
        args.config = ".."
        with patch.object(build.subprocess, "run"), self.assertRaisesRegex(ValueError, "escapes"):
            build.run(args)
        self.assertTrue(self.root.exists())

    def test_publication_failure_restores_previous_sdk(self):
        stage = self.stage_old_sdk()
        replace = Path.replace

        def fail_publish(source, target):
            if source.name.startswith(".sdk-"):
                raise PermissionError("Windows directory publication failed")
            return replace(source, target)

        with patch.object(build.subprocess, "run", side_effect=self.install_output):
            with patch.object(Path, "replace", autospec=True, side_effect=fail_publish):
                with self.assertRaises(PermissionError):
                    build.run(self.args())
        self.assertTrue((stage / "obsolete-dev.pyd").exists())
        self.assertEqual([stage], list(stage.parent.iterdir()))

    def test_test_entry_requires_validation_configuration(self):
        with patch.object(build.subprocess, "run") as command:
            with self.assertRaisesRegex(ValueError, "--validation"):
                build.run(self.args(action="test"))
        command.assert_not_called()

    def test_test_entry_accepts_cmake_true_boolean_spellings(self):
        args = self.args("--dev", action="test")
        args.build_dir.mkdir()
        (args.build_dir / "CMakeCache.txt").write_text(
            "SMLIB_BUILD_VALIDATION:BOOL=1\nSMLIB_BUILD_DEV:BOOL=TRUE\n", encoding="utf-8"
        )
        with patch.object(build.subprocess, "run") as command:
            build.run(args)
        self.assertEqual("ctest", command.call_args.args[0][0])

    def test_transient_windows_sharing_denial_is_retried(self):
        source = self.root / "source"
        target = self.root / "published"
        source.mkdir()
        replace = Path.replace
        sharing = PermissionError("reader still holds a handle")
        sharing.winerror = 32
        attempts = []

        def transient_move(path, destination):
            attempts.append(destination)
            if len(attempts) == 1:
                raise sharing
            return replace(path, destination)

        with (
            patch.object(Path, "replace", autospec=True, side_effect=transient_move),
            patch.object(build.time, "sleep"),
        ):
            build.replace_directory(source, target)
        self.assertTrue(target.is_dir())
        self.assertEqual(2, len(attempts))

    def test_runtime_layout_uses_generator_instead_of_host_os(self):
        tree = self.root / "tree"
        tree.mkdir()
        (tree / "CMakeCache.txt").write_text("CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n", encoding="utf-8")
        self.assertEqual(tree / "bin/Release", build.runtime_directory(tree, "release"))
        (tree / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Release\n", encoding="utf-8")
        self.assertEqual(tree / "bin", build.runtime_directory(tree, "release"))

    def test_fetch_only_never_runs_compiler_discovery(self):
        with patch.object(build, "fetch_dependencies") as fetch, patch.object(build.subprocess, "run") as command:
            build.run(self.args("--fetch-only"))
        fetch.assert_called_once_with()
        command.assert_not_called()

    def test_linux_fetch_preserves_pinned_interpreter_artifact(self):
        with (
            patch.object(build.sys, "platform", "linux"),
            patch.object(build.platform, "machine", return_value="x86_64"),
        ):
            with patch.object(build.subprocess, "run") as command:
                build.fetch_dependencies()
        argv = command.call_args.args[0]
        self.assertIn("platform_target_abi=manylinux_2_35_x86_64", argv)
        self.assertNotIn("cmake", argv)


if __name__ == "__main__":
    unittest.main()
