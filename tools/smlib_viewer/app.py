# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Minimal model window using the workbench's viewport and camera controls."""
from __future__ import annotations

from pathlib import Path
import time

from tools.smlib_gui.runtime import QtCore, QtGui, QtWidgets, QtInteractor, np, pv
from tools.smlib_gui.camera_fit import fit_depth
from tools.smlib_gui.viewport import ViewportController
from tools.smlib_gui.settings import DISPLAY_MODE_SHADED_MESH_WIREFRAME, DISPLAY_MODE_SHADED, COLOR_MODE_MATERIAL
from .backend import CAD_EXTENSIONS, MESH_EXTENSIONS, ImportSettings, SceneLoader


class ViewerWindow(QtWidgets.QMainWindow):
    load_complete = QtCore.Signal(bool)

    def __init__(self, settings=None, *, edges=False, wireframe=False):
        super().__init__()
        self.setWindowTitle("BRep Viewer")
        self.resize(1200, 800)
        self.setMinimumSize(520, 360)
        self.setAcceptDrops(True)
        self.mesh = self.boundary = self.boundary_actor = None
        self.metadata = {}
        self.filename = ""
        self.load_started = 0.
        self._depth_pending = self._fitting_depth = False
        self.loader = SceneLoader(settings or ImportSettings(), self)
        self.loader.started.connect(self._started)
        self.loader.progress.connect(self._progress)
        self.loader.loaded.connect(self._loaded)
        self.loader.failed.connect(self._failed)
        self.loader.cancelled.connect(lambda: self._failed("已取消载入", ""))
        frame = QtWidgets.QWidget(self)
        layout = QtWidgets.QVBoxLayout(frame)
        layout.setContentsMargins(8, 6, 8, 8)
        layout.setSpacing(6)
        self.setCentralWidget(frame)
        bar = QtWidgets.QHBoxLayout()
        self.file_label = QtWidgets.QLabel("BRep Viewer")
        self.file_label.setTextFormat(QtCore.Qt.PlainText)
        bar.addWidget(self.file_label, 1)
        actions = [("打开", self.open_dialog), ("适配 F", self.fit),
                   ("斜视 I", lambda: self.named_view("isometric"))]
        for text, callback in actions:
            button = QtWidgets.QPushButton(text)
            button.clicked.connect(callback)
            bar.addWidget(button)
        self.edges_button = QtWidgets.QPushButton("边界 E")
        self.edges_button.setCheckable(True)
        self.edges_button.setChecked(edges)
        self.edges_button.toggled.connect(self._boundary_style)
        bar.addWidget(self.edges_button)
        self.wire_button = QtWidgets.QPushButton("网格 W")
        self.wire_button.setCheckable(True)
        self.wire_button.setChecked(wireframe)
        self.wire_button.toggled.connect(self._redraw)
        bar.addWidget(self.wire_button)
        self.cancel_button = QtWidgets.QPushButton("取消")
        self.cancel_button.clicked.connect(self.loader.cancel)
        self.cancel_button.hide()
        bar.addWidget(self.cancel_button)
        layout.addLayout(bar)
        self.plot = QtInteractor(frame, auto_update=False)
        self.plot.set_background("#25303b")
        layout.addWidget(self.plot.interactor, 1)
        self.viewport = ViewportController(self.plot)
        self.hud = QtWidgets.QLabel(self.plot.interactor)
        self.hud.setTextFormat(QtCore.Qt.PlainText)
        self.hud.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
        self.hud.setStyleSheet("background:rgba(21,29,39,220);color:#e4edf6;padding:10px;border-radius:6px;")
        self.hud.move(14, 14)
        self.hud.setText("拖入模型或按 Ctrl+O 打开\nSTEP · STL · OBJ · PLY · VTP · VTK")
        self.hud.adjustSize()
        self.status = QtWidgets.QLabel(
            "F 适配  ·  I 斜视  ·  1/2/3 正/俯/右视  ·  E 边界  ·  W 网格  ·  H 信息")
        self.status.setTextFormat(QtCore.Qt.PlainText)
        layout.addWidget(self.status)
        self.progress_bar = QtWidgets.QProgressBar()
        self.progress_bar.setFixedHeight(8)
        self.progress_bar.setTextVisible(False)
        self.progress_bar.hide()
        layout.addWidget(self.progress_bar)
        self.setStyleSheet("""
            QMainWindow,QWidget { background:#1c2530; color:#dfe9f2;
                                  font-family:'Microsoft YaHei UI'; font-size:11pt; }
            QPushButton { background:#2a3949; color:#edf5ff; border:1px solid #4f657b;
                          border-radius:4px; padding:5px 10px; }
            QPushButton:hover { background:#3b5269; }
            QPushButton:checked { background:#326792; border-color:#81b7df; }
            QLabel { background:transparent; }
            QProgressBar { background:#121a23; border:0; border-radius:3px; }
            QProgressBar::chunk { background:#67b9ef; border-radius:3px; }
        """)
        self.shortcuts = []
        for key, action in [("Ctrl+O", self.open_dialog), ("F", self.fit),
                            ("I", lambda: self.named_view("isometric")),
                            ("1", lambda: self.named_view("front")), ("2", lambda: self.named_view("top")),
                            ("3", lambda: self.named_view("right")), ("E", self.edges_button.toggle),
                            ("W", self.wire_button.toggle),
                            ("H", lambda: self.hud.setVisible(not self.hud.isVisible())),
                            ("R", self.reload), ("Ctrl+Q", self.close), ("Escape", self.loader.cancel)]:
            shortcut = QtGui.QShortcut(QtGui.QKeySequence(key), self)
            shortcut.activated.connect(action)
            self.shortcuts.append(shortcut)
        self.plot.camera.parallel_projection = True
        self.plot.camera.AddObserver("ModifiedEvent", self._camera_changed)
        self.plot.interactor.installEventFilter(self)

    def open_dialog(self):
        filename, _ = QtWidgets.QFileDialog.getOpenFileName(self, "打开模型", "",
            "模型 (*.step *.stp *.stl *.obj *.ply *.vtp *.vtk);;所有文件 (*)")
        if filename:
            self.load(filename)

    def load(self, filename):
        self.loader.load(str(filename))

    def reload(self):
        if self.filename:
            self.load(self.filename)

    def _started(self, filename):
        self.load_started = time.perf_counter()
        self.cancel_button.show()
        self.status.setToolTip("")
        self.progress_bar.setRange(0, 0)
        self.progress_bar.show()
        self.status.setText("正在载入 " + Path(filename).name + " …")

    def _progress(self, update):
        # The bar denotes whole-scene completion. A stage's measured count is
        # shown as text; unknown remaining work never becomes a guessed percent.
        self.status.setText(update.text)

    def _loaded(self, filename, mesh, boundary, metadata):
        previous = self.mesh, self.boundary, self.metadata, self.filename
        try:
            self.mesh, self.boundary, self.metadata, self.filename = mesh, boundary, metadata, filename
            self._redraw()
            self.named_view("isometric")
            self.file_label.setText(Path(filename).name)
            self.setWindowTitle(Path(filename).name + " — BRep Viewer")
            roles = {"MaterialSolid": "实体", "SurfaceModel": "表面模型", "Mesh": "网格"}
            role = roles.get(metadata.get("role"), "BRep")
            unit = "mm" if metadata.get("units") == "mm" else "单位未声明"
            dimensions = np.asarray(mesh.bounds)[1::2] - np.asarray(mesh.bounds)[::2]
            size = " × ".join(f"{value:.5g}" for value in dimensions)
            counts = f"{mesh.n_points:,} 点  ·  {mesh.n_cells:,} 面片"
            if "faces" in metadata:
                counts = (
                    f"BRep {metadata['faces']:,} 面 / {metadata['edges']:,} 边 / {metadata['vertices']:,} 点\n" + counts
                )
            study = self.loader.settings.profile != "development" and metadata.get("units") == "mm"
            policy = "\n研究配置" if study else ""
            self.hud.setText(f"{role}  ·  只读预览{policy}\n{counts}\n尺寸 ≈ {size}  {unit}")
            self.hud.adjustSize()
            self.cancel_button.hide()
            self.progress_bar.setRange(0, 100)
            self.progress_bar.setValue(100)
            self.status.setText(
                f"载入完成 · {time.perf_counter()-self.load_started:.3f} s    |    F 适配 · I 斜视 · E 边界 · W 网格 · H 信息"
            )
            self.load_complete.emit(True)
        except Exception as error:
            self.mesh, self.boundary, self.metadata, self.filename = previous
            if self.mesh is not None:
                try:
                    self._redraw()
                except Exception:
                    pass
            self._failed("显示失败", str(error))

    def _failed(self, title, details):
        self.cancel_button.hide()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.status.setText(title + ("：" + details.splitlines()[0] if details else ""))
        self.status.setToolTip(details)
        if self.mesh is None:
            self.hud.setText(title + "\n" + details[:700])
            self.hud.adjustSize()
        # Keep an existing accepted scene labelled with its original filename.
        # A failed new request never re-labels it as the failed file.
        self.load_complete.emit(False)

    def _redraw(self, *_):
        if self.mesh is None:
            return
        mesh = self.mesh.copy(deep=True)
        mesh.field_data["DisplayColor"] = np.array([.76, .83, .9])
        mode = DISPLAY_MODE_SHADED_MESH_WIREFRAME if self.wire_button.isChecked() else DISPLAY_MODE_SHADED
        self.viewport.render_meshes([mesh], mode, COLOR_MODE_MATERIAL, render=False, preserve_sharp_edges=True)
        self.boundary_actor = None
        if self.boundary is not None and self.boundary.n_cells:
            self.boundary_actor = self.plot.add_mesh(self.boundary, color="#101923", line_width=1.3,
                                                    lighting=False, render=False, reset_camera=False)
            self.boundary_actor.SetVisibility(self.edges_button.isChecked())
        self.plot.add_axes()
        self._fit_depth()

    def _boundary_style(self, *_):
        if self.boundary_actor is not None:
            self.boundary_actor.SetVisibility(self.edges_button.isChecked())
            self.plot.render()

    def fit(self):
        if self.mesh is not None:
            self.viewport.fit_view(render=False)
            self._fit_depth()

    def named_view(self, name):
        if self.mesh is not None:
            self.viewport.set_named_view(name)
            self._fit_depth()

    def _camera_changed(self, *_):
        if self.mesh is not None and not self._depth_pending and not self._fitting_depth:
            self._depth_pending = True
            QtCore.QTimer.singleShot(0, self._fit_depth)

    def _fit_depth(self):
        self._depth_pending = False
        if self.mesh is None or self._fitting_depth:
            return
        self._fitting_depth = True
        try:
            fit_depth(self.plot.camera, self.mesh.bounds)
            self.plot.render()
        finally:
            self._fitting_depth = False

    def dragEnterEvent(self, event):
        if (event.mimeData().hasUrls() and len(event.mimeData().urls()) == 1
                and event.mimeData().urls()[0].isLocalFile()):
            event.acceptProposedAction()

    def dropEvent(self, event):
        if (event.mimeData().hasUrls() and len(event.mimeData().urls()) == 1
                and event.mimeData().urls()[0].isLocalFile()):
            self.load(event.mimeData().urls()[0].toLocalFile())
            event.acceptProposedAction()

    def eventFilter(self, target, event):
        if event.type() == QtCore.QEvent.DragEnter:
            self.dragEnterEvent(event)
            return event.isAccepted()
        if event.type() == QtCore.QEvent.Drop:
            self.dropEvent(event)
            return event.isAccepted()
        return super().eventFilter(target, event)

    def closeEvent(self, event):
        self.loader.close()
        self.plot.close()
        event.accept()


def run(options):
    application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    application.setStyle("Fusion")
    settings = ImportSettings(options.profile, options.normalize_periodic_faces,
                              options.restrict_unused_support_domains, options.strict_source)
    window = ViewerWindow(settings, edges=options.edges, wireframe=options.wireframe)
    result = [0]

    def finished(success):
        result[0] = 0 if success else 1
        def complete():
            try:
                if success and options.screenshot:
                    options.screenshot.parent.mkdir(parents=True, exist_ok=True)
                    # QWidget.grab cannot capture the native VTK child window.
                    # Read the rendered viewport; Qt HUD/widgets stay on screen.
                    window.plot.screenshot(str(options.screenshot))
            except Exception as error:
                result[0] = 1
                window.status.setText("Screenshot failed: " + str(error))
            finally:
                if options.quit_after_load:
                    window.close()
        QtCore.QTimer.singleShot(150, complete)
    window.load_complete.connect(finished)
    window.show()
    if options.file:
        QtCore.QTimer.singleShot(0, lambda: window.load(options.file))
    elif options.quit_after_load:
        window.close()
        return 2
    application.exec()
    return result[0]
