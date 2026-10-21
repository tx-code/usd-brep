# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Local, atomic observations of one viewer request; no geometric qualification."""
import json
from pathlib import Path
import time
import uuid


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        pending.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
        pending.replace(path)
    finally:
        pending.unlink(missing_ok=True)


class RequestReceipt:
    def __init__(self, filename):
        self.started = time.perf_counter()
        self.filename = str(Path(filename).resolve())
        self.observations = []
        self.result = None

    def progress(self, update):
        if self.result is not None:
            return
        if not self.observations or self.observations[-1]["phase"] != update.phase:
            self.observations.append({"phase": update.phase, "seconds": time.perf_counter() - self.started})

    def finish(self, outcome, worker_result=None, details=""):
        # Completion is a single decision. A later close event cannot overwrite
        # a completed render or turn a failed request into a successful one.
        if self.result is not None:
            return self.result
        scene = worker_result or {}
        phases = {entry["phase"] for entry in self.observations}
        is_cad = Path(self.filename).suffix.lower() in {".step", ".stp"}
        ready = scene.get("status") == "Ready"
        native_status = scene.get("status") if is_cad else None
        explicit_status = scene.get("native_import_status") if is_cad else None
        import_complete = is_cad and (ready or "copy" in phases or
                                     explicit_status in ("Accepted", "AcceptedUnderInterpretation"))
        if native_status in (None, "Ready", "DisplayFailure"):
            # Ready establishes this viewer's import stage completed. It does
            # not expose the core's exact acceptance/interpretation status, so
            # do not invent an unqualified native Accepted status from it.
            native_status = None
        if explicit_status:
            native_status = explicit_status
        self.result = {
            "format": "brep-view-result.v1", "input": self.filename, "outcome": outcome,
            "qualification": "ViewerPipelineOnly", "source_sha256": scene.get("source_sha256"),
            "native_import_status": native_status,
            # Null means unestablished/not applicable. Entering a stage is not
            # proof it completed: only a Ready manifest establishes mesh output.
            "stages": {"import": True if import_complete else False if native_status else None,
                       "mesh": ready, "render": outcome == "Rendered"},
            "worker": scene, "details": details[:4096],
            "phase_observations": self.observations,
            "elapsed_seconds": time.perf_counter() - self.started,
        }
        return self.result
