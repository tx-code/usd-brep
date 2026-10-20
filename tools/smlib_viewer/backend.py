# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Request lifetime, cancellation and atomic scene adoption for the viewer."""
from __future__ import annotations

from dataclasses import dataclass
import json
import os
from pathlib import Path
import sys
import tempfile

from PySide6 import QtCore
from .progress import Progress, ProgressReader
from .process_guard import ProcessGuard

CAD_EXTENSIONS = frozenset({".step", ".stp"})
MESH_EXTENSIONS = frozenset({".stl", ".obj", ".ply", ".vtp", ".vtk"})


@dataclass(frozen=True)
class ImportSettings:
    profile: str = "development"
    normalize_periodic: bool = False
    restrict_domains: bool = False
    strict_source: bool = False
    timeout_seconds: int = 120


def native_worker() -> Path:
    override = os.environ.get("BREP_VIEW_WORKER")
    suffix = ".exe" if sys.platform == "win32" else ""
    if override:
        return Path(override).resolve()
    if getattr(sys, "frozen", False):
        return Path(sys._MEIPASS) / "native" / ("step_view_export" + suffix)
    repo = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("SMLIB_CMAKE_BUILD_DIR", repo / "_build/cam"))
    for directory in (build / "Release", build / "bin/Release", build / "bin"):
        candidate = directory / ("step_view_export" + suffix)
        if candidate.is_file():
            return candidate
    return build / "bin/Release" / ("step_view_export" + suffix)


def scene_file(directory: Path, name: str) -> Path:
    """Manifest paths are request-relative; no worker may redirect a GUI read."""
    target = (directory / name).resolve()
    if not target.is_relative_to(directory.resolve()) or not target.is_file():
        raise ValueError("InvalidScenePath")
    return target


class SceneLoader(QtCore.QObject):
    started = QtCore.Signal(str)
    progress = QtCore.Signal(object)
    loaded = QtCore.Signal(str, object, object, object)
    failed = QtCore.Signal(str, str)
    cancelled = QtCore.Signal()

    def __init__(self, settings: ImportSettings, parent=None):
        super().__init__(parent)
        self.settings = settings
        self.generation = 0
        self.jobs = {}
        self.worker_result = None

    def load(self, filename: str) -> None:
        self.cancel(notify=False)
        self.worker_result = None
        path = Path(filename).expanduser().resolve()
        if not path.is_file():
            self.failed.emit("文件不存在", str(path))
            return
        if path.suffix.lower() not in CAD_EXTENSIONS | MESH_EXTENSIONS:
            self.failed.emit("暂不支持此文件格式", path.suffix)
            return
        generation = self.generation
        directory = tempfile.TemporaryDirectory(prefix="brep-view-")
        process = QtCore.QProcess(self)
        timer = QtCore.QTimer(self)
        timer.setSingleShot(True)
        job = dict(process=process, timer=timer, directory=directory, path=path, stderr=b"",
                   stopped=False, progress=ProgressReader(), guard=None)
        self.jobs[generation] = job
        if path.suffix.lower() in CAD_EXTENSIONS:
            program = native_worker()
            arguments = [str(path), directory.name, self.settings.profile, str(int(self.settings.normalize_periodic)),
                         str(int(self.settings.restrict_domains)), str(int(self.settings.strict_source))]
        else:
            program = Path(sys.executable)
            script = Path(__file__).resolve().parents[1] / "scripts/brep_view.py"
            entry = [] if getattr(sys, "frozen", False) else [str(script)]
            arguments = [*entry, "--mesh-worker", str(path), directory.name]
        if not program.is_file():
            self._dispose(generation)
            self.failed.emit("找不到导入进程", "请使用完整便携目录，或设置 BREP_VIEW_WORKER。")
            return
        environment = QtCore.QProcessEnvironment.systemEnvironment()
        runtime = str(Path(getattr(sys, "_MEIPASS", program.parent)))
        environment.insert("PATH", os.pathsep.join([str(program.parent), runtime, environment.value("PATH")]))
        process.setProcessEnvironment(environment)
        process.setWorkingDirectory(directory.name)
        process.setProgram(str(program))
        process.setArguments(arguments)  # QProcess quotes paths; never invoke a shell.
        process.started.connect(lambda: self._process_started(generation))
        process.readyReadStandardOutput.connect(lambda: self._output(generation))
        process.readyReadStandardError.connect(lambda: self._error_output(generation))
        process.finished.connect(lambda code, status: self._finished(generation, code))
        process.errorOccurred.connect(lambda error: self._process_error(generation, error))
        timer.timeout.connect(lambda: self._timeout(generation))
        self.started.emit(str(path))
        if generation != self.generation or job["stopped"]:
            self._dispose(generation)
            return
        self.progress.emit(Progress("start"))
        timer.start(self.settings.timeout_seconds * 1000)
        process.start()

    def _process_started(self, generation):
        job = self.jobs.get(generation)
        if not job:
            return
        try:
            job["guard"] = ProcessGuard(job["process"].processId())
            if generation != self.generation or job["stopped"]:
                self._stop(job)
                return
            # The worker waits until the parent has assigned its Windows job.
            job["process"].write(b"start\n")
            job["process"].closeWriteChannel()
        except OSError as error:
            self._stop(job)
            self.failed.emit("无法隔离导入进程", str(error))

    def _output(self, generation):
        job = self.jobs.get(generation)
        if not job:
            return
        updates = job["progress"].feed(bytes(job["process"].readAllStandardOutput()))
        if generation == self.generation and not job["stopped"]:
            for update in updates:
                self.progress.emit(update)

    def _error_output(self, generation):
        job = self.jobs.get(generation)
        if job:
            job["stderr"] = (job["stderr"] + bytes(job["process"].readAllStandardError()))[-65536:]

    def _process_error(self, generation, error):
        if error == QtCore.QProcess.FailedToStart and generation in self.jobs:
            current = generation == self.generation
            self._dispose(generation)
            if current:
                self.failed.emit("导入进程无法启动", "请检查便携目录中的运行库是否完整。")

    def _finished(self, generation, code):
        job = self.jobs.get(generation)
        if not job:
            return
        self._output(generation)
        if generation == self.generation and not job["stopped"]:
            self.progress.emit(Progress("display"))
        # Let the caption paint and recheck cancellation/new requests before VTK
        # reads. No global processEvents may service unrelated viewer instances.
        QtCore.QTimer.singleShot(0, lambda: self._adopt(generation, code))

    def _adopt(self, generation, code):
        job = self.jobs.get(generation)
        if not job:
            return
        try:
            # A cancelled/stale request may finish successfully. Its scene must
            # never replace a newer request, even if its manifest is complete.
            if generation != self.generation or job["stopped"]:
                return
            directory = Path(job["directory"].name)
            manifest_path = directory / "scene.json"
            if not manifest_path.is_file() or manifest_path.stat().st_size > 65536:
                raise ValueError("WorkerFailedOrCrashed")
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if manifest.get("format") != "brep-view-scene.v1":
                raise ValueError("SceneVersionMismatch")
            # Retain only the current request's bounded manifest, before its
            # temporary payloads are released. No borrowed paths escape here.
            self.worker_result = {**manifest, "exit_code": code}
            if code != 0 or manifest.get("status") != "Ready":
                messages = [f"{d['key']}: {d['message']}" for d in manifest.get("diagnostics", [])]
                self.failed.emit("模型未能载入", "\n".join([manifest.get("status", ""), manifest.get("key", ""), *messages]))
                return
            import pyvista as pv
            import numpy as np
            mesh = pv.read(scene_file(directory, manifest["mesh"]))
            if (not isinstance(mesh, pv.PolyData) or not mesh.n_cells or mesh.n_cells > 2000000
                    or mesh.n_points > 6000000 or not np.isfinite(mesh.points).all()):
                raise ValueError("InvalidDisplayMesh")
            boundary = pv.read(scene_file(directory, manifest["boundary"])) if manifest.get("boundary") else None
            self.progress.emit(Progress("render"))
            if generation != self.generation or job["stopped"]:
                return
            self.loaded.emit(str(job["path"]), mesh, boundary, manifest)
        except Exception as error:
            if generation == self.generation:
                self.failed.emit("显示数据载入失败", str(error))
        finally:
            self._dispose(generation)

    def _timeout(self, generation):
        job = self.jobs.get(generation)
        if job and generation == self.generation:
            self._stop(job)
            self.failed.emit("载入超时", "导入进程已终止；没有发布部分模型。")

    def cancel(self, notify=True):
        self.generation += 1
        active = False
        for job in self.jobs.values():
            if job["process"].state() != QtCore.QProcess.NotRunning:
                active = True
                self._stop(job)
            elif not job["stopped"]:
                active = True
                job["stopped"] = True
        if active and notify:
            self.cancelled.emit()

    @staticmethod
    def _stop(job):
        job["stopped"] = True
        if job["guard"]:
            job["guard"].close()
        job["process"].kill()

    def _dispose(self, generation):
        job = self.jobs.pop(generation, None)
        if job:
            if job["guard"]:
                job["guard"].close()
            job["timer"].stop()
            job["timer"].deleteLater()
            job["process"].deleteLater()
            # TemporaryDirectory owns only the directory created for this job;
            # source paths are never used as cleanup targets.
            job["directory"].cleanup()

    def close(self):
        self.cancel(notify=False)
        for generation, job in list(self.jobs.items()):
            job["process"].waitForFinished(2000)
            if generation in self.jobs:
                self._dispose(generation)
