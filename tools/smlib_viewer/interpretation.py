# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit source assumptions shared by CLI, worker transport and scan settings."""
SCHEMA_POLICIES = ('DeclaredOnly', 'UnregisteredAp214As2010', 'Ap214NameOnlyAs2010')
CARRIER_POLICIES = ('DeclaredOnly', 'Level1AsCorrectedEncoding', 'Level1AsCorrectedEncodingWithLowercaseHex',
                    'BlankLevelAsCorrectedEncoding')


UNIT_POLICIES = ('DeclaredOnly', 'VerifiedExplicitUnitDimensions')


def validate(schema, carrier, strict, file_name_text=False, ascii_invariant_data=False, unit='DeclaredOnly'):
    if schema not in SCHEMA_POLICIES or carrier not in CARRIER_POLICIES or unit not in UNIT_POLICIES:
        raise ValueError('UnknownSourceInterpretation')
    if strict and (schema != 'DeclaredOnly' or carrier != 'DeclaredOnly' or unit != 'DeclaredOnly'):
        raise ValueError('SourceInterpretationRequiresInteroperability')
    if type(ascii_invariant_data) is not bool:
        raise ValueError('InvalidAsciiInvariantDataPolicy')
    if strict and ascii_invariant_data:
        raise ValueError('AsciiInvariantDataRequiresInteroperability')
    if strict and file_name_text:
        raise ValueError('FileNameTextRequiresInteroperability')
