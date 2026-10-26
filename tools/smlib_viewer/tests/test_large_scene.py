# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""A complete repeated public box assembly must not outgrow its root index."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from tools.smlib_viewer.backend import native_worker
from tools.smlib_viewer.backend import ImportSettings, SceneLoader, scene_file
from tools.smlib_viewer.assembly import read_assembly
from tools.smlib_viewer.scene_manifest import expand_scene, MAX_METADATA_BYTES
from tools.smlib_viewer.native_report import read_report, use_report
from tools.smlib_viewer.tests import test_topology

FIXTURE = Path(__file__).resolve().parents[3] / 'source/STEP_SEMANTICS/tests/fixtures/assembly_repeated_box.step'


def repeated_boxes(count=256):
    source = FIXTURE.read_bytes()
    frames = ['#933', '#935']
    records = []
    for index in range(count - 2):
        base = 1000 + 10 * index
        records.extend([
            f"#{base}=NEXT_ASSEMBLY_USAGE_OCCURRENCE('public-{index}','public instance',$,#902,#7,$);",
            f"#{base+1}=PRODUCT_DEFINITION_SHAPE('',$,#{base});",
            f"#{base+2}=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#{base+3},#{base+1});",
            (f"#{base+3}=(REPRESENTATION_RELATIONSHIP('',$,#20,#920) "
             f"REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#{base+4}) SHAPE_REPRESENTATION_RELATIONSHIP());"),
            f"#{base+4}=ITEM_DEFINED_TRANSFORMATION('',$,#931,#{base+5});",
            f"#{base+5}=AXIS2_PLACEMENT_3D('',#{base+6},#936,#937);",
            f"#{base+6}=CARTESIAN_POINT('',({50*(index+1)}.,0.,0.));",
        ])
        frames.append(f'#{base+5}')
    original = b"#920=SHAPE_REPRESENTATION('parent',(#933,#935),#921);"
    assert source.count(original) == 1
    source = source.replace(original, f"#920=SHAPE_REPRESENTATION('parent',({','.join(frames)}),#921);".encode())
    return source.replace(b'ENDSEC;\nEND-ISO', ('\n'.join(records)+'\n').encode()+b'ENDSEC;\nEND-ISO')


@unittest.skipUnless(native_worker().is_file(), 'matching native worker required')
class LargeSceneWorkerTests(unittest.TestCase):
    def test_complete_large_assembly_uses_a_small_root_and_bounded_metadata(self):
        source = repeated_boxes()
        with tempfile.TemporaryDirectory(prefix='public-large-scene-') as name:
            root = Path(name)
            path, output = root/'public.step', root/'output'
            path.write_bytes(source)
            output.mkdir()
            run = subprocess.run([str(native_worker()), str(path), str(output), 'development', '0', '0', '0',
                                  'DeclaredOnly', 'DeclaredOnly', '16', '0', '0', 'DeclaredOnly', '--assembly', '1'],
                                 input=b'start\n', capture_output=True, timeout=90)
            scene = json.loads((output/'scene.json').read_text())
            self.assertEqual(0, run.returncode, scene.get('key'))
            self.assertEqual('brep-view-scene.v8', scene['format'])
            self.assertLessEqual((output/'scene.json').stat().st_size, 65536)
            self.assertEqual(source, path.read_bytes())
            self.assertEqual(hashlib.sha256(source).hexdigest(), scene['source_sha256'])
            metadata = (output/scene['metadata']).read_bytes()
            self.assertEqual(len(metadata), scene['metadata_bytes'])
            self.assertLessEqual(len(metadata), 32*1024*1024)
            manifest = json.loads(metadata)
            self.assertEqual(257, manifest['instance_count'])
            self.assertEqual(256, manifest['body_count'])
            self.assertEqual(257, len(manifest['instances']))
            self.assertEqual(256, len(manifest['body_scenes']))
            self.assertEqual('Ready', manifest['status'])
            expanded = expand_scene(output, scene, scene_file)
            mesh, boundary, topology = read_assembly(output, expanded, scene_file)
            self.assertEqual(256*6, len(topology.faces))
            self.assertEqual(256*12, len(topology.edges))
            self.assertEqual(256*8, len(topology.vertices))
            self.assertEqual(256, len(topology.bodies))
            self.assertEqual(set(range(256*6)), set(mesh.cell_data['TopoFace']))
            self.assertEqual(set(range(256*12)), set(boundary.cell_data['TopoEdge']))


def envelope(scene, size):
    return dict(format='brep-view-scene.v8', status=scene['status'], source_sha256=scene['source_sha256'],
                native_import_status=scene['native_import_status'], metadata='scene-metadata.json',
                metadata_format=scene['format'], metadata_bytes=size)


class SceneMetadataTests(unittest.TestCase):
    def test_transport_binds_complete_identity_status_format_and_size(self):
        document = dict(format='brep-view-scene.v7', status='Ready', source_sha256='a'*64,
                        native_import_status='Accepted', instances=[dict(index=0)], body_scenes=[])
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            path = root/'scene-metadata.json'
            raw = json.dumps(document).encode()
            path.write_bytes(raw)
            wrapper = envelope(document, len(raw))
            self.assertEqual(document, expand_scene(root, wrapper, scene_file))
            self.assertIs(document, expand_scene(root, document, scene_file))
            for key, value in (('source_sha256', 'b'*64), ('status', 'DisplayFailure'),
                               ('native_import_status', 'AcceptedUnderInterpretation'),
                               ('metadata_format', 'brep-view-scene.v6'), ('metadata_bytes', True),
                               ('metadata_bytes', len(raw)+1), ('metadata', '../escape.json')):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    expand_scene(root, dict(wrapper, **{key: value}), scene_file)
            with patch('pathlib.Path.open', side_effect=AssertionError('must bound before read')):
                with self.assertRaises(ValueError):
                    expand_scene(root, dict(wrapper, metadata_bytes=MAX_METADATA_BYTES+1), scene_file)

    def test_duplicate_nonfinite_recursive_and_missing_payloads_refuse(self):
        scene = dict(format='brep-view-scene.v6', status='Ready', source_sha256='a'*64, native_import_status='Accepted')
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":1e999}',
                        json.dumps(envelope(scene, 1)).encode()):
                (root/'scene-metadata.json').write_bytes(raw)
                with self.subTest(raw=raw), self.assertRaises(ValueError):
                    expand_scene(root, envelope(scene, len(raw)), scene_file)
            (root/'scene-metadata.json').unlink()
            with self.assertRaises(ValueError):
                expand_scene(root, envelope(scene, 10), scene_file)

    def test_owning_report_is_parsed_once_and_cannot_bypass_request_capacity(self):
        with tempfile.TemporaryDirectory() as name, tempfile.TemporaryDirectory() as other:
            root, other_root = Path(name), Path(other)
            data = dict(status='Accepted', assembly={'instances': []}, quality_id='public')
            raw = json.dumps(data).encode()
            (root/'import-report.json').write_bytes(raw)
            (other_root/'import-report.json').write_bytes(raw)
            loaded = read_report(root, scene_file, len(raw))
            with patch('pathlib.Path.open', side_effect=AssertionError('shared snapshot must not parse again')):
                self.assertIs(loaded.document, use_report(loaded, root, scene_file, len(raw)))
            for directory, budget in ((root, len(raw)-1), (other_root, len(raw)), (root, 1 << 40)):
                with self.subTest(budget=budget), self.assertRaises(ValueError):
                    use_report(loaded, directory, scene_file, budget)
            with self.assertRaises(ValueError):
                use_report(data, root, scene_file, len(raw))
            (root/'import-report.json').write_bytes(b'{"x":1,"x":2}')
            with self.assertRaisesRegex(ValueError, 'Duplicate'):
                read_report(root, scene_file, 100)


class LargeSceneAdoptionTests(unittest.TestCase):
    setUpClass = classmethod(test_topology.TopologyAdoptionTests.setUpClass.__func__)
    setUp = test_topology.TopologyAdoptionTests.setUp
    request = test_topology.TopologyAdoptionTests.request
    write_manifest = staticmethod(test_topology.TopologyAdoptionTests.write_manifest)

    def test_bad_external_metadata_preserves_previous_scene_before_mesh_read(self):
        generation, _, _ = self.request()
        self.loader._adopt(generation, 0)
        previous = self.loaded[0]
        for bad in ('missing', 'mismatch'):
            self.loader.generation += 1
            generation, directory, scene = self.request()
            document = dict(scene, format='brep-view-scene.v6', source_sha256='a'*64, native_import_status='Accepted')
            raw = json.dumps(document).encode()
            if bad == 'mismatch':
                (directory/'scene-metadata.json').write_bytes(raw)
            wrapper = envelope(document, len(raw))
            wrapper['source_sha256'] = 'b'*64
            self.write_manifest(directory, wrapper)
            with patch('pyvista.read', side_effect=AssertionError('cannot decode incomplete metadata')):
                self.loader._adopt(generation, 0)
            self.assertEqual(1, len(self.loaded))
            self.assertIs(previous, self.loaded[-1])
            self.assertIn('scene_adoption_error', self.loader.worker_result)
            self.assertNotIn('native_import_status', self.loader.worker_result)
        self.assertEqual(2, len(self.failed))
