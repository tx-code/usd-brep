# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit source/use transport and complete native public-box evidence."""
import contextlib
import copy
from dataclasses import FrozenInstanceError
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from PySide6 import QtCore, QtWidgets

from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker, scene_file
from tools.smlib_viewer.cli import main
from tools.smlib_viewer.scan import run_scan
from tools.smlib_viewer.surface_curve import (FIELD, INTERPRETATION, QUALIFICATION_FLAGS,
                                            arguments, validate_receipt)
from tools.smlib_viewer.tests import test_topology
from tools.smlib_viewer.tests.test_scan import options as scan_options

USE = '7:21:203:920:202:PCURVE_S1:921'
OTHER_USE = '7:21:213:940:212:PCURVE_S1:941'
FIXTURE = test_topology.REPO / 'source/STEP_IMPORT/tests/fixtures/source_pcurve_box.step'


def settings(source=b'public original', **values):
    return ImportSettings(**(dict(surface_curve_3d_source_sha256=hashlib.sha256(source).hexdigest(),
                                 maximum_surface_curve_3d_uses=1, surface_curve_3d_uses=(USE,)) | values))


def documents(request):
    """Transport control only; the real-worker tests establish native truth."""
    plans = []
    for use in request.surface_curve_3d_uses:
        fields = use.split(':')
        plans.append(dict(zip(('source_definition', 'source_shape', 'source_edge', 'source_surface_curve',
                               'source_curve_3d', 'original_master'), fields[:6])) | dict(source_pcurves=fields[6:]))
    quality = 'Q-AP214-development-mm-01'
    receipt = dict(source_sha256=request.surface_curve_3d_source_sha256, interpretation=INTERPRETATION,
                   request_complete=True, quality_id=quality, maximum_uses=request.maximum_surface_curve_3d_uses,
                   plans=plans, actions=[dict(plan_index=index, native_edge_interval=[0, 1],
                                             native_checks_passed=True, original_conflicts=[])
                                         for index in reversed(range(len(plans)))],
                   consistency_budget_mm=[1, 1000], derived_uv_budget_mm=[1, 1000], maximum_work_tolerance_mm=[1, 1000])
    receipt.update({flag: False for flag in QUALIFICATION_FLAGS})
    marker = dict(source_sha256=receipt['source_sha256'], interpretation=INTERPRETATION, request_complete=True,
                  quality_id=quality, maximum_uses=receipt['maximum_uses'], plan_count=len(plans),
                  action_count=len(plans), report='import-report.json')
    report = dict(format_version='step-general-native-development.v1', status='AcceptedUnderInterpretation',
                  source_sha256=receipt['source_sha256'], candidate_discarded=False, quality_id=quality,
                  quality={key: dict(kind='rational', num='1', den='1000')
                           for key in ('consistency_mm', 'derived_uv_mm', 'maximum_work_tolerance_mm')})
    report[FIELD] = receipt
    scene = dict(native_import_status='AcceptedUnderInterpretation', source_sha256=receipt['source_sha256'])
    scene[FIELD] = marker
    return scene, report


class SurfaceCurveOptionsTests(unittest.TestCase):
    def test_default_and_explicit_immutable_source_use_plan(self):
        self.assertEqual([], arguments(ImportSettings()))
        uses = [USE]
        request = settings(surface_curve_3d_uses=uses)
        uses.clear()
        self.assertEqual((USE,), request.surface_curve_3d_uses)
        with self.assertRaises(FrozenInstanceError):
            request.surface_curve_3d_source_sha256 = '0' * 64
        self.assertEqual(['--surface-curve-3d-source-sha256', hashlib.sha256(b'public original').hexdigest(),
                          '--max-surface-curve-3d-uses', '1', '--surface-curve-3d-use', USE], arguments(request))
        # Schema identity cannot fill in or replace this independent request.
        independent = settings(schema_interpretation='SourceBoundAp214As2010', schema_source_sha256='b' * 64)
        self.assertNotEqual(independent.schema_source_sha256, independent.surface_curve_3d_source_sha256)
        self.assertEqual(arguments(request), arguments(independent))
        self.assertTrue(arguments(settings(strict_source=True)))

    def test_hash_scope_budget_and_exact_source_lists_are_required(self):
        for values in (dict(surface_curve_3d_source_sha256=''), dict(surface_curve_3d_source_sha256='A' * 64),
                       dict(surface_curve_3d_source_sha256='a' * 63), dict(maximum_surface_curve_3d_uses=True),
                       dict(maximum_surface_curve_3d_uses=0), dict(maximum_surface_curve_3d_uses=1000001),
                       dict(surface_curve_3d_uses=[]), dict(surface_curve_3d_uses=[USE, USE],
                                                           maximum_surface_curve_3d_uses=2),
                       dict(surface_curve_3d_uses=[USE.replace('PCURVE_S1', 'CURVE_3D')]),
                       dict(surface_curve_3d_uses=[USE.replace('PCURVE_S1', 'PCURVE_S2')]),
                       dict(surface_curve_3d_uses=[USE.replace('7:', '07:', 1)])):
            with self.subTest(values=values), self.assertRaises(ValueError):
                settings(**values)
        repeated = USE.replace('PCURVE_S1:921', 'PCURVE_S2:921:921')
        self.assertEqual(repeated, arguments(settings(surface_curve_3d_uses=(repeated,)))[-1])
        self.assertTrue(arguments(settings(surface_curve_3d_uses=(OTHER_USE, USE), maximum_surface_curve_3d_uses=2)))
        with self.assertRaises(ValueError):
            settings(surface_curve_3d_uses=(OTHER_USE, USE), maximum_surface_curve_3d_uses=1)

    def test_core_combination_restrictions_do_not_become_gui_fallbacks(self):
        base = dict(surface_curve_3d_source_sha256='a' * 64, maximum_surface_curve_3d_uses=1,
                    surface_curve_3d_uses=(USE,))
        for key, value in (('normalize_periodic', True), ('restrict_domains', True),
                           ('normalize_periodic_faces', True), ('restrict_unused_support_domains', True),
                           ('face_orientation_edits', ('plan',)), ('cylindrical_band_repairs', ('plan',)),
                           ('selected_definitions', ('7',)), ('source_error_isolation', 'FaceBoundsClause')):
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, 'PlanCombinationUnsupported'):
                arguments(SimpleNamespace(**base, **{key: value}))

    def test_cli_and_scan_api_refuse_non_file_scope_before_startup(self):
        flags = ['--surface-curve-3d-source-sha256', 'a' * 64, '--max-surface-curve-3d-uses', '1',
                 '--surface-curve-3d-use', USE]
        for args in (flags, ['public.stl', *flags], ['--scan', '.', *flags],
                     ['public.step', *flags, '--normalize-periodic-faces'],
                     ['public.step', '--surface-curve-3d-use', USE]):
            with self.subTest(args=args), contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                main(args)
        with tempfile.TemporaryDirectory() as name:
            output = Path(name) / 'not-created'
            with self.assertRaisesRegex(ValueError, 'SurfaceCurve3dRequiresOneExplicitStepFile'):
                run_scan(scan_options(scan=[Path(name)], output=output, surface_curve_3d_source_sha256='a' * 64,
                                      maximum_surface_curve_3d_uses=1, surface_curve_3d_uses=[USE]))
            self.assertFalse(output.exists())
        with patch('tools.smlib_viewer.app.run', return_value=0) as run:
            self.assertEqual(0, main(['public.step', *flags]))
        self.assertEqual(USE, arguments(run.call_args[0][0])[-1])


class SurfaceCurveReceiptTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(prefix='viewer-public-source3d-report-')
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name)
        self.request = settings()
        self.scene, self.report = documents(self.request)

    def read(self):
        (self.path / 'import-report.json').write_text(json.dumps(self.report), encoding='utf-8')
        return validate_receipt(self.path, self.scene, self.request, scene_file)

    def test_complete_owning_receipt_keeps_list_occurrences_and_action_correspondence(self):
        repeated = USE.replace('PCURVE_S1:921', 'PCURVE_S2:921:921')
        self.request = settings(surface_curve_3d_uses=(OTHER_USE, repeated), maximum_surface_curve_3d_uses=2)
        self.scene, self.report = documents(self.request)
        receipt = self.read()
        self.assertEqual(['921', '921'], receipt['plans'][1]['source_pcurves'])
        self.assertEqual([1, 0], [action['plan_index'] for action in receipt['actions']])
        (self.path / 'import-report.json').unlink()
        self.assertTrue(receipt['request_complete'])

    def test_wrong_marker_or_unrequested_receipt_is_rejected(self):
        original = copy.deepcopy(self.scene)
        for key, value in (('source_sha256', 'b' * 64), ('request_complete', False), ('maximum_uses', 2),
                           ('plan_count', True), ('action_count', 0), ('quality_id', 'another'),
                           ('report', '../import-report.json')):
            with self.subTest(key=key):
                self.scene = copy.deepcopy(original)
                self.scene[FIELD][key] = value
                with self.assertRaises(ValueError):
                    self.read()
        with self.assertRaisesRegex(ValueError, 'MissingSurfaceCurve3dReceipt'):
            validate_receipt(self.path, {}, self.request, scene_file)
        with self.assertRaisesRegex(ValueError, 'UnexpectedSurfaceCurve3dReceipt'):
            validate_receipt(self.path, original, ImportSettings(), scene_file)
        self.assertIsNone(validate_receipt(self.path, {}, ImportSettings(), scene_file))

    def test_core_flags_status_actions_and_budgets_cannot_be_upgraded(self):
        original = copy.deepcopy(self.report)
        mutations = [lambda r: r.update(status='Accepted'), lambda r: r.update(source_sha256='b' * 64),
                     lambda r: r.update(candidate_discarded=True), lambda r: r[FIELD].update(request_complete=False),
                     lambda r: r[FIELD]['plans'][0].update(source_curve_3d='999'),
                     lambda r: r[FIELD]['actions'][0].update(native_checks_passed=False),
                     lambda r: r[FIELD]['actions'][0].update(plan_index=True),
                     lambda r: r[FIELD]['actions'].clear(),
                     lambda r: r[FIELD]['actions'][0].update(native_edge_interval=[0, float('nan')]),
                     lambda r: r[FIELD].update(consistency_budget_mm=[1, 1]),
                     lambda r: r[FIELD]['actions'][0].update(original_conflicts=[dict(
                         source_pcurve='999', association_occurrence=1, status='Indeterminate', key='original',
                         record_id='921', stage='Native', message='original')])]
        mutations.extend(lambda r, flag=flag: r[FIELD].update({flag: True}) for flag in QUALIFICATION_FLAGS)
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                self.report = copy.deepcopy(original)
                mutate(self.report)
                with self.assertRaises(ValueError):
                    self.read()

    def test_duplicate_fields_and_bounded_read_are_enforced(self):
        path = self.path / 'import-report.json'
        path.write_text('{"status":"Accepted","status":"AcceptedUnderInterpretation"}', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'DuplicateSurfaceCurve3dReportField'):
            validate_receipt(self.path, self.scene, self.request, scene_file)
        with path.open('wb') as stream:
            stream.seek(32 * 1024 * 1024)
            stream.write(b' ')
        with self.assertRaisesRegex(ValueError, 'SurfaceCurve3dReportLimit'):
            validate_receipt(self.path, self.scene, self.request, scene_file)


class SurfaceCurveAdoptionTests(unittest.TestCase):
    setUpClass = classmethod(test_topology.TopologyAdoptionTests.setUpClass.__func__)
    setUp = test_topology.TopologyAdoptionTests.setUp
    request = test_topology.TopologyAdoptionTests.request
    write_manifest = staticmethod(test_topology.TopologyAdoptionTests.write_manifest)

    def test_bad_required_receipt_preserves_old_scene_before_mesh_decode(self):
        generation, _, _ = self.request()
        self.loader._adopt(generation, 0)
        previous = self.loaded[0]
        self.loader.settings = settings()
        for corrupt in ('missing', 'truncated', 'plan', 'action'):
            with self.subTest(corrupt=corrupt):
                self.loader.generation += 1
                generation, path, manifest = self.request()
                scene, report = documents(self.loader.settings)
                manifest.update(scene)
                if corrupt == 'missing':
                    del manifest[FIELD]
                elif corrupt == 'plan':
                    report[FIELD]['plans'][0]['source_edge'] = '999'
                elif corrupt == 'action':
                    report[FIELD]['actions'][0]['native_checks_passed'] = False
                self.write_manifest(path, manifest)
                (path / 'import-report.json').write_text('{}' if corrupt == 'truncated' else json.dumps(report),
                                                        encoding='utf-8')
                with patch('pyvista.read', side_effect=AssertionError('Reject before mesh decode')):
                    self.loader._adopt(generation, 0)
                self.assertEqual(1, len(self.loaded))
                self.assertIs(previous, self.loaded[0])
                self.assertFalse(path.exists())
        self.assertEqual(4, len(self.failed))

    def test_cancel_at_final_boundary_keeps_the_complete_previous_scene(self):
        generation, _, _ = self.request()
        self.loader._adopt(generation, 0)
        previous = self.loaded[0]
        self.loader.settings = settings()
        self.loader.generation += 1
        generation, path, manifest = self.request()
        scene, report = documents(self.loader.settings)
        manifest.update(scene)
        self.write_manifest(path, manifest)
        (path / 'import-report.json').write_text(json.dumps(report), encoding='utf-8')

        def cancel(update):
            if update.phase == 'render':
                self.loader.cancel(notify=False)

        self.loader.progress.connect(cancel)
        self.loader._adopt(generation, 0)
        self.assertIs(previous, self.loaded[-1])
        self.assertEqual(1, len(self.loaded))
        self.assertFalse(self.failed)
        self.assertFalse(path.exists())

    def test_old_worker_rejection_has_no_implicit_fallback(self):
        self.loader.settings = settings()
        generation, path, _ = self.request()
        (path / 'scene.json').unlink()
        self.loader._adopt(generation, 2)
        self.assertFalse(self.loaded)
        self.assertIn('SurfaceCurve3dWorkerRequestUnsupportedOrInvalid', self.failed[0][1])
        self.assertFalse(path.exists())

    def test_windows_transport_limit_refuses_the_whole_plan_before_start(self):
        uses = tuple(f'7:21:{index}:920:202:PCURVE_S1:921' for index in range(1000, 2000))
        self.loader.settings = settings(surface_curve_3d_uses=uses, maximum_surface_curve_3d_uses=len(uses))
        with (patch('tools.smlib_viewer.backend.native_worker', return_value=Path(sys.executable)),
              patch('tools.smlib_viewer.backend.sys.platform', 'win32')):
            self.loader.load(str(test_topology.BOX))
        self.assertFalse(self.loaded)
        self.assertFalse(self.loader.jobs)
        self.assertIn('SurfaceCurve3dTransportCommandLimit', self.failed[-1][1])

    def test_unpublished_failure_keeps_conflicts_and_secondary_error_cannot_mask_primary(self):
        self.loader.settings = settings()
        for corrupt in (False, True):
            with self.subTest(corrupt=corrupt):
                self.loader.generation += 1
                generation, path, manifest = self.request()
                scene, report = documents(self.loader.settings)
                scene['native_import_status'] = report['status'] = 'PostcheckFailure'
                scene[FIELD]['request_complete'] = report[FIELD]['request_complete'] = False
                report[FIELD]['actions'][0]['native_checks_passed'] = False
                conflict = dict(source_pcurve='921', association_occurrence=1, status='Indeterminate',
                                key='OriginalPcurveConflict', record_id='921', stage='Native',
                                message='original refusal')
                report[FIELD]['actions'][0]['original_conflicts'] = [conflict]
                manifest.update(scene, status='PostcheckFailure',
                                diagnostics=[dict(key='PrimaryFailure', message='fatal')])
                self.write_manifest(path, manifest)
                (path / 'import-report.json').write_text('{}' if corrupt else json.dumps(report),
                                                        encoding='utf-8')
                self.loader._adopt(generation, 1)
                self.assertFalse(self.loaded)
                self.assertIn('PrimaryFailure: fatal', self.failed[-1][1])
                if corrupt:
                    self.assertIn('surface_curve_3d_report_error', self.loader.worker_result)
                else:
                    observation = self.loader.worker_result['surface_curve_3d_failure_observation']
                    self.assertEqual('Unpublished', observation['publication'])
                    self.assertFalse(observation['receipt']['request_complete'])
                    self.assertEqual([conflict], observation['receipt']['actions'][0]['original_conflicts'])
                self.assertFalse(path.exists())


@unittest.skipUnless(native_worker().is_file(), 'Requires matching native display worker')
class SurfaceCurveWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings())
        self.addCleanup(self.close_loader)

    def close_loader(self):
        self.loader.close()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def public_case(self, source, uses=(USE,), conflict=False):
        self.loader.settings = settings(source, surface_curve_3d_uses=uses, maximum_surface_curve_3d_uses=len(uses))
        with tempfile.TemporaryDirectory(prefix='viewer-public-source3d-') as name:
            path = Path(name) / 'public.step'
            path.write_bytes(source)
            _, mesh, boundary, metadata = self.load(path)
            receipt = metadata['_surface_curve_3d_receipt']
            self.assertEqual('AcceptedUnderInterpretation', metadata['native_import_status'])
            self.assertTrue(receipt['request_complete'])
            self.assertEqual(len(uses), len(receipt['actions']))
            self.assertEqual([use.split(':')[2] for use in uses], [plan['source_edge'] for plan in receipt['plans']])
            self.assertEqual(conflict, any(action['original_conflicts'] for action in receipt['actions']))
            self.assertTrue(all(receipt[flag] is False for flag in QUALIFICATION_FLAGS))
            self.assertEqual(source, path.read_bytes())
            self.assertEqual(hashlib.sha256(source).hexdigest(), metadata['source_sha256'])
            self.assertEqual((6, 12, 8), tuple(metadata[key] for key in ('faces', 'edges', 'vertices')))
            self.assertEqual(set(range(6)), set(mesh.cell_data['TopoFace']))
            self.assertEqual(set(range(12)), set(boundary.cell_data['TopoEdge']))
            topology = metadata['_topology']
            self.assertAlmostEqual(2200, sum(face.area_mm2 for face in topology.faces), delta=.22)
            test_topology.np.testing.assert_allclose(sorted(edge.length_mm for edge in topology.edges),
                                                    [10] * 4 + [20] * 4 + [30] * 4, atol=1e-5, rtol=0)
            from tools.smlib_viewer.app import ViewerWindow
            window = ViewerWindow(self.loader.settings)
            try:
                window._loaded(str(path), mesh, boundary, metadata)
                self.assertEqual(1, window.hud.text().count('条件接受'))
                self.assertIn('原件限定三维优先', window.hud.text())
                self.assertIn('原 pcurve 一致性未获资格', window.hud.text())
            finally:
                window.close()
            return receipt

    def test_real_source_bound_box_and_original_conflict_have_complete_receipts(self):
        source = FIXTURE.read_bytes()
        self.public_case(source)
        for before, after in ((b'(0.,0.)', b'(0.1,0.)'), (b'(0.,2.)', b'(0.1,2.)'),
                              (b'(0.,10.)', b'(0.1,10.)')):
            self.assertIn(before, source)
            source = source.replace(before, after, 1)
        self.public_case(source, conflict=True)

    def test_repeated_original_association_is_not_deduplicated(self):
        source = FIXTURE.read_bytes().replace(b'(#921),.PCURVE_S1.', b'(#921,#921),.PCURVE_S2.')
        receipt = self.public_case(source, (USE.replace('PCURVE_S1:921', 'PCURVE_S2:921:921'),))
        self.assertEqual(['921', '921'], receipt['plans'][0]['source_pcurves'])

    def test_complete_two_use_plan_keeps_request_order_and_actual_action_mapping(self):
        source = FIXTURE.read_bytes()
        before = b"#213=EDGE_CURVE('',#111,#112,#212,.T.);"
        self.assertIn(before, source)
        source = source.replace(before, b"#213=EDGE_CURVE('',#111,#112,#940,.T.);")
        extra = (b"#940=SURFACE_CURVE('',#212,(#941),.PCURVE_S1.);\n"
                 b"#941=PCURVE('',#504,#942);\n#942=DEFINITIONAL_REPRESENTATION('',(#943),#930);\n"
                 b"#943=B_SPLINE_CURVE_WITH_KNOTS('',1,(#944,#945),.UNSPECIFIED.,.F.,.F.,"
                 b"(2,2),(2.,5.),.UNSPECIFIED.);\n"
                 b"#944=CARTESIAN_POINT('',(0.,10.));\n#945=CARTESIAN_POINT('',(20.,10.));\n")
        at = source.rindex(b'ENDSEC;')
        source = source[:at] + extra + source[at:]
        receipt = self.public_case(source, (OTHER_USE, USE))
        self.assertEqual({0, 1}, {action['plan_index'] for action in receipt['actions']})

    def test_wrong_original_and_duplicate_worker_arguments_do_not_publish(self):
        source = FIXTURE.read_bytes()
        for identity, duplicate in ((hashlib.sha256(b'another public original').hexdigest(), False),
                                    (hashlib.sha256(source).hexdigest(), True)):
            with self.subTest(duplicate=duplicate), tempfile.TemporaryDirectory() as name:
                root = Path(name)
                path, output = root / 'public.step', root / 'output'
                path.write_bytes(source)
                output.mkdir()
                flags = ['--surface-curve-3d-source-sha256', identity, '--max-surface-curve-3d-uses', '1',
                         '--surface-curve-3d-use', USE]
                if duplicate:
                    flags.extend(['--max-surface-curve-3d-uses', '1'])
                run = subprocess.run([str(native_worker()), str(path), str(output), 'development', '0', '0', '0',
                                      'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly', *flags],
                                     input=b'start\n', capture_output=True, timeout=45)
                self.assertNotEqual(0, run.returncode)
                if duplicate:
                    self.assertEqual(2, run.returncode)
                    self.assertFalse((output / 'scene.json').exists())
                else:
                    scene = json.loads((output / 'scene.json').read_text(encoding='utf-8'))
                    self.assertNotEqual('Ready', scene['status'])
                    self.assertIn('SurfaceCurve3dSourceMismatch', [entry['key'] for entry in scene['diagnostics']])
                self.assertFalse(list(output.glob('mesh.vtk')))
                self.assertEqual(source, path.read_bytes())
