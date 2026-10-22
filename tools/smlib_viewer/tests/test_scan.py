# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Public fixtures through the real isolated viewer, plus process-tree controls."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from tools.smlib_viewer.receipt import RequestReceipt
from tools.smlib_viewer.progress import Progress
from tools.smlib_viewer.scan import discover, run_case, run_scan
from tools.smlib_viewer.receipt import write_json

REPO = Path(__file__).resolve().parents[3]
BOX = REPO / "tools/step_import_validation/corpus/ap203e2-planar/v1/cases/S203-P04/input.step"


def options(**values):
    defaults = dict(scan=[], output=None, profile="development", normalize_periodic_faces=False,
                    restrict_unused_support_domains=False, strict_source=False, edges=False,
                    wireframe=False, thumbnails=False, timeout=150,
                    schema_interpretation="DeclaredOnly", carrier_interpretation="DeclaredOnly", report_budget_mib=16,
                    preserve_opaque_file_name_text=False, assume_ascii_invariant_data=False)
    return SimpleNamespace(**(defaults | values))


class ScanContracts(unittest.TestCase):
    def test_stage_entry_does_not_establish_stage_completion(self):
        receipt = RequestReceipt(BOX)
        for phase in ["capture", "check", "build", "render"]:
            receipt.progress(Progress(phase))
        result = receipt.finish("LoadFailure", {"status": "DisplayFailure"})
        self.assertIsNone(result["native_import_status"])
        self.assertEqual({"import": None, "mesh": False, "render": False}, result["stages"])
        self.assertIs(result, receipt.finish("Rendered", {"status": "Ready"}))
        receipt = RequestReceipt(BOX)
        receipt.progress(Progress("copy"))
        result = receipt.finish("LoadFailure", {"status": "DisplayFailure"})
        self.assertEqual({"import": True, "mesh": False, "render": False}, result["stages"])
        receipt = RequestReceipt(BOX)
        result = receipt.finish("Rendered", {"status": "Ready"})
        self.assertIsNone(result["native_import_status"], "Ready does not expose core acceptance qualification")
        self.assertEqual({"import": True, "mesh": True, "render": True}, result["stages"])

    def test_discovery_overlapping_roots_and_output_rejection(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            nested = root / "nested"
            nested.mkdir()
            model = nested / "public.STEP"
            model.write_bytes(b"public")
            self.assertEqual([model.resolve()], discover([root, nested]))
            with self.assertRaisesRegex(ValueError, "OutsideInputs"):
                run_scan(options(scan=[root], output=root / "results"))
            for timeout in [0, -1, float("inf"), float("nan")]:
                with self.assertRaisesRegex(ValueError, "FinitePositive"):
                    run_scan(options(scan=[root], output=root.parent / "unused", timeout=timeout))
            self.assertEqual(b"public", model.read_bytes())

    def test_cli_refuses_output_alias_of_input(self):
        from tools.smlib_viewer.cli import main
        with tempfile.TemporaryDirectory() as name:
            source = Path(name) / "public.step"
            source.write_bytes(b"immutable public source")
            alias = Path(name) / "public.png"
            os.link(source, alias)
            with contextlib.redirect_stderr(io.StringIO()):
                for path in [source, alias]:
                    with self.assertRaises(SystemExit) as error:
                        main([str(source), "--screenshot", str(path)])
                    self.assertEqual(2, error.exception.code)
            self.assertEqual(b"immutable public source", source.read_bytes())

    def _tree_control(self, cancelled):
        with tempfile.TemporaryDirectory(prefix="brep-view-tree-") as name:
            directory = Path(name)
            # A real grandchild must die along with a timed-out/cancelled
            # renderer, including when the renderer itself has already exited.
            script = directory / "public_worker.py"
            pid_file = directory / "grandchild.pid"
            script.write_text(
                "import subprocess,sys,time,tempfile\nfrom pathlib import Path\n"
                "assert sys.stdin.readline().strip()=='start'\n"
                "(Path(tempfile.gettempdir())/'public-unpublished-payload').write_bytes(b'public payload')\n"
                "p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(60)'])\n"
                f"Path({str(pid_file)!r}).write_text(str(p.pid))\n"
                "print('@brep-view:capture,0,0',flush=True)\ntime.sleep(60)\n", encoding="utf-8")
            def caption(_):
                if cancelled:
                    raise KeyboardInterrupt
            with patch("tools.smlib_viewer.scan.viewer_command", return_value=[sys.executable, str(script)]):
                result = run_case(BOX.resolve(), directory, options(timeout=3), caption)
            self.assertEqual("Cancelled" if cancelled else "Timeout", result["outcome"])
            self.assertIsNotNone(result["process_exit_code"])
            self.assertTrue(result["temporary_payloads_cleaned"])
            self.assertFalse(list(directory.glob(".work-*")), "Unpublished payloads survived cancellation/timeout")
            self.assertTrue(pid_file.is_file(), "Synthetic descendant never started")
            pid = int(pid_file.read_text())
            if os.name == "nt":
                import ctypes
                from ctypes import wintypes
                kernel = ctypes.WinDLL("kernel32", use_last_error=True)
                kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
                kernel.OpenProcess.restype = wintypes.HANDLE
                kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
                kernel.CloseHandle.argtypes = [wintypes.HANDLE]
                handle = kernel.OpenProcess(0x100000, False, pid)
                if handle:
                    try:
                        self.assertEqual(0, kernel.WaitForSingleObject(handle, 2000), "Orphan descendant")
                    finally:
                        kernel.CloseHandle(handle)

    def test_timeout_cleans_entire_renderer_tree(self):
        self._tree_control(False)

    def test_cancel_cleans_entire_renderer_tree(self):
        self._tree_control(True)

    def test_unreported_success_is_not_rendered(self):
        with tempfile.TemporaryDirectory() as name:
            with patch("tools.smlib_viewer.scan.viewer_command",
                       return_value=[sys.executable, "-c", "import sys;sys.stdin.readline()"]):
                result = run_case(BOX.resolve(), Path(name), options(), lambda _: None)
            self.assertEqual("WorkerFailure", result["outcome"])

    def test_report_publication_does_not_rewrite_history_quadratically(self):
        volumes = []
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for count in (16, 32):
                source = root / f"input-{count}"
                source.mkdir()
                for index in range(count):
                    (source / f"public-{index:03}.step").write_text(f"public input {index}", encoding="ascii")
                output = root / f"output-{count}"
                written = []
                def publish(path, value):
                    written.append(len(json.dumps(value, ensure_ascii=False, indent=2).encode("utf-8")))
                    write_json(path, value)
                # Stub only the external renderer: this checks report I/O and
                # journal integrity, independently of Qt startup/geometry cost.
                observed = {"outcome": "LoadFailure", "details": "public diagnostic " * 500}
                with patch("tools.smlib_viewer.scan.run_case", return_value=observed), \
                        patch("tools.smlib_viewer.scan.write_json", side_effect=publish), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(1, run_scan(options(scan=[source], output=output)))
                journal = output / "records.jsonl"
                rows = [json.loads(line) for line in journal.read_text(encoding="utf-8").splitlines()]
                final = json.loads((output / "scan.json").read_text(encoding="utf-8"))
                self.assertEqual(count, len(rows))
                self.assertEqual(rows, final["records"])
                volumes.append(sum(written) + journal.stat().st_size)
            self.assertLess(volumes[1] / volumes[0], 2.5, "Doubling models must not quadruple report bytes")

    def test_cancel_preserves_completed_journal_and_incomplete_summary(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            source = root / "input"
            source.mkdir()
            for index in range(3):
                (source / f"public-{index}.step").write_text(f"public {index}", encoding="ascii")
            output = root / "output"
            with patch("tools.smlib_viewer.scan.run_case", return_value={"outcome": "Cancelled"}), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(130, run_scan(options(scan=[source], output=output)))
            final = json.loads((output / "scan.json").read_text(encoding="utf-8"))
            self.assertFalse(final["complete"])
            self.assertEqual(1, final["summary"]["processed"])
            self.assertEqual(2, final["summary"]["not_run"])
            self.assertEqual(final["records"], [json.loads((output / "records.jsonl").read_text(encoding="utf-8"))])

    def test_changed_source_cannot_suppress_unchanged_alias(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            source = root / "input"
            source.mkdir()
            data = b"public same bytes"
            for filename in ["a.step", "b.stp"]:
                (source / filename).write_bytes(data)
            identity = hashlib.sha256(data).hexdigest()
            def external_reader(path, *_):
                if path.name == "a.step":
                    # Simulate another application editing only this disposable
                    # public input. The scanner must not repair/restore it or
                    # skip an unchanged alias of the originally selected bytes.
                    path.write_bytes(b"external edit to public fixture")
                return {"outcome": "Rendered", "source_sha256": identity}
            output = root / "output"
            with patch("tools.smlib_viewer.scan.run_case", side_effect=external_reader), \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(1, run_scan(options(scan=[source], output=output)))
            final = json.loads((output / "scan.json").read_text(encoding="utf-8"))
            self.assertEqual(["SourceChanged", "Rendered"], [r["outcome"] for r in final["records"]])
            self.assertEqual(b"external edit to public fixture", (source / "a.step").read_bytes())
            self.assertEqual(data, (source / "b.stp").read_bytes())


class ScanViewerIntegration(unittest.TestCase):
    def test_box_mesh_duplicate_and_bad_step_through_viewer(self):
        # Native availability is checked without importing Qt in the scanner.
        worker = Path(os.environ.get("BREP_VIEW_WORKER", "missing-native-worker"))
        if not worker.is_file():
            self.skipTest("Set BREP_VIEW_WORKER to step_view_export")
        with tempfile.TemporaryDirectory(prefix="brep-view-scan-public-") as name:
            directory = Path(name)
            source = directory / "公开样件"
            source.mkdir()
            original = BOX.read_bytes()
            (source / "box.step").write_bytes(original)
            (source / "duplicate.stp").write_bytes(original)
            (source / "invalid.step").write_bytes(b"not STEP")
            import pyvista as pv
            pv.Cube().triangulate().save(source / "mesh.stl")
            before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()}
            output = directory / "local-results"
            with contextlib.redirect_stdout(io.StringIO()):
                code = run_scan(options(scan=[source], output=output, thumbnails=True))
            self.assertEqual(1, code)
            report = json.loads((output / "scan.json").read_text(encoding="utf-8"))
            self.assertTrue(report["complete"])
            self.assertEqual({"files": 4, "processed": 4, "rendered": 2, "duplicates": 1,
                              "failures": 1, "not_run": 0}, report["summary"])
            results = {Path(record["input"]).name: record for record in report["records"]}
            box = results["box.step"]
            self.assertEqual({"import": True, "mesh": True, "render": True}, box["stages"])
            self.assertEqual(before["box.step"], box["source_sha256"])
            self.assertEqual(box["id"], results["duplicate.stp"]["duplicate_of"])
            self.assertIsNone(results["mesh.stl"]["stages"]["import"])
            for record in report["records"]:
                if record["outcome"] == "Rendered":
                    self.assertTrue((output / record["id"] / "thumbnail.png").is_file())
                    self.assertTrue(record["original_unchanged"])
                    self.assertGreater(record["process_seconds"], 0)
            self.assertEqual(before, {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.iterdir()})
            # A previous scan is never silently overwritten or folded into a
            # new run with different settings or input identities.
            with self.assertRaisesRegex(ValueError, "MustBeNew"):
                run_scan(options(scan=[source], output=output))
