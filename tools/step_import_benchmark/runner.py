# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Sequential, isolated probes. Failures and scope stops never become import timings."""
from collections import Counter
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time

from .profile import write_json


def digest(path):
    value = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            value.update(block)
    return value.hexdigest()


class ProcessMemory:
    """Windows sampled supervision, not a birth-time or hard allocation limit."""
    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                    *((name, ctypes.c_size_t) for name in ("PeakWorkingSetSize", "WorkingSetSize",
                                                          "QuotaPeakPagedPoolUsage",
                                                          "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
                                                          "QuotaNonPagedPoolUsage", "PagefileUsage",
                                                          "PeakPagefileUsage",
                                                          "PrivateUsage"))]

    def __init__(self, process):
        self.process = process
        self.function = None
        if os.name == "nt":
            self.function = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
            self.function.argtypes = [wintypes.HANDLE, ctypes.POINTER(self.Counters), wintypes.DWORD]
            self.function.restype = wintypes.BOOL

    def sample(self):
        if not self.function:
            return None
        value = self.Counters()
        value.cb = ctypes.sizeof(value)
        # Popen owns a live Windows process handle until wait/close; no extra handle is created.
        if self.function(wintypes.HANDLE(int(self.process._handle)), ctypes.byref(value), value.cb):
            return dict(private_bytes=value.PrivateUsage, lifetime_peak_commit_bytes=value.PeakPagefileUsage,
                        lifetime_peak_working_set_bytes=value.PeakWorkingSetSize)
        if self.process.poll() is None:
            raise OSError(ctypes.get_last_error(), "Cannot measure worker memory")
        return None


def execute(binary, mode, path, identity, directory, *, repeat=1, warmup=0, maximum_input=67108864,
            timeout=90, maximum_private=2147483648, env=None):
    directory.mkdir(parents=True, exist_ok=False)
    output = directory/"probe.json"
    before = digest(path)
    if before != identity:
        raise ValueError("Input no longer matches census SHA256")
    command = [str(binary), "--mode", mode, "--input", str(path), "--output", str(output), "--repeat", str(repeat),
               "--warmup", str(warmup), "--max-input-bytes", str(maximum_input)]
    observation = dict(format="step-probe-observation.v1", command=command, input_sha256=identity, result=None,
                       supervisor=dict(timeout_seconds=timeout, maximum_private_bytes=maximum_private,
                                       memory_policy="20ms sampled private-byte kill; overshoot possible, not a hard cap",
                                       filesystem_cache="SHA256 before launch; warm input, not cold IO"))
    process = None
    samples = []
    start = time.perf_counter()
    try:
        with (directory/"stdout.log").open("wb") as stdout, (directory/"stderr.log").open("wb") as stderr:
            process = subprocess.Popen(command, stdout=stdout, stderr=stderr, env=env)
            monitor = ProcessMemory(process)
            stop = None
            while process.poll() is None:
                value = monitor.sample()
                if value:
                    samples.append(value)
                if time.perf_counter()-start > timeout:
                    stop = "Timeout"
                elif value and value["private_bytes"] > maximum_private:
                    stop = "MemoryBudgetStop"
                elif max(stdout.tell(), stderr.tell()) > 16*1024*1024:
                    stop = "LogBudgetStop"
                if stop:
                    process.kill()
                    break
                time.sleep(0.02)
            code = process.wait()
            observation.update(process_wall_ms=(time.perf_counter()-start)*1000, exit=code,
                               status=stop or ("Completed" if code == 0 else "ProbeFailure"))
        if samples:
            observation["supervisor"]["sampled_peak_private_bytes"] = max(s["private_bytes"] for s in samples)
            observation["supervisor"]["observed_lifetime_peak_commit_bytes"] = max(
                s["lifetime_peak_commit_bytes"] for s in samples)
        if observation["status"] == "Completed":
            result = json.loads(output.read_bytes())
            if (result["format"], result["mode"], result["build_mode"], result["input_bytes"]) != (
                    "step-import-benchmark.v1", mode, "Release", path.stat().st_size):
                raise ValueError("Probe identity, mode or build mismatch")
            trials = result["trials"]
            if len(trials) != repeat+warmup or [t["warmup"] for t in trials] != [True]*warmup+[False]*repeat:
                raise ValueError("Incomplete warmup/repeat contract")
            for trial in trials:
                if trial.get("source_sha256") and trial["source_sha256"] != identity:
                    raise ValueError("Captured input SHA256 mismatch")
            if digest(path) != identity:
                raise ValueError("Input changed during benchmark")
            observation["result"] = result
    except Exception as error:
        if process and process.poll() is None:
            process.kill()
            process.wait()
        observation.update(status="InfrastructureFailure", error=repr(error))
    finally:
        if process and process.poll() is None:
            process.kill()
            process.wait()
    write_json(directory/"observation.json", observation)
    return observation


def summarize(rows):
    groups = {}
    for row in rows:
        key = row["engine"]+":"+row["mode"]
        group = groups.setdefault(key, dict(files=0, supervisor_status=Counter(), outcomes=Counter(),
                                           diagnostics=Counter(),
                                           model_files=0, completed_operation_ms=[], successful_operation_ms=[]))
        group["files"] += 1
        observation = row["observation"]
        group["supervisor_status"][observation["status"]] += 1
        if observation["status"] != "Completed":
            continue
        trials = [t for t in observation["result"]["trials"] if not t["warmup"]]
        states = {t["status"] for t in trials}
        status = next(iter(states)) if len(states) == 1 else "InconsistentRepeatedOutcome"
        group["outcomes"][status] += 1
        group["diagnostics"].update({t["diagnostic_key"] for t in trials if t["diagnostic_key"]})
        complete_models = all(t["has_model"] for t in trials)
        group["model_files"] += complete_models
        times = [t["operation_ms"] for t in trials if t["operation_ms"] is not None]
        if times:
            group["completed_operation_ms"].append(statistics.median(times))
        successful = status in {"Inspected", "Accepted", "ReadDone", "Transferred"}
        if successful and times:
            group["successful_operation_ms"].append(statistics.median(times))
    for group in groups.values():
        for field in ("completed_operation_ms", "successful_operation_ms"):
            values = sorted(group.pop(field))
            group[field] = dict(files=len(values), median=statistics.median(values) if values else None,
                               p95=values[min(len(values)-1, int(len(values)*0.95))] if values else None,
                               total=sum(values))
    return dict(groups=groups, speedup_ratio=None,
                comparison_policy="Descriptive: raw parser vs typed read and strict import vs fixed transfer differ",
                timing_policy="Library time excludes probe JSON. Process wall includes startup/check/measure/cleanup. "
                              "Unsupported and failure times never enter successful-operation statistics.")


def batch(census_path, cohorts_path, output, probes, scope, *, repeat, warmup, timeout, maximum_private,
          occt_bin=None, resume=False):
    report = json.loads(census_path.read_bytes())
    cohorts = json.loads(cohorts_path.read_bytes())
    root = Path(report["root"])
    blind = {r["sha256"] for r in cohorts["holdout"]}
    if scope == "source-scan":
        selected = [r for r in report["files"] if r["sha256"] not in blind]
        modes = (("source", "source"),)
    elif scope == "paired":
        selected = cohorts["development"]
        modes = (("source", "source"), ("native", "import"), ("occt", "read"), ("occt", "transfer"))
    elif scope == "large":
        selected = cohorts["large"]
        modes = (("source", "source"), ("native", "import"), ("occt", "read"), ("occt", "transfer"))
    else:
        raise ValueError("Unknown benchmark scope")
    identities = {name: dict(path=str(path), sha256=digest(path)) for name, path in probes.items()}
    runtime_paths = [probes["native"].parent/name for name in ("SMLib.dll", "SM_API.dll")] if "native" in probes else []
    if occt_bin:
        runtime_paths += sorted(occt_bin.glob("*.dll"))
    runtime = {str(path.resolve(strict=True)): digest(path) for path in runtime_paths}
    driver_sources = {name: digest(Path(__file__).with_name(name)) for name in ("runner.py", "profile.py")}
    config = dict(format="step-benchmark-run.v1", census_sha256=digest(census_path),
                  cohorts_sha256=digest(cohorts_path),
                  probes=identities, runtime_files=runtime, driver_sources=driver_sources,
                  scope=scope, repeat=repeat, warmup=warmup, timeout=timeout,
                  maximum_private=maximum_private, occt_bin=str(occt_bin) if occt_bin else None)
    if output.exists():
        if not resume or json.loads((output/"run.json").read_bytes()) != config:
            raise ValueError("Existing batch requires matching explicit resume")
    else:
        output.mkdir(parents=True)
        write_json(output/"run.json", config)
    env = os.environ.copy()
    if occt_bin:
        env["PATH"] = str(occt_bin)+os.pathsep+env.get("PATH", "")
    rows = []
    for index, item in enumerate(selected, 1):
        if item["sha256"] in blind:
            raise ValueError("Holdout content entered a benchmark cohort")
        for engine, mode in modes:
            directory = output/"cases"/f"F{index:05d}"/(engine+"-"+mode)
            record = directory/"observation.json"
            previous_attempts = [record]
            if resume:
                previous_attempts += sorted(directory.parent.glob(directory.name+"-attempt*/observation.json"))
            retained = next((p for p in previous_attempts if p.exists()), None)
            if retained and resume:
                observation = json.loads(retained.read_bytes())
                if (observation["input_sha256"] != item["sha256"] or
                        digest(root/item["relative_path"]) != item["sha256"]):
                    raise ValueError("Resume input identity mismatch")
            else:
                if directory.exists():
                    # Interrupted probe artifacts stay intact; a new attempt has its own directory.
                    attempt = 2
                    while directory.with_name(directory.name+f"-attempt{attempt}").exists():
                        attempt += 1
                    directory = directory.with_name(directory.name+f"-attempt{attempt}")
                maximum = 512*1024*1024 if scope == "large" and engine == "occt" else 64*1024*1024
                observation = execute(probes[engine], mode, root/item["relative_path"], item["sha256"], directory,
                                      repeat=repeat, warmup=warmup, maximum_input=maximum, timeout=timeout,
                                      maximum_private=maximum_private, env=env)
            if observation["result"]:
                for key in ("TKernel", "TKDESTEP", "SMLib"):
                    loaded = observation["result"]["identity"].get(key)
                    allowed_runtime = {os.path.normcase(p) for p in runtime}
                    if loaded and os.path.normcase(str(Path(loaded).resolve(strict=True))) not in allowed_runtime:
                        raise ValueError("Probe loaded a runtime outside the pinned identity")
            rows.append(dict(case_id=f"F{index:05d}", relative_path=item["relative_path"], input_sha256=item["sha256"],
                             bytes=item["bytes"], engine=engine, mode=mode, observation=observation))
        if index % 25 == 0 or index == len(selected):
            print(f"{scope}: {index}/{len(selected)} files", flush=True)
            write_json(output/"progress.json", dict(completed=index, total=len(selected)))
    for name, identity in identities.items():
        if digest(probes[name]) != identity["sha256"]:
            raise ValueError("Probe binary changed during batch")
    if any(digest(Path(path)) != sha for path, sha in runtime.items()):
        raise ValueError("Runtime changed during batch")
    if any(digest(Path(__file__).with_name(name)) != sha for name, sha in driver_sources.items()):
        raise ValueError("Benchmark driver changed during batch")
    result = dict(config=config, rows=rows, summary=summarize(rows), holdout_content_count=len(blind),
                  holdout_files_excluded=sum(r["sha256"] in blind for r in report["files"]))
    write_json(output/"results.json", result)
    return result
