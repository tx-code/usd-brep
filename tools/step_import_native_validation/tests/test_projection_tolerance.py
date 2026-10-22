# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Analytic split-circle controls; projection retries cannot enlarge import budgets."""
import math
import unittest

from tools.step_import_native_validation.tests import test_general


def split_circle_cylinder(first=0.2, second=1.1):
    """Radius 3, height 7; split both circular rims without changing the solid.

    The side still uses one seam at the first endpoint. All surfaces and curves
    are exact analytic definitions; native curve parameters need not be angles.
    This fixture is independent of private CAD inputs and importer output.
    """
    data = test_general.cylinder_source()
    data = data.replace(b"#130=EDGE_CURVE('',#120,#120,#110,.T.);", b"#130=EDGE_CURVE('',#120,#122,#110,.T.);")
    data = data.replace(b"#131=EDGE_CURVE('',#121,#121,#111,.T.);", b"#131=EDGE_CURVE('',#121,#123,#111,.T.);")
    data = data.replace(b"#160=EDGE_LOOP('',(#150));", b"#160=EDGE_LOOP('',(#150,#250));")
    data = data.replace(b"#161=EDGE_LOOP('',(#151));", b"#161=EDGE_LOOP('',(#151,#251));")
    data = data.replace(b"(#152,#153,#154,#155)", b"(#152,#252,#153,#254,#154,#155)")
    extra = b"""#124=CARTESIAN_POINT('',(0.,-3.,0.));
#125=CARTESIAN_POINT('',(0.,-3.,7.));
#122=VERTEX_POINT('',#124);
#123=VERTEX_POINT('',#125);
#134=EDGE_CURVE('',#122,#120,#110,.T.);
#135=EDGE_CURVE('',#123,#121,#111,.T.);
#250=ORIENTED_EDGE('',*,*,#134,.F.);
#251=ORIENTED_EDGE('',*,*,#135,.T.);
#252=ORIENTED_EDGE('',*,*,#134,.T.);
#254=ORIENTED_EDGE('',*,*,#135,.F.);
"""
    data = data.replace(b'ENDSEC;\nEND-ISO', extra + b'ENDSEC;\nEND-ISO')
    for identifier, angle, z in ((102, first, 0), (103, first, 7), (124, second, 0), (125, second, 7)):
        old = f"#{identifier}=CARTESIAN_POINT('',(0.,{'3.' if identifier < 124 else '-3.'},{z}.));".encode()
        new = f"#{identifier}=CARTESIAN_POINT('',({3*math.cos(angle):.17E},{3*math.sin(angle):.17E},{z}.));".encode()
        assert data.count(old) == 1
        data = data.replace(old, new)
    return data


class ProjectionToleranceTests(unittest.TestCase):
    invoke_bytes = test_general.GeneralTests.invoke_bytes

    def test_axis_aligned_partition_has_independent_cylinder_truth(self):
        source = split_circle_cylinder(math.pi/2, 3*math.pi/2)
        run, report = self.invoke_bytes(source)
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual((3, 5, 4), tuple(report['model'][key] for key in ['faces', 'edges', 'vertices']))
        self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)

    def test_retry_cannot_publish_an_excessive_work_tolerance(self):
        source = split_circle_cylinder()
        run, report = self.invoke_bytes(source)
        self.assertEqual(source, report['source_inspection']['source']['input']['raw'].encode('latin1'))
        self.assertEqual('None', report['repair'])
        quality = report['quality']
        rational = lambda value: int(value['num'])/int(value['den'])
        limit = min(rational(quality['maximum_work_tolerance_mm']),
                    rational(quality['initial_work_tolerance_mm']) + rational(quality['tolerance_growth_mm']))
        observed = [item for item in report['measurements'] if item['stage'] == 'Postchecked']
        self.assertTrue(observed)
        if report['status'] == 'Accepted':
            # A future projection fix may legitimately accept the exact solid;
            # it must still preserve this fixed budget and the analytic truth.
            self.assertEqual(0, run.returncode)
            self.assertTrue(all(0 < item['value_mm'] <= limit for item in observed))
            self.assertAlmostEqual(63*math.pi, report['model']['material_volume']['value_mm3'], delta=1e-3)
        else:
            self.assertEqual('PostcheckFailure', report['status'])
            self.assertEqual(['GeneralUnexpectedToleranceGrowth'], [d['key'] for d in report['diagnostics']])
            self.assertIsNone(report['model'])
            self.assertTrue(report['candidate_discarded'])
            self.assertEqual('EdgeWorkTolerance', observed[-1]['kind'])
            self.assertGreater(observed[-1]['value_mm'], limit)
