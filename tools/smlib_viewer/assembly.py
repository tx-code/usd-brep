# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit assembly request and complete instance display transport."""
import json
import re

POLICIES = ('Reject', 'RetainDisconnected')
ACCEPTED = {'Accepted', 'AcceptedUnderInterpretation'}


def arguments(settings):
    enabled = getattr(settings, 'assembly', False)
    policy = getattr(settings, 'unreferenced_placement_policy', 'Reject')
    if type(enabled) is not bool or policy not in POLICIES:
        raise ValueError('InvalidAssemblyRequest')
    if not enabled:
        if policy != 'Reject':
            raise ValueError('PlacementRetentionRequiresAssembly')
        return []
    if (settings.face_orientation_edits or settings.selected_definitions or settings.cylindrical_band_repairs
            or settings.source_error_isolation):
        raise ValueError('AssemblyPlanCombinationUnsupported')
    return ['--assembly', '1', '--unreferenced-placement-policy', policy]


def validate_request(manifest, settings):
    requested = getattr(settings, 'assembly', False)
    if requested != (manifest.get('format') == 'brep-view-scene.v6'):
        raise ValueError('AssemblySceneScopeMismatch')
    if requested and manifest.get('requested_unreferenced_placement_policy') != settings.unreferenced_placement_policy:
        raise ValueError('AssemblySourcePolicyMismatch')


def read_assembly(directory, manifest, resolve):
    """Read every transformed display copy under one independently placed root.

    Definition geometry remains local in the core. The worker transforms private
    copies before tessellation and native measurements. Never weld coincident
    instances or infer source identities from mesh point order.
    """
    from .collection import read_collection
    from .topology import _object, _invalid_constant
    nodes = manifest.get('instances')
    definitions = manifest.get('definition_count')
    if (manifest.get('format') != 'brep-view-scene.v6' or manifest.get('role') != 'Assembly'
            or manifest.get('coordinate_scope') != 'AssemblyRoot' or type(manifest.get('root_count')) is not int
            or manifest['root_count'] != 1
            or manifest.get('native_import_status') not in ACCEPTED or manifest.get('auxiliary_required') is not False
            or type(definitions) is not int or not 0 < definitions <= 100000
            or type(manifest.get('instance_count')) is not int or not isinstance(nodes, list)
            or not 0 < len(nodes) <= 10000 or len(nodes) != manifest['instance_count']):
        raise ValueError('InvalidAssemblyManifest')
    if (not isinstance(manifest.get('source_sha256'), str)
            or not re.fullmatch('[0-9a-f]{64}', manifest['source_sha256'])):
        raise ValueError('InvalidAssemblySourceIdentity')
    report_path = resolve(directory, 'import-report.json')
    # Explicit display-evidence capacity, independent of core import acceptance.
    if report_path.stat().st_size > 64 * 1024 * 1024:
        raise ValueError('AssemblyDisplayReportLimit')
    report = json.loads(report_path.read_text(encoding='utf-8'), object_pairs_hook=_object,
                        parse_constant=_invalid_constant)
    core = report.get('assembly')
    if (report.get('format_version') != 'step-assembly-development.v1'
            or report.get('status') != manifest['native_import_status']
            or report.get('source_sha256') != manifest.get('source_sha256') or not isinstance(core, dict)
            or not isinstance(core.get('roots'), list) or len(core['roots']) != 1
            or not isinstance(core.get('definitions'), list) or len(core['definitions']) != definitions
            or not isinstance(core.get('instances'), list) or len(core['instances']) != len(nodes)
            or report.get('candidate_discarded') is not False
            or not isinstance(report.get('product_document'), dict)
            or report.get('product_document', {}).get('complete') is not True):
        raise ValueError('AssemblyCoreReceiptMismatch')
    checks = report['product_document'].get('definitions', [])
    if (not isinstance(checks, list) or len(checks) != definitions
            or any(not isinstance(d, dict) or d.get('checks_passed') is not True for d in checks)):
        raise ValueError('AssemblyDefinitionChecksMissing')
    retained = core.get('retained_unreferenced_placements')
    records = retained.get('records', []) if isinstance(retained, dict) else []
    if (type(manifest.get('retained_source_record_count')) is not int or not isinstance(records, list)
            or manifest['retained_source_record_count'] != len(records)
            or (records and manifest.get('requested_unreferenced_placement_policy') != 'RetainDisconnected')):
        raise ValueError('AssemblyRetentionReceiptMismatch')
    definition_ids = set()
    for definition in core['definitions']:
        if not isinstance(definition, dict):
            raise ValueError('InvalidAssemblyDefinitionIdentity')
        identity, shapes = definition.get('source_definition_id'), definition.get('source_shapes')
        if (not isinstance(identity, str) or not re.fullmatch('0|[1-9][0-9]*', identity)
                or identity in definition_ids or not isinstance(shapes, list)
                or any(not isinstance(s, str) or not re.fullmatch('0|[1-9][0-9]*', s) for s in shapes)
                or len(set(shapes)) != len(shapes)):
            raise ValueError('InvalidAssemblyDefinitionIdentity')
        definition_ids.add(identity)
        role = definition.get('role')
        if (role not in {'MaterialSolid', 'MaterialSolidCollection', 'ClosedSurfaceModel',
                         'OpenSurfaceModel', 'AssemblyPlacementWrapper'}
                or (role == 'AssemblyPlacementWrapper') != (not shapes)):
            raise ValueError('InvalidAssemblyDefinitionRole')
    roots = 0
    expected = set()
    for index, (node, origin) in enumerate(zip(nodes, core['instances'])):
        if not isinstance(node, dict) or not isinstance(origin, dict):
            raise ValueError('InvalidAssemblyInstance')
        definition = node.get('definition')
        parent = node.get('parent')
        if (type(node.get('index')) is not int or node['index'] != index or type(definition) is not int
                or not 0 <= definition < definitions
                or node.get('source_definition_id') != origin.get('source_definition_id')
                or node.get('usage') != origin.get('source_usage') or parent != origin.get('parent_instance')):
            raise ValueError('InvalidAssemblyInstance')
        if type(origin.get('root_index')) is not int or origin['root_index'] != 0:
            raise ValueError('InvalidAssemblyInstanceRoot')
        if type(origin.get('index')) is not int or origin['index'] != index:
            raise ValueError('InvalidAssemblyInstanceIdentity')
        source = core['definitions'][definition]
        if (source.get('source_definition_id') != node['source_definition_id']
                or source.get('role') != node.get('role')
                or checks[definition].get('source_definition_id') != node['source_definition_id']
                or type(node.get('body_count')) is not int
                or node['body_count'] != len(source.get('source_shapes', []))):
            raise ValueError('AssemblyDefinitionMismatch')
        if parent is None:
            roots += 1
            if index != 0 or node.get('usage') is not None or core['roots'] != [node['source_definition_id']]:
                raise ValueError('InvalidAssemblyRoot')
        elif (type(parent) is not int or not 0 <= parent < index or not isinstance(node.get('usage'), str)
              or not re.fullmatch('0|[1-9][0-9]*', node['usage'])):
            raise ValueError('InvalidAssemblyParent')
        for body in range(node['body_count']):
            expected.add((index, body))
    if roots != 1 or len({n['definition'] for n in nodes}) != definitions:
        raise ValueError('IncompleteAssemblyInstances')
    bodies = manifest.get('body_scenes')
    if not isinstance(bodies, list) or len(bodies) != len(expected):
        raise ValueError('IncompleteAssemblyBodies')
    for body in bodies:
        if not isinstance(body, dict):
            raise ValueError('InvalidAssemblyBodyOwner')
        instance, local = body.get('instance'), body.get('definition_body')
        if type(instance) is not int or type(local) is not int or (instance, local) not in expected:
            raise ValueError('InvalidAssemblyBodyOwner')
        expected.remove((instance, local))
        node = nodes[instance]
        source = core['definitions'][node['definition']]
        if (type(body.get('definition')) is not int or body['definition'] != node['definition']
                or body.get('source_definition_id') != node['source_definition_id']
                or body.get('source_shape_id') != source['source_shapes'][local]
                or body.get('units') != 'mm'
                or body.get('role') not in {'MaterialSolid', 'ClosedSurfaceModel', 'OpenSurfaceModel'}):
            raise ValueError('AssemblyBodySourceMismatch')
    if expected:
        raise ValueError('IncompleteAssemblyBodies')
    return read_collection(directory, manifest, resolve, _allow_single=True, _assembly=True)
