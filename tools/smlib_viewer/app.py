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
from .backend import ImportSettings, SceneLoader
from .appearance import SURFACE_COLOR, configure_scene, style_surface, verify_model_frame
from .picking import TopologyPicker


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
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)
        self.setCentralWidget(frame)
        toolbar = QtWidgets.QFrame()
        toolbar.setObjectName("toolbar")
        toolbar.setFixedHeight(44)
        bar = QtWidgets.QHBoxLayout(toolbar)
        bar.setContentsMargins(14, 4, 10, 4)
        bar.setSpacing(4)
        self.file_label = QtWidgets.QLabel("BRep Viewer")
        self.file_label.setMinimumWidth(0)
        self.file_label.setSizePolicy(QtWidgets.QSizePolicy.Ignored, QtWidgets.QSizePolicy.Preferred)
        self.file_label.setTextFormat(QtCore.Qt.PlainText)
        bar.addWidget(self.file_label, 1)
        self.definition_selector = QtWidgets.QComboBox()
        self.definition_selector.setMaximumWidth(235)
        self.definition_selector.setToolTip("切换独立零件定义；坐标属于当前定义，不代表装配位置")
        self.definition_selector.currentIndexChanged.connect(self._switch_definition)
        self.definition_selector.hide()
        bar.addWidget(self.definition_selector)
        actions = [("打开", "打开文件 · Ctrl+O", self.open_dialog), ("适配", "适配视图 · F", self.fit),
                   ("斜视", "等轴测视图 · I", lambda: self.named_view("isometric"))]
        for text, tip, callback in actions:
            button = QtWidgets.QToolButton()
            button.setText(text)
            button.setToolTip(tip)
            button.clicked.connect(callback)
            bar.addWidget(button)
        separator = QtWidgets.QFrame()
        separator.setFixedSize(1, 18)
        separator.setStyleSheet("background:#3a4350;")
        bar.addSpacing(5)
        bar.addWidget(separator)
        bar.addSpacing(5)
        self.edges_button = QtWidgets.QToolButton()
        self.edges_button.setText("边线")
        self.edges_button.setToolTip("显示 BRep 边线 · E")
        self.edges_button.setCheckable(True)
        self.edges_button.setChecked(edges)
        self.edges_button.toggled.connect(self._boundary_style)
        bar.addWidget(self.edges_button)
        self.wire_button = QtWidgets.QToolButton()
        self.wire_button.setText("网格")
        self.wire_button.setToolTip("显示三角网格 · W")
        self.wire_button.setCheckable(True)
        self.wire_button.setChecked(wireframe)
        self.wire_button.toggled.connect(self._redraw)
        bar.addWidget(self.wire_button)
        self.cancel_button = QtWidgets.QToolButton()
        self.cancel_button.setText("取消")
        self.cancel_button.setToolTip("取消加载 · Escape")
        self.cancel_button.clicked.connect(self.loader.cancel)
        self.cancel_button.hide()
        bar.addWidget(self.cancel_button)
        layout.addWidget(toolbar)
        self.plot = QtInteractor(frame, auto_update=False)
        configure_scene(self.plot)
        layout.addWidget(self.plot.interactor, 1)
        self.viewport = ViewportController(self.plot)
        self.picker = TopologyPicker(self.plot)
        self.selected = []
        self.hover_hit = None
        self._scene_summary = ""
        self._mouse_press = self._mouse_position = None
        self._mouse_dragged = False
        self._hover_timer = QtCore.QTimer(self)
        self._hover_timer.setSingleShot(True)
        self._hover_timer.setInterval(40)
        self._hover_timer.timeout.connect(self._update_hover)
        self.plot.interactor.setMouseTracking(True)
        self.hud = QtWidgets.QLabel(self.plot.interactor)
        self.hud.setTextFormat(QtCore.Qt.PlainText)
        self.hud.setAttribute(QtCore.Qt.WA_TransparentForMouseEvents)
        # QVTK is a native OpenGL child, not part of Qt's backing store. Alpha
        # blending a label over that store can blend against its own previous
        # text after resize/reload, leaving doubled glyphs. Paint an opaque HUD.
        self.hud.setStyleSheet("background:#12171f;color:#dce3ed;"
                               "padding:9px 12px;border-radius:5px;font-size:10pt;")
        self.hud.move(14, 14)
        self.hud.setText("拖入模型\nCtrl+O 打开 · H 隐藏信息")
        self.hud.adjustSize()
        self.status = QtWidgets.QLabel("就绪")
        self.status.setTextFormat(QtCore.Qt.PlainText)
        self.status.setContentsMargins(14, 0, 14, 0)
        self.status.setFixedHeight(26)
        self.status.setStyleSheet("color:#9aa6b5;font-size:9pt;")
        layout.addWidget(self.status)
        self.progress_bar = QtWidgets.QProgressBar()
        self.progress_bar.setFixedHeight(3)
        self.progress_bar.setTextVisible(False)
        self.progress_bar.hide()
        layout.addWidget(self.progress_bar)
        self.setStyleSheet("""
            QMainWindow { background:#171c24; }
            QWidget { color:#d6dee9; font-family:'Microsoft YaHei UI'; font-size:10pt; }
            QFrame#toolbar { background:#1b212b; border-bottom:1px solid #303946; }
            QToolButton { background:transparent; color:#bfcad9; border:1px solid transparent;
                          border-radius:4px; padding:5px 10px; }
            QToolButton:hover { background:#2c3644; color:#ffffff; }
            QToolButton:checked { background:#263f50; border-color:#426377; color:#a7dbf2; }
            QToolTip { background:#26303d; color:#e2e9f2; border:1px solid #48566a; padding:5px; }
            QComboBox { background:#26303d; color:#e2e9f2; border:1px solid #48566a;
                        border-radius:4px; padding:4px; }
            QComboBox QAbstractItemView { background:#26303d; color:#e2e9f2;
                                        selection-background-color:#365567; }
            QLabel { background:transparent; }
            QProgressBar { background:#202936; border:0; }
            QProgressBar::chunk { background:#73b8cc; }
        """)
        self.shortcuts = []
        for key, action in [("Ctrl+O", self.open_dialog), ("F", self.fit),
                            ("I", lambda: self.named_view("isometric")),
                            ("1", lambda: self.named_view("front")), ("2", lambda: self.named_view("top")),
                            ("3", lambda: self.named_view("right")), ("E", self.edges_button.toggle),
                            ("W", self.wire_button.toggle),
                            ("Ctrl+D", self.clear_selection),
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
        self.status.setStyleSheet("color:#9aa6b5;font-size:9pt;")
        self.progress_bar.setRange(0, 0)
        self.progress_bar.show()
        self.status.setText("正在载入 " + Path(filename).name + " …")

    def _progress(self, update):
        # The bar denotes whole-scene completion. A stage's measured count is
        # shown as text; unknown remaining work never becomes a guessed percent.
        self.status.setText(update.text)

    def _sync_definitions(self):
        definitions = self.metadata.get('_definitions', ())
        # A display projection is disposable; all owning definition scenes stay
        # with the accepted request. Updating this control must not re-enter
        # adoption or replace a failed selection with a new file request.
        blocked = self.definition_selector.blockSignals(True)
        try:
            self.definition_selector.clear()
            for index in range(len(definitions)):
                self.definition_selector.addItem(f"定义 {index + 1}/{len(definitions)} · 本地坐标")
            if definitions:
                self.definition_selector.setCurrentIndex(self.metadata['definition_index'])
            self.definition_selector.setVisible(len(definitions) > 1)
        finally:
            self.definition_selector.blockSignals(blocked)

    def _switch_definition(self, index):
        definitions = self.metadata.get('_definitions', ())
        if not 0 <= index < len(definitions) or index == self.metadata.get('definition_index'):
            return
        scene = definitions[index]
        self._loaded(self.filename, scene.mesh, scene.boundary,
                     dict(scene.metadata, _definitions=definitions), definition_switch=True)

    def _loaded(self, filename, mesh, boundary, metadata, *, definition_switch=False):
        previous = self.mesh, self.boundary, self.metadata, self.filename, self.selected, self._scene_summary
        try:
            self.selected = []
            self.hover_hit = None
            self.mesh, self.boundary, self.metadata, self.filename = mesh, boundary, metadata, filename
            self._redraw()
            self.named_view("isometric")
            self._arrange_overlays()
            self.file_label.setToolTip(Path(filename).name)
            self.setWindowTitle(Path(filename).name + " — BRep Viewer")
            roles = {"MaterialSolid": "实体", "SurfaceModel": "表面模型", "Mesh": "网格"}
            role = roles.get(metadata.get("role"), "BRep")
            if metadata.get('role') == 'MaterialSolidCollection':
                role = f"实体集合 · {metadata['body_count']} 件"
            if metadata.get('role') == 'Assembly':
                role = f"装配 · {metadata['instance_count']} 个节点 / {metadata['body_count']} 个形状实例"
                if metadata.get('retained_source_record_count'):
                    role += f"\n保留 {metadata['retained_source_record_count']} 条未引用源声明"
            if metadata.get('_definitions'):
                role = f"定义 {metadata['definition_index'] + 1}/{metadata['definition_count']} · 本地坐标\n" + role
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
            selection = metadata.get('definition_selection')
            if selection:
                policy += f"\n选入 {len(selection['selected_definitions'])}/{selection['source_definition_count']} 个源定义"
                if selection.get('isolated_source_failure_count'):
                    policy += f"\n隔离 {selection['isolated_source_failure_count']} 处未选定义的源错误"
                    if metadata.get('source_interpretation_applied'):
                        policy += '\n源解释模式 · 条件接受'
            if metadata.get("native_import_status") == "AcceptedUnderInterpretation":
                policy += "\n源解释模式 · 条件接受"
            if metadata.get('repair_applied'):
                if metadata.get('repair_kind') == 'CylindricalBandWithCircleVertices':
                    policy += (f"\n已修复 {metadata['repaired_face_count']} 处周期边界角色"
                               f" · {metadata['repaired_vertex_count']} 个圆边顶点")
                else:
                    kind = '周期边界角色' if metadata.get('repair_kind') == 'CylindricalBandRoles' else '面朝向'
                    policy += f"\n已修复 {metadata['repaired_face_count']} 处{kind}"
                if metadata.get('source_interpretation_applied'):
                    policy += '\n源解释模式 · 条件接受'
            if metadata.get('applied_unit_interpretation') == 'VerifiedExplicitUnitDimensions':
                policy += "\n单位声明经验证解释"
            if metadata.get("ascii_invariant_data_applied"):
                policy += "\n文本编码未验证"
            self.hud.setText(f"{role}{policy}\n{counts}\n≈ {size}  {unit}")
            auxiliary = metadata.get("_auxiliary")
            if auxiliary:
                info = auxiliary.counts
                text = f"\n附属：线框 {info['wireframe_groups']} · 构造 {info['supporting_groups']}"
                if info["planes"]:
                    text += "\n无限平面以预览窗口显示"
                self.hud.setText(self.hud.text() + text)
            self._scene_summary = self.hud.text()
            self._update_measurements()
            self._sync_definitions()
            if definition_switch:
                self.status.setText("已切换独立定义 · 本地坐标 · 只读预览")
                return
            self.cancel_button.hide()
            self.progress_bar.setRange(0, 100)
            self.progress_bar.setValue(100)
            self.status.setText(
                f"已载入 · {time.perf_counter()-self.load_started:.3f} s · 只读预览"
            )
            self._arrange_overlays()
            generation = self.loader.generation
            QtCore.QTimer.singleShot(1500, lambda: self._hide_completed_progress(generation))
            self.load_complete.emit(True)
        except Exception as error:
            self.mesh, self.boundary, self.metadata, self.filename, self.selected, self._scene_summary = previous
            if self.mesh is not None:
                try:
                    self._redraw()
                except Exception:
                    pass
            self._sync_definitions()
            self._failed("显示失败", str(error), notify=not definition_switch)

    def _failed(self, title, details, *, notify=True):
        self.cancel_button.hide()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.status.setText(title + ("：" + details.splitlines()[0] if details else ""))
        self.status.setToolTip(details)
        self.status.setStyleSheet("color:#efb18d;font-size:9pt;")
        if self.mesh is None:
            self.hud.setText(title + "\n" + details[:700])
            self.hud.adjustSize()
            self._arrange_overlays()
        # Keep an existing accepted scene labelled with its original filename.
        # A failed new request never re-labels it as the failed file.
        if notify:
            self.load_complete.emit(False)

    def _redraw(self, *_):
        if self.mesh is None:
            return
        mesh = self.mesh.copy(deep=True)
        mesh.field_data["DisplayColor"] = np.array(SURFACE_COLOR)
        mode = DISPLAY_MODE_SHADED_MESH_WIREFRAME if self.wire_button.isChecked() else DISPLAY_MODE_SHADED
        self.picker.clear()
        self.viewport.render_meshes([mesh], mode, COLOR_MODE_MATERIAL, render=False, preserve_sharp_edges=True)
        body_actor = next((a for a in self.plot.renderer.actors.values() if isinstance(a, pv.Actor)), None)
        for actor in self.plot.renderer.actors.values():
            if isinstance(actor, pv.Actor):
                style_surface(actor)
        self.boundary_actor = None
        if self.boundary is not None and self.boundary.n_cells:
            self.boundary_actor = self.plot.add_mesh(self.boundary, color="#101923", line_width=1.3,
                                                    lighting=False, render=False, reset_camera=False)
            self.boundary_actor.SetVisibility(self.edges_button.isChecked())
        self.auxiliary_actors = []
        auxiliary = self.metadata.get("_auxiliary")
        self._scene_bounds = auxiliary.combined_bounds(self.mesh.bounds) if auxiliary else self.mesh.bounds
        if auxiliary:
            self.auxiliary_actors = auxiliary.render(self.plot)
        self.plot.add_axes(color="#c8d0dc", viewport=(0.88, 0, 1, 0.16))
        self.picker.set_scene(mesh, self.boundary, self.metadata.get("_topology"), body_actor)
        # Selection keys survive a redraw of this snapshot only. A new successful
        # load clears them before rendering; stale picker hits never cross scenes.
        self.hover_hit = None
        self._restore_selection()
        self._fit_depth()

    def _restore_selection(self):
        # Rebind through the current picker, without manufacturing scene tokens.
        hits = [self.picker.resolve(kind, index) for kind, index in self.selected]
        self.picker.highlight_many([hit for hit in hits if hit is not None])
        self._update_measurements()

    def clear_selection(self):
        self.selected = []
        self.hover_hit = None
        self.picker.clear_highlight("hover")
        self.picker.clear_highlight("selection")
        self._update_measurements()
        self.plot.render()

    def _device_position(self, position):
        width, height = self.plot.render_window.GetSize()
        widget = self.plot.interactor
        return (position.x() * width / max(1, widget.width()),
                height - 1 - position.y() * height / max(1, widget.height()))

    def _update_hover(self):
        if self._mouse_position is None or self._mouse_press is not None:
            return
        hit = self.picker.pick(*self._device_position(self._mouse_position))
        # Moving within the same current feature does not change its geometry,
        # measurement or color. Avoid extracting its triangles and rendering a
        # replacement actor on every mouse tick; camera/scene changes invalidate
        # hover_hit separately, as do selection changes below.
        if hit == self.hover_hit and (hit is None or self.picker.is_current(self.hover_hit)):
            return
        self.hover_hit = hit
        # A selected feature already owns an overlay. Do not stack another
        # coplanar hover layer over it or change its selected color.
        selected = self.hover_hit and (self.hover_hit.kind, self.hover_hit.id) in self.selected
        self.picker.highlight(None if selected else self.hover_hit)
        self._update_measurements()
        self.plot.render()

    def _select_at(self, position, additive=False):
        hit = self.picker.pick(*self._device_position(position))
        self.hover_hit = None
        self.picker.clear_highlight("hover")
        key = (hit.kind, hit.id) if hit else None
        if not additive:
            self.selected = [key] if key else []
        elif key:
            self.selected = ([item for item in self.selected if item != key] if key in self.selected
                             else (self.selected + [key])[-2:])
        self._restore_selection()
        self.plot.render()

    def _update_measurements(self):
        if not hasattr(self, "hud") or not self._scene_summary:
            return
        lines = [self._scene_summary]
        hits = [self.picker.resolve(kind, index) for kind, index in self.selected]
        if self.hover_hit and (self.hover_hit.kind, self.hover_hit.id) not in self.selected:
            hits.append(self.hover_hit)
        for hit in hits:
            info = self.picker.info(hit)
            if info is None:
                continue
            prefix = "选中" if (hit.kind, hit.id) in self.selected else "悬停"
            local_id = info.local_id if info.local_id is not None else hit.id
            if info.local_id is not None:
                origins = self.metadata['_topology'].bodies
                origin = origins[info.body] if info.body < len(origins) else None
                if origin is not None and origin.instance_index is not None:
                    prefix += f" 实例 {origin.instance_index + 1} ·"
                else:
                    prefix += f" 实体 {info.body + 1} ·"
            if hit.kind == "vertex":
                value = " · ".join(f"{coordinate:.7g}" for coordinate in info.point_mm)
                lines.append(f"{prefix} 点 {local_id}：({value}) mm")
            else:
                face = hit.kind == "face"
                value = info.area_mm2 if face else info.length_mm
                measure = (f"≈ {value:.7g} {'mm²' if face else 'mm'}" if value is not None
                           else f"不可用 ({info.measurement_status})")
                lines.append(f"{prefix} {'面' if face else '边'} {local_id}：{measure}")
        if len(self.selected) == 2 and all(kind == "vertex" for kind, _ in self.selected):
            points = [self.picker.info(self.picker.resolve(kind, index)) for kind, index in self.selected]
            if all(point is not None for point in points):
                distance = np.linalg.norm(np.array(points[0].point_mm) - points[1].point_mm)
                lines.append(f"两点距离：{distance:.7g} mm")
        if self.metadata.get("_topology"):
            lines.append("点击选择 · Ctrl+点击双选 · Ctrl+D 清除")
        self.hud.setText("\n".join(lines))
        self.hud.adjustSize()
        self._arrange_overlays()

    def _boundary_style(self, *_):
        if self.boundary_actor is not None:
            self.boundary_actor.SetVisibility(self.edges_button.isChecked())
            self.plot.render()

    def fit(self):
        if self.mesh is not None:
            self.viewport.fit_view(render=False)
            self.plot.reset_camera(bounds=self._scene_bounds, render=False)
            self.plot.camera.parallel_scale *= 1.12
            self._fit_depth()

    def named_view(self, name):
        if self.mesh is not None:
            self.viewport.set_named_view(name)
            self.plot.reset_camera(bounds=self._scene_bounds, render=False)
            self.plot.camera.parallel_scale *= 1.12
            self._fit_depth()

    def _camera_changed(self, *_):
        if hasattr(self, "picker"):
            self.hover_hit = None
            self.picker.clear_highlight("hover")
            self._update_measurements()
        if self.mesh is not None and not self._depth_pending and not self._fitting_depth:
            self._depth_pending = True
            QtCore.QTimer.singleShot(0, self._fit_depth)

    def _fit_depth(self):
        self._depth_pending = False
        if self.mesh is None or self._fitting_depth:
            return
        self._fitting_depth = True
        try:
            fit_depth(self.plot.camera, self._scene_bounds)
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

    def _arrange_overlays(self):
        if not hasattr(self, "hud"):
            return
        self.hud.move(16, max(12, self.plot.interactor.height() - self.hud.height() - 16))
        title = Path(self.filename).name if self.filename else "BRep Viewer"
        self.file_label.setText(self.file_label.fontMetrics().elidedText(
            title, QtCore.Qt.ElideMiddle, max(30, self.file_label.width() - 4)))

    def _hide_completed_progress(self, generation):
        if generation == self.loader.generation and self.progress_bar.value() == 100:
            self.progress_bar.hide()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if hasattr(self, "hud"):
            QtCore.QTimer.singleShot(0, self._arrange_overlays)

    def eventFilter(self, target, event):
        if target is self.plot.interactor:
            if event.type() == QtCore.QEvent.MouseButtonPress and event.button() == QtCore.Qt.LeftButton:
                self._mouse_press = event.position()
                self._mouse_dragged = False
                self._hover_timer.stop()
            elif event.type() == QtCore.QEvent.MouseMove:
                self._mouse_position = event.position()
                if self._mouse_press is not None and (event.position() - self._mouse_press).manhattanLength() > 4:
                    self._mouse_dragged = True
                if not event.buttons():
                    self._hover_timer.start()
            elif event.type() == QtCore.QEvent.MouseButtonRelease and event.button() == QtCore.Qt.LeftButton:
                press, self._mouse_press = self._mouse_press, None
                if press is not None and not self._mouse_dragged and (event.position() - press).manhattanLength() <= 4:
                    self._select_at(event.position(), bool(event.modifiers() & QtCore.Qt.ControlModifier))
            elif event.type() == QtCore.QEvent.Leave:
                self._hover_timer.stop()
                self._mouse_position = self.hover_hit = None
                self.picker.clear_highlight("hover")
                self._update_measurements()
        # Mouse events continue to VTK so orbit/pan/zoom retain their controls.
        if event.type() == QtCore.QEvent.Resize:
            QtCore.QTimer.singleShot(0, self._arrange_overlays)
        if event.type() == QtCore.QEvent.DragEnter:
            self.dragEnterEvent(event)
            return event.isAccepted()
        if event.type() == QtCore.QEvent.Drop:
            self.dropEvent(event)
            return event.isAccepted()
        return super().eventFilter(target, event)

    def closeEvent(self, event):
        self._hover_timer.stop()
        self.picker.clear()
        self.loader.close()
        self.plot.close()
        event.accept()


def run(options):
    from .receipt import RequestReceipt, write_json
    receipt = RequestReceipt(options.file) if options.result else None
    application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
    application.setStyle("Fusion")
    application.styleHints().setColorScheme(QtCore.Qt.ColorScheme.Dark)
    settings = ImportSettings(options.profile, options.normalize_periodic_faces,
                              options.restrict_unused_support_domains, options.strict_source,
                              schema_interpretation=options.schema_interpretation,
                              carrier_interpretation=options.carrier_interpretation,
                              report_budget_mib=options.report_budget_mib,
                              preserve_opaque_file_name_text=options.preserve_opaque_file_name_text,
                              assume_ascii_invariant_data=options.assume_ascii_invariant_data,
                              unit_interpretation=options.unit_interpretation,
                              repair_source_sha256=getattr(options, 'repair_source_sha256', ''),
                              maximum_face_orientation_edits=getattr(options, 'maximum_face_orientation_edits', 0),
                              maximum_repair_proof_controls=getattr(options, 'maximum_repair_proof_controls', 0),
                              maximum_repair_proof_products=getattr(options, 'maximum_repair_proof_products', 0),
                              face_orientation_edits=tuple(getattr(options, 'face_orientation_edits', ())),
                              selection_source_sha256=getattr(options, 'selection_source_sha256', ''),
                              selected_definitions=tuple(getattr(options, 'selected_definitions', ())),
                              source_error_isolation=getattr(options, 'source_error_isolation', ''),
                              maximum_isolated_source_failures=getattr(options, 'maximum_isolated_source_failures', 0),
                              maximum_source_influence_visits=getattr(options, 'maximum_source_influence_visits', 0),
                              complex_mapping_interpretation=getattr(
                                  options, 'complex_mapping_interpretation', 'DeclaredOnly'),
                              band_repair_source_sha256=getattr(options, 'band_repair_source_sha256', ''),
                              maximum_band_repair_faces=getattr(options, 'maximum_band_repair_faces', 0),
                              maximum_band_bound_edits=getattr(options, 'maximum_band_bound_edits', 0),
                              maximum_band_proof_operations=getattr(options, 'maximum_band_proof_operations', 0),
                              cylindrical_band_repairs=tuple(getattr(options, 'cylindrical_band_repairs', ())),
                              maximum_band_circle_vertices=getattr(options, 'maximum_band_circle_vertices', 0),
                              maximum_band_vertex_move_mm=getattr(options, 'maximum_band_vertex_move_mm', ''),
                              maximum_band_total_vertex_move_mm=getattr(
                                  options, 'maximum_band_total_vertex_move_mm', ''),
                              band_circle_vertices=tuple(getattr(options, 'band_circle_vertices', ())),
                              assembly=getattr(options, 'assembly', False),
                              unreferenced_placement_policy=getattr(options, 'unreferenced_placement_policy', 'Reject'),
                              integration_relative_accuracy=getattr(options, 'integration_relative_accuracy', None))
    window = ViewerWindow(settings, edges=options.edges, wireframe=options.wireframe)
    result = [1 if options.quit_after_load else 0]
    cancelled = [False]

    def progress(update):
        if receipt:
            receipt.progress(update)
        if options.scan_child:
            print(f"@brep-view:{update.phase},{update.completed},{update.total}", flush=True)
    window.loader.progress.connect(progress)
    window.loader.cancelled.connect(lambda: cancelled.__setitem__(0, True))

    def finished(success):
        result[0] = 0 if success else 1
        def complete():
            outcome = "Rendered" if success else "Cancelled" if cancelled[0] else "LoadFailure"
            try:
                if success and receipt:
                    verify_model_frame(window.plot)
                if success and options.screenshot:
                    options.screenshot.parent.mkdir(parents=True, exist_ok=True)
                    # QWidget.grab cannot capture the native VTK child window.
                    # Read the rendered viewport; Qt HUD/widgets stay on screen.
                    window.plot.screenshot(str(options.screenshot), transparent_background=False)
            except Exception as error:
                result[0] = 1
                outcome = "RenderFailure"
                window.status.setText("Screenshot failed: " + str(error))
            finally:
                if receipt:
                    try:
                        write_json(options.result, receipt.finish(
                            outcome, window.loader.worker_result, window.status.text()))
                    except OSError as error:
                        result[0] = 1
                        window.status.setText("Result write failed: " + str(error))
                if options.quit_after_load:
                    window.close()
        QtCore.QTimer.singleShot(150, complete)
    window.load_complete.connect(finished)
    if options.scan_child:
        # Keep Qt's normal layout/VTK rendering path, but never show batch
        # windows on the desktop. OFF_SCREEN was selected before PyVista import.
        window.setAttribute(QtCore.Qt.WA_DontShowOnScreen, True)
    window.show()
    if options.file:
        QtCore.QTimer.singleShot(0, lambda: window.load(options.file))
    elif options.quit_after_load:
        window.close()
        return 2
    application.exec()
    if receipt and receipt.result is None:
        write_json(options.result, receipt.finish("Closed", window.loader.worker_result))
    return result[0]
