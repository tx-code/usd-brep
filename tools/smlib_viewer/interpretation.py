# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit source assumptions shared by CLI, worker transport and scan settings."""
SCHEMA_POLICIES = ('DeclaredOnly', 'UnregisteredAp214As2010', 'Ap214NameOnlyAs2010')
CARRIER_POLICIES = ('DeclaredOnly', 'Level1AsCorrectedEncoding', 'Level1AsCorrectedEncodingWithLowercaseHex',
                    'BlankLevelAsCorrectedEncoding')


UNIT_POLICIES = ('DeclaredOnly', 'VerifiedExplicitUnitDimensions')
COMPLEX_MAPPING_POLICIES = ('DeclaredOnly', 'RegisteredFacetNames')


def validate(schema, carrier, strict, file_name_text=False, ascii_invariant_data=False, unit='DeclaredOnly',
             complex_mapping='DeclaredOnly'):
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
             ('AcceptedUnderInterpretation', 'AcceptedWithRepairs'))):
        raise ValueError('InvalidComplexMappingQualification')
