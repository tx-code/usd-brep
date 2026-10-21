# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit assumptions survive native import, rendering transport and scan receipts."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from PySide6 import QtCore, QtWidgets
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, native_worker
from tools.smlib_viewer.cli import main
from tools.smlib_viewer.receipt import RequestReceipt
from tools.smlib_viewer.tests import test_topology
from tools.smlib_viewer.tests.test_scan import options
from tools.smlib_viewer.scan import run_scan


def interpreted_box():
    source = test_topology.BOX.read_bytes()
    return source.replace(
        b'AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}',
        b'AUTOMOTIVE_DESIGN').replace(
        b"'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011",
        b"'automotive_design',2010").replace(b"'2;1'", b"'1'")


class InterpretationOptionsTests(unittest.TestCase):
    def test_strict_cannot_inherit_an_interpretation_and_enums_are_closed(self):
        for args in [dict(strict_source=True, schema_interpretation='Ap214NameOnlyAs2010'),
                     dict(strict_source=True, carrier_interpretation='Level1AsCorrectedEncoding'),
                     dict(schema_interpretation='Guess')]:
            with self.assertRaises(ValueError):
                ImportSettings(**args)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            main(['--strict-source', '--schema-interpretation', 'Ap214NameOnlyAs2010'])
        self.assertEqual(2, error.exception.code)

    def test_receipt_retains_exact_native_qualification(self):
        result = RequestReceipt('public.step').finish('Rendered', {
            'status': 'Ready', 'native_import_status': 'AcceptedUnderInterpretation'})
        self.assertEqual('AcceptedUnderInterpretation', result['native_import_status'])
        self.assertEqual('ViewerPipelineOnly', result['qualification'])
        failed_display = RequestReceipt('public.step').finish('RenderFailure', {
            'status': 'DisplayFailure', 'native_import_status': 'AcceptedUnderInterpretation'})
        self.assertEqual({'import': True, 'mesh': False, 'render': False}, failed_display['stages'])


@unittest.skipUnless(native_worker().is_file(), 'Requires native display worker')
class InterpretationWorkerTests(unittest.TestCase):
    load = test_topology.NativeTopologyTests.load

    @classmethod
    def setUpClass(cls):
        cls.application = QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
        cls.application.setQuitOnLastWindowClosed(False)

    def setUp(self):
        self.loader = SceneLoader(ImportSettings(schema_interpretation='Ap214NameOnlyAs2010',
                                                carrier_interpretation='Level1AsCorrectedEncodingWithLowercaseHex'))

    def tearDown(self):
        self.loader.close()
        QtCore.QCoreApplication.sendPostedEvents(None, QtCore.QEvent.DeferredDelete)

    def test_explicit_worker_preserves_interpretation_and_native_topology(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'public.step'
            source = interpreted_box()
            path.write_bytes(source)
            _, mesh, boundary, metadata = self.load(path)
            self.assertEqual('AcceptedUnderInterpretation', metadata['native_import_status'])
            self.assertEqual('Ap214NameOnlyAs2010', metadata['requested_schema_interpretation'])
            self.assertEqual('Level1AsCorrectedEncodingWithLowercaseHex', metadata['requested_carrier_interpretation'])
            self.assertEqual((6, 12, 8), tuple(metadata[key] for key in ['faces', 'edges', 'vertices']))
            self.assertEqual(source, path.read_bytes())
            from tools.smlib_viewer.app import ViewerWindow
            window = ViewerWindow(self.loader.settings)
            try:
                window._loaded(str(path), mesh, boundary, metadata)
                self.assertIn('源解释模式', window.hud.text())
            finally:
                window.close()

    def test_scan_default_refuses_and_explicit_choice_is_recorded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            inputs = root/'inputs'
            inputs.mkdir()
            (inputs/'public.step').write_bytes(interpreted_box())
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(1, run_scan(options(scan=[inputs], output=root/'default')))
                self.assertEqual(0, run_scan(options(scan=[inputs], output=root/'explicit',
                    schema_interpretation='Ap214NameOnlyAs2010',
                    carrier_interpretation='Level1AsCorrectedEncodingWithLowercaseHex')))
            report = json.loads((root/'explicit/scan.json').read_bytes())
            self.assertEqual('Ap214NameOnlyAs2010', report['settings']['schema_interpretation'])
            receipts = list((root/'explicit').rglob('result.json'))
            self.assertEqual(1, len(receipts))
            self.assertEqual('AcceptedUnderInterpretation', json.loads(receipts[0].read_bytes())['native_import_status'])
