# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Sequential directory scan through isolated instances of the actual viewer."""
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import queue
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

from .process_guard import ProcessGuard, resume_suspended_process
from .progress import LABELS, ProgressReader
from .receipt import write_json

EXTENSIONS = frozenset({".step", ".stp", ".stl", ".obj", ".ply", ".vtp", ".vtk"})
INPUT_LIMIT = 64 * 1024 * 1024


def discover(roots):
    """Walk deterministically without following directory links or hiding I/O errors."""
    paths = set()
    for root in roots:
        if not root.is_dir():
            raise ValueError("ScanRootNotDirectory")
        def failed(error):
            raise error
        for directory, subdirs, files in os.walk(root, followlinks=False, onerror=failed):
            # Windows junctions are reparse directories, not ordinary symlinks;
            # followlinks=False alone does not prevent loops through junctions.
            subdirs[:] = sorted(
                (name for name in subdirs if not (Path(directory) / name).is_symlink()
                 and not getattr((Path(directory) / name).lstat(), "st_file_attributes", 0) & 0x400),
                key=lambda name: (name.casefold(), name))
            for name in sorted(files, key=lambda name: (name.casefold(), name)):
                path = Path(directory) / name
                if path.suffix.lower() in EXTENSIONS:
                    paths.add(path.resolve())
    return sorted(paths, key=lambda path: (str(path).casefold(), str(path)))


def source_hash(path):
    digest = hashlib.sha256()
    count = 0
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            count += len(data)
            if count > INPUT_LIMIT:
                raise ValueError("InputByteLimit")
            digest.update(data)
    return digest.hexdigest()


def viewer_command():
    if getattr(sys, "frozen", False):
        return [sys.executable]
    return [sys.executable, str(Path(__file__).resolve().parents[1] / "scripts/brep_view.py")]


def _read_progress(stream, updates):
    reader = ProgressReader()
    try:
        while data := stream.read1(4096):
            for update in reader.feed(data):
                try:
                    updates.put_nowait(update.phase)
                except queue.Full:
                    pass  # Captions may be coalesced; never block the worker pipe.
    finally:
        stream.close()


def run_case(path, directory, options, caption):
    result_file = directory / "result.json"
    command = [*viewer_command(), str(path), "--quit-after-load", "--result", str(result_file),
               "--scan-child", "--profile", options.profile]
    command.extend(["--schema-interpretation", options.schema_interpretation,
                    "--carrier-interpretation", options.carrier_interpretation,
                    "--unit-interpretation", options.unit_interpretation,
                    "--report-budget-mib", str(options.report_budget_mib)])
    mapping = getattr(options, 'complex_mapping_interpretation', 'DeclaredOnly')
    if mapping != 'DeclaredOnly':
        command.extend(['--complex-mapping-interpretation', mapping])
    if path.suffix.lower() in {'.step', '.stp'}:
        from .integration import arguments as integration_arguments
        command.extend(integration_arguments(options))
    for enabled, flag in [(options.normalize_periodic_faces, "--normalize-periodic-faces"),
                          (options.restrict_unused_support_domains, "--restrict-unused-support-domains"),
                          (options.strict_source, "--strict-source"), (options.edges, "--edges"),
                          (options.preserve_opaque_file_name_text, "--preserve-opaque-file-name-text"),
                          (options.assume_ascii_invariant_data, "--assume-ascii-invariant-data"),
                          (options.wireframe, "--wireframe")]:
        if enabled:
            command.append(flag)
    if options.thumbnails:
        command.extend(["--screenshot", str(directory / "thumbnail.png")])
    updates = queue.Queue(maxsize=64)
    scratch = Path(tempfile.mkdtemp(prefix=".work-", dir=directory))
    environment = dict(os.environ, TEMP=str(scratch), TMP=str(scratch), TMPDIR=str(scratch))
    process = guard = reader = None
    started = time.perf_counter()
    outcome, last_phase = None, None
    try:
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   env=environment,
                                   start_new_session=os.name != "nt",
                                   creationflags=0x4 if os.name == "nt" else 0)  # Win32 CREATE_SUSPENDED
        # The child waits before importing Qt/VTK or opening a model. Assign the
        # whole renderer/worker tree first. The native worker retains its own
        # 2 GiB job; the enclosing GUI tree is capped at 4 GiB on Windows.
        guard = ProcessGuard(process.pid, maximum_bytes=4 * 1024**3)
        # Venv launchers and the frozen bootloader can spawn their interpreter
        # before Python reaches the stdin handshake. Suspension closes that
        # race: every descendant is born inside the enclosing Windows job.
        resume_suspended_process(process.pid)
        reader = threading.Thread(target=_read_progress, args=(process.stdout, updates), daemon=True)
        reader.start()
        process.stdin.write(b"start\n")
        process.stdin.close()
        while process.poll() is None:
            if time.perf_counter() - started >= options.timeout:
                outcome = "Timeout"
                break
            try:
                phase = updates.get(timeout=0.1)
                if phase != last_phase:
                    caption(LABELS[phase])
                    last_phase = phase
            except queue.Empty:
                pass
    except KeyboardInterrupt:
        outcome = "Cancelled"
    except OSError:
        outcome = "ProcessFailure"
    finally:
        if guard:
            guard.close()
        if process:
            if os.name != "nt":
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            if process.poll() is None:
                process.kill()
            process.wait()
            if process.stdin and not process.stdin.closed:
                process.stdin.close()
            if reader:
                reader.join(timeout=2)
            elif process.stdout:
                process.stdout.close()
        # The GUI can be hard-killed before its TemporaryDirectory destructor.
        # All descendants inherit this parent-owned scratch root. Never derive
        # cleanup targets from a source/worker manifest. Retry briefly while
        # Windows finishes terminating descendants and releasing file handles.
        scratch_cleaned = False
        if scratch.resolve().is_relative_to(directory.resolve()):
            for attempt in range(20):
                try:
                    shutil.rmtree(scratch)
                    scratch_cleaned = True
                    break
                except OSError:
                    if attempt < 19:
                        time.sleep(0.05)
        if not scratch_cleaned and outcome not in {"Cancelled", "Timeout"}:
            outcome = "CleanupFailure"
    result = {"outcome": outcome or "WorkerFailure", "last_phase_observed": last_phase}
    if outcome is None:
        try:
            if result_file.stat().st_size > 1024 * 1024:
                raise ValueError("ResultByteLimit")
            observed = json.loads(result_file.read_text(encoding="utf-8"))
            if (not isinstance(observed, dict) or observed.get("format") != "brep-view-result.v1"
                    or observed.get("input") != str(path)
                    or observed.get("outcome") not in {
                        "Rendered", "LoadFailure", "RenderFailure", "Closed", "Cancelled"}
                    or (observed["outcome"] == "Rendered") != (process.returncode == 0)):
                raise ValueError("InvalidViewerResult")
            result.update(observed)
        except (OSError, ValueError, KeyError, TypeError):
            result["outcome"] = "WorkerFailure"
    result.update(process_exit_code=process.returncode if process else None,
                  process_seconds=time.perf_counter() - started, temporary_payloads_cleaned=scratch_cleaned)
    return result


def run_scan(options):
    roots = list(dict.fromkeys(path.expanduser().resolve() for path in options.scan))
    output = options.output.expanduser().resolve()
    if not math.isfinite(options.timeout) or options.timeout <= 0:
        raise ValueError("ScanTimeoutMustBeFinitePositive")
    if output.exists() or any(output.is_relative_to(root) for root in roots):
        raise ValueError("ScanOutputMustBeNewAndOutsideInputs")
    paths = discover(roots)
    if not paths:
        raise ValueError("ScanSelectionEmpty")
    output.mkdir(parents=True, exist_ok=False)
    records, seen = [], {}
    counts = Counter()
    started = time.perf_counter()
    cancelled = False
    for index, path in enumerate(paths, 1):
        record = {"id": f"case-{index:06d}", "input": str(path)}
        key = None
        caption = lambda text: print(f"[{index}/{len(paths)}] {text}", flush=True)
        try:
            caption("读取内容身份")
            identity = source_hash(path)
            record["source_sha256"] = identity
            # STEP/.stp share a reader. Equal bytes in different mesh formats
            # do not share interpretation, so their reader suffix is part of
            # the key. No filename-based deduplication or geometry comparison.
            reader_kind = "step" if path.suffix.lower() in {".step", ".stp"} else path.suffix.lower()
            key = reader_kind, identity
            if key in seen:
                record.update(outcome="Duplicate", duplicate_of=seen[key])
            else:
                seen[key] = record["id"]
                directory = output / record["id"]
                directory.mkdir()
                observed = run_case(path, directory, options, caption)
                record.update(observed)
                # Hash before AND after, and bind any worker-captured identity.
                # An edited source never counts as a successful comparison to
                # the discovered input, even if the isolated viewer rendered.
                observed_hash = observed.get("source_sha256")
                record["source_sha256"] = identity
                record["original_unchanged"] = source_hash(path) == identity
                if (record["outcome"] != "Cancelled" and
                        (not record["original_unchanged"] or (observed_hash and observed_hash != identity))):
                    record["outcome"] = "SourceChanged"
                if record["outcome"] == "Rendered" and observed_hash != identity:
                    record["outcome"] = "UnboundSourceIdentity"
                if record["outcome"] in {"SourceChanged", "UnboundSourceIdentity"}:
                    seen.pop(key, None)
        except KeyboardInterrupt:
            record["outcome"] = "Cancelled"
        except (OSError, ValueError) as error:
            if key is not None:
                seen.pop(key, None)
            record.update(outcome="InputFailure", details=str(error)[:4096])
        records.append(record)
        counts.update([record["outcome"]])
        # Append each observation once. Checkpoint only small counters while
        # running, rather than rewriting every preceding model's diagnostics
        # after each file (quadratic report I/O). A crash can leave a partial
        # last journal row; the atomic checkpoint's processed count is the
        # committed prefix. The complete/incomplete final summary embeds it once.
        with (output / "records.jsonl").open("a", encoding="utf-8") as journal:
            journal.write(json.dumps(record, ensure_ascii=False, allow_nan=False) + "\n")
        write_json(output / "scan.json", _summary(
            roots, records, len(paths), started, options, False, counts, include_records=False))
        caption(record["outcome"])
        if record["outcome"] == "Cancelled":
            cancelled = True
            break
    report = _summary(roots, records, len(paths), started, options, not cancelled, counts)
    write_json(output / "scan.json", report)
    print(json.dumps(report["summary"], ensure_ascii=False), flush=True)
    return 130 if cancelled else 1 if report["summary"]["failures"] else 0


def _summary(roots, records, count, started, options, complete, counts, *, include_records=True):
    result = {
        "format": "brep-view-scan.v1", "complete": complete, "qualification": "ViewerPipelineOnly",
        "roots": list(map(str, roots)),
        "settings": {"profile": options.profile, "strict_source": options.strict_source,
                     "schema_interpretation": options.schema_interpretation,
                     "carrier_interpretation": options.carrier_interpretation,
                     "unit_interpretation": options.unit_interpretation,
                     "complex_mapping_interpretation": getattr(options, 'complex_mapping_interpretation', 'DeclaredOnly'),
                     "report_budget_mib": options.report_budget_mib,
                     "preserve_opaque_file_name_text": options.preserve_opaque_file_name_text,
                     "assume_ascii_invariant_data": options.assume_ascii_invariant_data,
                     "normalize_periodic_faces": options.normalize_periodic_faces,
                     "restrict_unused_support_domains": options.restrict_unused_support_domains,
                     "edges": options.edges, "wireframe": options.wireframe, "thumbnails": options.thumbnails,
                     "file_timeout_seconds": options.timeout},
        "summary": {"files": count, "processed": len(records), "rendered": counts["Rendered"],
                    "duplicates": counts["Duplicate"],
                    "failures": len(records) - counts["Rendered"] - counts["Duplicate"],
                    "not_run": count - len(records)},
        "outcomes": dict(counts), "elapsed_seconds": time.perf_counter() - started,
        "journal": "records.jsonl",
    }
    if include_records:
        result["records"] = records
    return result
