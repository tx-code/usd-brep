# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit source assumptions shared by CLI, worker transport and scan settings."""
import re

SCHEMA_POLICIES = ('DeclaredOnly', 'UnregisteredAp214As2010', 'Ap214NameOnlyAs2010', 'SourceBoundAp214As2010')
CARRIER_POLICIES = ('DeclaredOnly', 'Level1AsCorrectedEncoding', 'Level1AsCorrectedEncodingWithLowercaseHex',
                    'BlankLevelAsCorrectedEncoding')


UNIT_POLICIES = ('DeclaredOnly', 'VerifiedExplicitUnitDimensions')
COMPLEX_MAPPING_POLICIES = ('DeclaredOnly', 'RegisteredFacetNames')


def validate(schema, carrier, strict, file_name_text=False, ascii_invariant_data=False, unit='DeclaredOnly',
             complex_mapping='DeclaredOnly', source_identity=''):
    if (schema not in SCHEMA_POLICIES or carrier not in CARRIER_POLICIES or unit not in UNIT_POLICIES or
            complex_mapping not in COMPLEX_MAPPING_POLICIES):
        raise ValueError('UnknownSourceInterpretation')
    if strict and (schema != 'DeclaredOnly' or carrier != 'DeclaredOnly' or unit != 'DeclaredOnly' or
                   complex_mapping != 'DeclaredOnly'):
        raise ValueError('SourceInterpretationRequiresInteroperability')
    if type(ascii_invariant_data) is not bool:
        raise ValueError('InvalidAsciiInvariantDataPolicy')
    if strict and ascii_invariant_data:
        raise ValueError('AsciiInvariantDataRequiresInteroperability')
    if strict and file_name_text:
        raise ValueError('FileNameTextRequiresInteroperability')
    bound = schema == 'SourceBoundAp214As2010'
    if (not isinstance(source_identity, str) or
            (bound and not re.fullmatch('[0-9a-f]{64}', source_identity)) or (not bound and source_identity)):
        raise ValueError('SourceBoundSchemaRequiresExactOriginalIdentity')


def source_identity_arguments(settings):
    schema = getattr(settings, 'schema_interpretation', 'DeclaredOnly')
    identity = getattr(settings, 'schema_source_sha256', '')
    if schema == 'SourceBoundAp214As2010':
        if not isinstance(identity, str) or not re.fullmatch('[0-9a-f]{64}', identity):
            raise ValueError('SourceBoundSchemaRequiresExactOriginalIdentity')
        return ['--schema-source-sha256', identity]
    if identity:
        raise ValueError('UnexpectedSourceBoundSchemaIdentity')
    return []


def validate_source_identity_receipt(scene, settings):
    requested = getattr(settings, 'schema_interpretation', 'DeclaredOnly')
    identity = getattr(settings, 'schema_source_sha256', '')
    actual_request = scene.get('requested_schema_interpretation')
    applied = scene.get('applied_schema_interpretation')
    if requested != 'SourceBoundAp214As2010':
        if identity or actual_request == 'SourceBoundAp214As2010' or applied == 'SourceBoundAp214As2010':
            raise ValueError('UnexpectedSourceBoundSchemaReceipt')
        return
    # A policy label alone cannot bind a result to the original. The scene's
    # source hash comes from the core's immutable capture, not a second path read.
    # The core refuses this option for already registered inputs. A worker may
    # not silently turn the explicit interpretation into an ordinary success.
    if (actual_request != requested or scene.get('source_sha256') != identity or
            applied != requested or scene.get('native_import_status') not in
            ('AcceptedUnderInterpretation', 'AcceptedWithRepairs', 'AcceptedWithSourceIsolation')):
        raise ValueError('SourceBoundSchemaReceiptMismatch')


def complex_mapping_arguments(settings):
    value = getattr(settings, 'complex_mapping_interpretation', 'DeclaredOnly')
    if value not in COMPLEX_MAPPING_POLICIES:
        raise ValueError('UnknownSourceInterpretation')
    return [] if value == 'DeclaredOnly' else ['--complex-mapping-interpretation', value]


def validate_complex_mapping_receipt(scene, settings):
    expected = settings.complex_mapping_interpretation
    receipt = scene.get('complex_mapping_interpretation')
    if receipt is None and expected == 'DeclaredOnly':
        return
    if not isinstance(receipt, dict) or receipt.get('requested') != expected:
        raise ValueError('ComplexMappingRequestMismatch')
    count = receipt.get('records_interpreted')
    applied = receipt.get('applied')
    if (type(count) is not int or not 0 <= count <= 1000000 or applied not in COMPLEX_MAPPING_POLICIES or
            (applied == 'DeclaredOnly') != (count == 0) or (expected == 'DeclaredOnly' and count) or
            (count and scene.get('native_import_status') not in
             ('AcceptedUnderInterpretation', 'AcceptedWithRepairs', 'AcceptedWithSourceIsolation'))):
        raise ValueError('InvalidComplexMappingQualification')
