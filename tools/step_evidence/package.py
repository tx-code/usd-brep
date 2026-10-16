# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""One package lifecycle; stage adapters retain independent comparison and qualification knowledge."""
from __future__ import annotations

import base64
from collections import Counter
from dataclasses import dataclass
from datetime import datetime
import html
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import subprocess
import sys
from typing import Callable

from tools.step_import_validation.common import (
    InvalidEvidence, checked_bytes, digest, file_ref, inside, json_bytes, load_json, parse_json, write_json,
)
from tools.step_import_validation.corpus import verify
from tools.step_import_validation.evidence import identifier, now
from tools.step_import_validation.oracle import reference_truth, verify_input_definition
from tools.step_import_validation.records import inventory, ledger
from tools.step_import_validation.reference import anchors
from .dependencies import PROVENANCE, VERSIONS, required_sources
from .presentation import case_body, page

PROTOCOL = "step-evidence.v2"
CASE_ID = re.compile(r"S203-(?:P(?:0[1-9]|1[0-3])|N(?:0[1-9]|[12][0-9]|3[0-6]))\Z")
RUN_ID = re.compile(r"[0-9]{8}T[0-9]{6}Z-[0-9a-f]{12}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def require(condition, detail):
    if not condition:
        raise InvalidEvidence("Evidence.PackageConflict", detail)


@dataclass(frozen=True)
class Witness:
    path: Path
    sha256: str


@dataclass(frozen=True)
class Stage:
    ticket: str
    scope: str
    status_kind: str
    description: str
    components: tuple[str, ...]
    dependencies: tuple[str, ...]
    compare: Callable
    witness: Witness | None = None
    truth: bool = False
    native_options: tuple[str, ...] = ("--fixture-limits",)

    @property
    def format(self):
        return f"t{self.ticket}.evidence.v1"

    @property
    def observation_file(self):
        return "inspect.json" if self.status_kind == "source" else "observed.json"

    @property
    def contract(self):
        return "source_contract" if self.status_kind == "source" else "scope_contract"

    @property
    def matched(self):
        return self.contract + "_matched"

    @property
    def status_field(self):
        return self.status_kind + "_status"

    @property
    def counts_field(self):
        return self.status_kind + "_statuses"

class Runner:
    """Shared execution and comparison replay; no product-reading implementation lives here."""

    def __init__(self, stage: Stage):
        self.stage = stage

    def page(self, title, body):
        return page(title, body, self.stage.description)

    def source_identity(self):
        repo = Path(__file__).resolve().parents[2]
        paths = set(Path(__file__).parent.rglob("*.py"))
        for component in self.stage.components:
            folder = repo / "source" / component
            require(folder.is_dir(), "native source directory missing during identity capture")
            paths.update(p for p in folder.rglob("*")
                         if p.suffix in {".cpp", ".h", ".lua"} or p.name == "CMakeLists.txt")
        for dependency in self.stage.dependencies:
            folder = repo / "tools" / dependency
            require(folder.is_dir(), "tool dependency directory missing during identity capture")
            paths.update(folder.rglob("*.py"))
            paths.update((folder / "contracts").glob("*.json"))
        return {p.relative_to(repo).as_posix(): digest(p.read_bytes()) for p in sorted(paths)}

    def _witness(self, root=None):
        reference = self.stage.witness
        if reference is None:
            return None
        path = reference.path if root is None else root / "reference.json"
        require(digest(path.read_bytes()) == reference.sha256, "independent witness identity")
        return load_json(path)

    def _compare(self, case, observed, exit_code, data, inventory, witness):
        args = (case, observed, exit_code, data, inventory)
        return self.stage.compare(*args) if witness is None else self.stage.compare(*args, witness)

    def _row(self, case_id, observed, comparison):
        row = {"case_id": case_id, self.stage.status_field: observed["status"], self.stage.contract: "Matched",
               "full_import_execution": "NotRun", "full_import_contract": "NotChecked"}
        if self.stage.witness:
            row["first_blocking_key"] = comparison.get("first_blocking_key")
        return row

    def _required(self, selected):
        names = {"input.step", "input.receipt.json", "preparation.json", "oracle.json", "expected.json",
                 "inventory.json", self.stage.observation_file, "invocation.json", "comparison.json", "case.html"}
        if self.stage.truth:
            names.add("truth.json")
        required = {"run.json", "summary.json", "summary.html"}
        if self.stage.witness:
            required.add("reference.json")
        return required | {f"cases/{key}/{name}" for key in selected for name in names}

    def run(self, binary: Path, corpus: Path, output_root: Path, selected=None):
        materials = verify(corpus, selected=selected)
        witness = self._witness()
        binary = binary.resolve(strict=True)
        binary_hash, sources = digest(binary.read_bytes()), self.source_identity()
        root, run_id = output_root.resolve(), identifier()
        stage = inside(root, f"staging/{run_id}.partial")
        destination = inside(root, f"runs/{run_id}")
        stage.mkdir(parents=True, exist_ok=False)
        if self.stage.witness:
            (stage / "reference.json").write_bytes(self.stage.witness.path.read_bytes())
        metadata = {"format_version": self.stage.format, "package_protocol": PROTOCOL,
                    "source_provenance": PROVENANCE, "execution_directory": str(stage),
                    "run_id": run_id, "started_utc": now(), "finished_utc": None,
                    "selected": materials["selected"], "manifest_sha256": materials["manifest_sha256"],
                    "reference_sha256": self.stage.witness.sha256 if self.stage.witness else
                    materials["trusted_reference_sha256"], "reader": {"path": str(binary), "sha256": binary_hash},
                    "python": sys.version, "sources": sources, "scope": self.stage.scope,
                    "full_import_execution": "NotRun", "native_build": "NotRun", "repair": "None"}
        write_json(stage / "run.json", metadata)
        rows = []
        for case in materials["cases"]:
            key = case["case_id"]
            prefix = stage / "cases" / key
            prefix.mkdir(parents=True)
            data = base64.b64decode(case["input_snapshot_b64"], validate=True)
            (prefix / "input.step").write_bytes(data)
            write_json(prefix / "input.receipt.json", {"case_id": key, "sha256": digest(data), "byte_length": len(data),
                                                      "identity": case["identity"]})
            for name, content in (("preparation", case["preparation"]), ("oracle", case["oracle"]),
                                  ("expected", case["expected"])):
                write_json(prefix / (name + ".json"), content)
            inventory_bytes = checked_bytes(corpus, case["identity"]["materials"]["inventory"])
            (prefix / "inventory.json").write_bytes(inventory_bytes)
            if self.stage.truth:
                (prefix / "truth.json").write_bytes(checked_bytes(corpus, case["identity"]["materials"]["truth"]))
                case["truth"] = load_json(prefix / "truth.json")
            command = [str(binary), *self.stage.native_options, str(prefix / "input.step")]
            actual = subprocess.run(command, capture_output=True, timeout=30, check=False)
            (prefix / self.stage.observation_file).write_bytes(actual.stdout)
            write_json(prefix / "invocation.json", {"command": command, "exit_code": actual.returncode,
                                                   "stderr": actual.stderr.decode("utf-8", errors="replace"),
                                                   "reader_sha256": binary_hash, "execution": "Completed"})
            observed = parse_json(actual.stdout)
            comparison = self._compare(case, observed, actual.returncode, data, parse_json(inventory_bytes), witness)
            write_json(prefix / "comparison.json", comparison)
            (prefix / "case.html").write_bytes(self.page(key, case_body(self.stage.ticket, self.stage.observation_file,
                                                                       case["expected"], observed)))
            rows.append(self._row(key, observed, comparison))
        summary = {"format_version": self.stage.format, "cases": rows, self.stage.matched: len(rows),
                   self.stage.counts_field: dict(Counter(row[self.stage.status_field] for row in rows)),
                   "full_import_not_run": len(rows), "import_success_count": None}
        write_json(stage / "summary.json", summary)
        body = (f"<p>{self.stage.ticket}当前范围合同匹配 {len(rows)} 项；完整导入未运行。</p>"
                "<pre>" + html.escape(json.dumps(summary[self.stage.counts_field], indent=2)) + "</pre>"
                "<table><tr><th>样件</th><th>当前状态</th><th>当前合同</th><th>完整导入</th></tr>")
        for row in rows:
            key = row["case_id"]
            body += (f"<tr><td><a href='cases/{key}/case.html'>{key}</a></td>"
                     f"<td>{html.escape(row[self.stage.status_field])}</td><td>Matched</td><td>NotRun</td></tr>")
        (stage / "summary.html").write_bytes(self.page(f"STEP {self.stage.ticket}：{self.stage.scope}", body+"</table>"))
        metadata["finished_utc"] = now()
        write_json(stage / "run.json", metadata)
        self._seal(stage, run_id)
        self.check(stage, staged=True)
        require(binary_hash == digest(binary.read_bytes()) and sources == self.source_identity(),
                "reader/source changed during execution")
        destination.parent.mkdir(parents=True, exist_ok=True)
        require(not destination.exists(), "existing run must not be overwritten")
        require(stage.resolve().is_relative_to(root) and destination.resolve().is_relative_to(root), "publication path")
        stage.rename(destination)
        self.check(destination)
        return destination

    def _seal(self, root, run_id):
        artifacts = [file_ref(root, p.relative_to(root).as_posix(), "evidence")
                     for p in sorted(root.rglob("*")) if p.is_file()]
        write_json(root / "artifacts.json", {"format_version": self.stage.format, "artifacts": artifacts})
        write_json(root / "completion.json", {"format_version": self.stage.format, "run_id": run_id,
                                              "artifacts_sha256": digest((root / "artifacts.json").read_bytes())})

    @staticmethod
    def _time(value):
        require(isinstance(value, str), "incomplete execution timestamp")
        try:
            result = datetime.fromisoformat(value.replace("Z", "+00:00"))
        except ValueError as error:
            raise InvalidEvidence("Evidence.PackageConflict", "invalid execution timestamp") from error
        require(result.utcoffset() is not None, "execution timestamp lacks timezone")
        return result

    @staticmethod
    def _captured_path(value):
        # A captured path is historical data, possibly from another OS, not a path to resolve/open now.
        require(isinstance(value, str) and value and
                not {".", ".."}.intersection(value.replace("\\", "/").split("/")), "captured path")
        result = PureWindowsPath(value) if "\\" in value or PureWindowsPath(value).drive else PurePosixPath(value)
        require(result.is_absolute(), "captured execution path must be absolute")
        return result

    def _execution(self, metadata, invocation, key):
        command = invocation["command"]
        require(isinstance(command, list) and all(isinstance(item, str) for item in command) and
                len(command) == len(self.stage.native_options)+2, "captured command shape")
        require(command[0] == metadata["reader"]["path"] and
                command[1:-1] == list(self.stage.native_options), "captured executable/options mismatch")
        captured = self._captured_path(command[-1])
        require(captured.parts[-5:] == ("staging", metadata["run_id"]+".partial", "cases", key, "input.step"),
                "captured input/run identity")
        if "package_protocol" in metadata:
            execution = self._captured_path(metadata["execution_directory"])
            require(execution.parts[-2:] == ("staging", metadata["run_id"]+".partial") and
                    captured == execution / "cases" / key / "input.step", "captured execution directory")

    def _metadata(self, metadata):
        require(self._time(metadata["started_utc"]) <= self._time(metadata["finished_utc"]), "execution time order")
        require(metadata.get("repair", "None") == "None", "repair scope contradiction")
        sources = metadata["sources"]
        require(isinstance(sources, dict) and sources and
                all(isinstance(k, str) and isinstance(v, str) and SHA256.fullmatch(v) for k, v in sources.items()),
                "recorded source identities")
        require(isinstance(metadata["reader"]["sha256"], str) and SHA256.fullmatch(metadata["reader"]["sha256"]),
                "recorded binary identity")
        if "package_protocol" in metadata:
            require(metadata["package_protocol"] == PROTOCOL and
                    metadata["source_provenance"] in VERSIONS, "unknown package protocol")
            require(required_sources(self.stage.components, self.stage.dependencies,
                                     metadata["source_provenance"]) <= set(sources),
                    "missing execution dependency identity")

    @staticmethod
    def _payload(root, references):
        paths, targets = [], set()
        for reference in references:
            name = reference["path"]
            require(isinstance(name, str) and name and "\\" not in name and ":" not in name and
                    not {"", ".", ".."}.intersection(name.split("/")) and
                    not PurePosixPath(name).is_absolute(), "noncanonical artifact path")
            target = inside(root, name)
            require(target not in targets and target == root / name, "aliased artifact identity")
            require(reference["role"] == "evidence", "artifact role")
            targets.add(target)
            paths.append(name)
        payload = set()
        for path in root.rglob("*"):
            require(not path.is_symlink() and path.resolve() == path, "aliased payload path")
            if path.is_file():
                payload.add(path.relative_to(root).as_posix())
        require(payload - {"artifacts.json", "completion.json"} == set(paths),
                "artifact index does not describe the complete payload")
        return set(paths)

    def check(self, root: Path, staged=False):
        root = root.resolve(strict=True)
        metadata, completion = load_json(root / "run.json"), load_json(root / "completion.json")
        self._metadata(metadata)
        require(metadata["format_version"] == completion["format_version"] == self.stage.format, "evidence format")
        require(isinstance(metadata["run_id"], str) and RUN_ID.fullmatch(metadata["run_id"]), "run identity")
        require(root.parent.name == ("staging" if staged else "runs") and
                root.name == metadata["run_id"] + (".partial" if staged else ""), "uncommitted evidence location")
        require(completion["run_id"] == metadata["run_id"] and completion["artifacts_sha256"] ==
                digest((root / "artifacts.json").read_bytes()), "completion binding")
        require(metadata["scope"] == self.stage.scope and
                metadata["full_import_execution"] == metadata["native_build"] == "NotRun", "run scope")
        selected = metadata["selected"]
        require(isinstance(selected, list) and selected and
                all(isinstance(key, str) and CASE_ID.fullmatch(key) for key in selected) and
                len(selected) == len(set(selected)), "case selection")
        index = load_json(root / "artifacts.json")
        require(index["format_version"] == self.stage.format, "artifact index format")
        refs = index["artifacts"]
        paths = self._payload(root, refs)
        require(self._required(selected) <= paths, "required artifact missing")
        if "package_protocol" in metadata:
            require(self._required(selected) == paths, "undeclared payload in scoped package")
        for reference in refs:
            checked_bytes(root, reference, 20_000_000)
        witness = self._witness(root)
        if self.stage.witness:
            require(metadata["reference_sha256"] == self.stage.witness.sha256, "run witness identity")
        summary = load_json(root / "summary.json")
        require(summary["format_version"] == self.stage.format and
                [row["case_id"] for row in summary["cases"]] == selected, "selected case coverage")
        require(summary[self.stage.matched] == summary["full_import_not_run"] == len(selected) and
                summary["import_success_count"] is None, "scope counters")
        require(summary[self.stage.counts_field] == dict(Counter(row[self.stage.status_field]
                                                               for row in summary["cases"])), "status counters")
        reviewed = anchors()
        for row in summary["cases"]:
            prefix = root / "cases" / row["case_id"]
            require(load_json(prefix / "preparation.json")["status"] == "Passed", "material preparation failed")
            data, receipt = (prefix / "input.step").read_bytes(), load_json(prefix / "input.receipt.json")
            require(receipt["case_id"] == row["case_id"] and receipt["byte_length"] == len(data) and
                    receipt["sha256"] == digest(data), "input receipt")
            frozen_input = receipt["identity"]["materials"]["input"]
            require(frozen_input["sha256"] == digest(data) and frozen_input["byte_length"] == len(data),
                    "captured input disagrees with frozen input identity")
            oracle = load_json(prefix / "oracle.json")
            require(oracle["status"] == "Passed" and oracle["truth_sha256"] ==
                    receipt["identity"]["materials"]["truth"]["sha256"] and
                    digest(json_bytes(oracle["truth"])) == oracle["truth_sha256"], "independent oracle contradiction")
            retained_inventory = load_json(prefix / "inventory.json")
            records = inventory(data)
            actual_inventory = {"format_version": "t01.v1", "data_record_occurrences": len(records),
                                "unique_entity_ids": len({record.entity_id for record in records}),
                                "records": ledger(records)}
            require(retained_inventory == actual_inventory, "retained inventory disagrees with input")
            preparation = load_json(prefix / "preparation.json")
            require(all(preparation[name] == actual_inventory[name]
                        for name in ("data_record_occurrences", "unique_entity_ids")),
                    "preparation count contradiction")
            anchor = reviewed[row["case_id"]]
            require(oracle["input_proof"] == verify_input_definition(row["case_id"], data, anchor) and
                    oracle["truth"] == reference_truth(row["case_id"], anchor), "independent input proof contradiction")
            for role, name in [("assertions", "expected.json"), ("inventory", "inventory.json")] + (
                    [("truth", "truth.json")] if self.stage.truth else []):
                require(digest((prefix / name).read_bytes()) == receipt["identity"]["materials"][role]["sha256"],
                        "frozen material identity")
            invocation = load_json(prefix / "invocation.json")
            require(invocation["execution"] == "Completed" and invocation["reader_sha256"] ==
                    metadata["reader"]["sha256"], "reader identity")
            self._execution(metadata, invocation, row["case_id"])
            case = {"case_id": row["case_id"], "identity": receipt["identity"],
                    "expected": load_json(prefix / "expected.json")}
            if self.stage.truth:
                case["truth"] = load_json(prefix / "truth.json")
            observed = load_json(prefix / self.stage.observation_file)
            comparison = self._compare(case, observed, invocation["exit_code"], data,
                                       load_json(prefix / "inventory.json"), witness)
            require(comparison == load_json(prefix / "comparison.json"), "comparison/evidence state")
            require(row == self._row(row["case_id"], observed, comparison), "case status / full-import overclaim")
        return {"evidence_completeness": "Complete", "selected": len(selected), self.stage.matched: len(selected),
                "full_import_execution": "NotRun"}
