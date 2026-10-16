# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Package behavior through existing stage run/check interfaces and real native executables."""
from __future__ import annotations

import os
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

from tools.step_planar_validation import runner as planar
from tools.step_semantic_validation import runner as semantic
from tools.step_inspect_validation import runner as source
from tools.step_import_validation.common import InvalidEvidence, load_json, write_json

CORPUS = Path(__file__).resolve().parents[2] / "step_import_validation/corpus/ap203e2-planar/v1"


class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = Path(os.environ["STEP_PLANAR_BINARY"]).resolve(strict=True)
        cls.stages = [
            ("02", source, Path(os.environ["STEP_INSPECT_BINARY"]).resolve(strict=True)),
            ("03", semantic, Path(os.environ["STEP_SEMANTIC_BINARY"]).resolve(strict=True)),
            ("04", planar, cls.binary),
        ]
        cls.temporary = tempfile.TemporaryDirectory(prefix="step-evidence-")
        cls.root = Path(cls.temporary.name).resolve()
        cls.packages = {}

    @classmethod
    def tearDownClass(cls):
        assert cls.root.parent == Path(tempfile.gettempdir()).resolve() and cls.root.name.startswith("step-evidence-")
        cls.temporary.cleanup()

    def test_planar_report_names_its_own_validation_scope(self):
        package = planar.run(self.binary, CORPUS, self.root / "scope", ["S203-P01"])
        report = (package / "cases/S203-P01/case.html").read_text(encoding="utf-8")
        self.assertTrue("04当前范围合同" in report, "The planar package must label its actual stage")
        self.assertFalse("03当前范围合同" in report, "A copied semantic stage label survived")

    def test_all_adapters_declare_shared_protocol_and_execution_dependencies(self):
        for ticket, adapter, binary in self.stages:
            with self.subTest(ticket=ticket):
                selected = ["S203-N02", "S203-P01", "S203-P11"]
                package = adapter.run(binary, CORPUS, self.root / ("shared-" + ticket), selected)
                metadata = load_json(package / "run.json")
                self.assertEqual("step-evidence.v2", metadata.get("package_protocol"))
                self.assertEqual("execution-dependencies.v2", metadata["source_provenance"])
                self.assertIn("source/STEP_SOURCE/inc/StepSourceReport.h", metadata["sources"])
                self.assertEqual(selected, metadata["selected"])
                self.assertTrue("tools/step_evidence/package.py" in metadata["sources"])
                self.assertTrue("tools/step_import_validation/corpus.py" in metadata["sources"])
                self.assertEqual("Complete", adapter.check(package)["evidence_completeness"])

    def test_new_dependency_inventory_requires_shared_report_sources(self):
        for ticket, _, _ in self.stages:
            with self.subTest(ticket=ticket):
                adapter, package = self.copy_package(ticket, "missing-shared-report")
                path = package / "run.json"
                metadata = load_json(path)
                metadata["sources"].pop("source/STEP_SOURCE/src/StepSourceReport.cpp")
                write_json(path, metadata)
                self.reseal(package)
                with self.assertRaises(InvalidEvidence):
                    adapter.check(package)

    def test_report_locations_across_newlines_eof_and_capture_failure(self):
        lf = (CORPUS / "cases/S203-N02/input.step").read_bytes()
        for ticket, _, binary in self.stages:
            for data in (lf, lf.replace(b"\n", b"\r\n"), lf.replace(b"\n", b"\r"), b"", b"\r\n"):
                with self.subTest(ticket=ticket, prefix=data[:20]):
                    path = self.root / "location.step"
                    path.write_bytes(data)
                    command = [str(binary), "--fixture-limits"]
                    if ticket == "04":
                        command += ["--quality", "Q-P0-mm-01"]
                    result = subprocess.run(command + [str(path)], capture_output=True, timeout=15)
                    self.assertEqual(1, result.returncode, result.stderr)
                    report = json.loads(result.stdout)
                    layers = [report]
                    if ticket == "04":
                        layers.append(report["source_semantics"])
                    if ticket != "02":
                        layers.append(layers[-1]["source"])
                    for layer in layers:
                        for diagnostic in layer["diagnostics"]:
                            prefix = data[:diagnostic["span"][0]].replace(b"\r\n", b"\n").replace(b"\r", b"\n")
                            lines = prefix.split(b"\n")
                            self.assertEqual({"line": len(lines), "column": len(lines[-1]) + 1},
                                             diagnostic["location"])
                    limited = subprocess.run(command + [str(path), "--max-input-bytes", "0"],
                                             capture_output=True, timeout=15)
                    if data:
                        self.assertEqual(3, limited.returncode)
                        self.assertIsNone(json.loads(limited.stdout)["diagnostics"][0]["location"])

    def copy_package(self, ticket, label):
        _, adapter, binary = next(s for s in self.stages if s[0] == ticket)
        if ticket not in self.packages:
            self.packages[ticket] = adapter.run(binary, CORPUS, self.root / ("seed-" + ticket), ["S203-P01"])
        seed = self.packages[ticket]
        target = self.root / label / ticket / "runs" / seed.name
        shutil.copytree(seed, target)
        return adapter, target

    @staticmethod
    def reseal(package):
        # A corruption injector controls the filesystem. Resealing must not make a contradiction valid.
        refs = []
        for path in sorted(package.rglob("*")):
            if path.is_file() and path.name not in {"artifacts.json", "completion.json"}:
                data = path.read_bytes()
                refs.append({"path": path.relative_to(package).as_posix(), "role": "evidence",
                             "sha256": hashlib.sha256(data).hexdigest(), "byte_length": len(data)})
        metadata = load_json(package / "run.json")
        write_json(package / "artifacts.json", {"format_version": metadata["format_version"], "artifacts": refs})
        write_json(package / "completion.json", {"format_version": metadata["format_version"],
                   "run_id": metadata["run_id"],
                   "artifacts_sha256": hashlib.sha256((package / "artifacts.json").read_bytes()).hexdigest()})

    def test_preparation_failure_cannot_be_resealed_as_complete(self):
        for ticket, _, _ in self.stages:
            with self.subTest(ticket=ticket):
                adapter, package = self.copy_package(ticket, "preparation-failure")
                path = package / "cases/S203-P01/preparation.json"
                value = load_json(path)
                value["status"] = "Failed"
                write_json(path, value)
                self.reseal(package)
                with self.assertRaises(InvalidEvidence):
                    adapter.check(package)

    def test_unindexed_payload_is_not_a_complete_package(self):
        for ticket, _, _ in self.stages:
            with self.subTest(ticket=ticket):
                adapter, package = self.copy_package(ticket, "unindexed")
                (package / "unrecorded.txt").write_text("not in the retained payload index", encoding="utf-8")
                with self.assertRaises(InvalidEvidence):
                    adapter.check(package)

    def test_saved_provenance_cannot_contradict_its_evidence_after_resealing(self):
        variants = ("oracle-failure", "oracle-truth", "input-identity", "invocation-input",
                    "repair", "unfinished", "missing-protocol-source", "preparation-counts",
                    "input-proof-counts", "truncated-dependencies")
        for ticket, _, _ in self.stages:
            for variant in variants:
                with self.subTest(ticket=ticket, variant=variant):
                    adapter, package = self.copy_package(ticket, variant)
                    prefix = package / "cases/S203-P01"
                    if variant in {"oracle-failure", "oracle-truth", "input-proof-counts"}:
                        path = prefix / "oracle.json"
                        value = load_json(path)
                        if variant == "oracle-failure":
                            value["status"] = "Failed"
                        elif variant == "oracle-truth":
                            value["truth"]["measures"]["area"]["num"] = "97"
                        else:
                            value["input_proof"]["record_occurrences"] = 0
                            value["input_proof"]["unique_entity_ids"] = 0
                    elif variant == "preparation-counts":
                        path = prefix / "preparation.json"
                        value = load_json(path)
                        value["data_record_occurrences"] = 0
                        value["unique_entity_ids"] = 0
                    elif variant == "input-identity":
                        path = prefix / "input.receipt.json"
                        value = load_json(path)
                        value["identity"]["materials"]["input"]["sha256"] = "0" * 64
                    elif variant == "invocation-input":
                        path = prefix / "invocation.json"
                        value = load_json(path)
                        value["command"][-1] = value["command"][-1].replace("input.step", "other.step")
                    else:
                        path = package / "run.json"
                        value = load_json(path)
                        if variant == "repair":
                            value["repair"] = "Automatic"
                        elif variant == "unfinished":
                            value["finished_utc"] = None
                        elif variant == "missing-protocol-source":
                            value["sources"].pop("tools/step_evidence/package.py")
                        else:
                            keep = {"tools/step_evidence/package.py", "tools/step_evidence/presentation.py",
                                    "tools/step_import_validation/corpus.py"}
                            value["sources"] = {k: v for k, v in value["sources"].items() if k in keep}
                    write_json(path, value)
                    self.reseal(package)
                    with self.assertRaises(InvalidEvidence):
                        adapter.check(package)

    def test_nested_completion_named_file_is_still_payload(self):
        adapter, package = self.copy_package("04", "shadow-completion")
        (package / "cases/S203-P01/completion.json").write_text("{}", encoding="ascii")
        with self.assertRaises(InvalidEvidence):
            adapter.check(package)

    def test_new_protocol_rejects_indexed_but_undeclared_payload(self):
        adapter, package = self.copy_package("04", "indexed-extra")
        (package / "extra-observation.txt").write_text("outside the selected package contract", encoding="ascii")
        self.reseal(package)
        with self.assertRaises(InvalidEvidence):
            adapter.check(package)

    def test_wrong_stage_failure_leaves_partial_and_retry_publishes_a_new_run(self):
        output = self.root / "retry"
        with self.assertRaises((InvalidEvidence, KeyError)):
            semantic.run(self.stages[0][2], CORPUS, output, ["S203-P01"])
        partials = list((output / "staging").glob("*.partial"))
        self.assertEqual(1, len(partials))
        self.assertFalse((partials[0] / "completion.json").exists())
        self.assertFalse((output / "runs").exists())
        valid = semantic.run(self.stages[1][2], CORPUS, output, ["S203-P01"])
        self.assertNotEqual(partials[0].name, valid.name + ".partial")
        self.assertEqual(1, semantic.check(valid)["scope_contract_matched"])
        self.assertTrue(partials[0].is_dir(), "A retry must retain the original failed attempt")

    def test_relocated_package_rechecks_after_captured_binary_is_removed(self):
        for ticket, adapter, binary in self.stages:
            with self.subTest(ticket=ticket):
                captured = self.root / ("captured-" + ticket + ".exe")
                shutil.copyfile(binary, captured)
                original = adapter.run(captured, CORPUS, self.root / ("offline-" + ticket), ["S203-P01"])
                target = self.root / ("relocated-" + ticket) / "runs" / original.name
                shutil.copytree(original, target)
                assert captured.resolve().parent == self.root
                captured.unlink()
                invocation = load_json(target / "cases/S203-P01/invocation.json")
                self.assertFalse(Path(invocation["command"][-1]).exists())
                self.assertEqual("Complete", adapter.check(target)["evidence_completeness"])

    def test_alias_cannot_present_staging_as_a_published_run(self):
        adapter, package = self.copy_package("04", "alias")
        stage = package.parent.parent / "staging" / (package.name + ".partial")
        stage.parent.mkdir()
        assert package.resolve().is_relative_to(self.root) and stage.resolve().is_relative_to(self.root)
        package.rename(stage)
        self.assertEqual("Complete", adapter.check(stage, staged=True)["evidence_completeness"])
        if os.name == "nt":
            result = subprocess.run(["cmd", "/c", "mklink", "/J", str(package), str(stage)],
                                    capture_output=True, timeout=10)
            self.assertEqual(0, result.returncode, "The Windows junction control could not be created")
        else:
            package.symlink_to(stage, target_is_directory=True)
        self.assertEqual(stage.resolve(), package.resolve())
        with self.assertRaises(InvalidEvidence):
            adapter.check(package)

    def test_canonical_artifact_identity_rejects_dot_alias(self):
        adapter, package = self.copy_package("04", "dot-alias")
        index = load_json(package / "artifacts.json")
        alias = dict(index["artifacts"][0])
        alias["path"] = "./" + alias["path"]
        index["artifacts"].append(alias)
        write_json(package / "artifacts.json", index)
        completion = load_json(package / "completion.json")
        completion["artifacts_sha256"] = hashlib.sha256((package / "artifacts.json").read_bytes()).hexdigest()
        write_json(package / "completion.json", completion)
        with self.assertRaises(InvalidEvidence):
            adapter.check(package)

    def test_unknown_protocol_is_not_treated_as_historical(self):
        adapter, package = self.copy_package("04", "unknown-protocol")
        metadata = load_json(package / "run.json")
        metadata["package_protocol"] = "step-evidence.v999"
        write_json(package / "run.json", metadata)
        self.reseal(package)
        with self.assertRaises(InvalidEvidence):
            adapter.check(package)

    def test_cli_rechecks_the_same_package_without_native_execution(self):
        for ticket, adapter, _ in self.stages:
            _, package = self.copy_package(ticket, "cli")
            module = {"02": "step_inspect_validation", "03": "step_semantic_validation",
                      "04": "step_planar_validation"}[ticket]
            actual = subprocess.run([sys.executable, "-m", "tools."+module, "check-evidence", "--run", str(package)],
                                    capture_output=True, timeout=15)
            self.assertEqual(0, actual.returncode, actual.stderr.decode(errors="replace"))
            self.assertEqual(adapter.check(package), json.loads(actual.stdout))

    def test_historical_packages_keep_their_qualifications_and_bytes(self):
        configured = os.environ.get("STEP_LEGACY_EVIDENCE")
        if not configured:
            self.skipTest("Set STEP_LEGACY_EVIDENCE to the three retained historical evidence directories")
        paths = json.loads(configured)
        for ticket, adapter, _ in self.stages:
            package = Path(paths[ticket]).resolve(strict=True)
            before = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
                      for p in package.rglob("*") if p.is_file()}
            metadata = load_json(package / "run.json")
            self.assertNotIn("package_protocol", metadata, "This control requires the actual old wire format")
            self.assertEqual(49, adapter.check(package)["selected"])
            self.assertEqual("NotRun", adapter.check(package)["full_import_execution"])
            after = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in package.rglob("*") if p.is_file()}
            self.assertEqual(before, after)

    def test_tool_change_during_real_execution_prevents_publication(self):
        repo = Path(__file__).resolve().parents[3]
        clone = self.root / "changing-source"
        for name in ("step_import_validation", "step_inspect_validation", "step_evidence"):
            shutil.copytree(repo / "tools" / name, clone / "tools" / name,
                            ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copytree(repo / "source/STEP_SOURCE", clone / "source/STEP_SOURCE")
        output = self.root / "changing-source-output"
        env = os.environ.copy()
        env["PYTHONPATH"] = str(clone)
        child = subprocess.Popen([sys.executable, "-m", "tools.step_inspect_validation", "run", "--binary",
                                  str(self.stages[0][2]), "--corpus", str(CORPUS), "--output-root", str(output)],
                                 cwd=clone, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        changed = False
        try:
            deadline = time.monotonic()+30
            while child.poll() is None and time.monotonic() < deadline:
                if list(output.glob("staging/*.partial/cases/*/invocation.json")):
                    target = clone / "tools/step_evidence/presentation.py"
                    target.write_bytes(target.read_bytes()+b"\n# source drift during an actual native run\n")
                    changed = True
                    break
                time.sleep(0.005)
            stdout, stderr = child.communicate(timeout=30)
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()
        self.assertTrue(changed, "The real execution did not reach the injection point")
        self.assertEqual(1, child.returncode, stderr.decode(errors="replace"))
        self.assertTrue("reader/source changed" in json.loads(stdout)["error"])
        self.assertFalse((output / "runs").exists())
        self.assertTrue(list(output.glob("staging/*.partial")))


if __name__ == "__main__":
    unittest.main()
