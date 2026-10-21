# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Explicit source assumptions shared by CLI, worker transport and scan settings."""
SCHEMA_POLICIES = ('DeclaredOnly', 'UnregisteredAp214As2010', 'Ap214NameOnlyAs2010')
CARRIER_POLICIES = ('DeclaredOnly', 'Level1AsCorrectedEncoding', 'Level1AsCorrectedEncodingWithLowercaseHex')


def validate(schema, carrier, strict):
    if schema not in SCHEMA_POLICIES or carrier not in CARRIER_POLICIES:
        raise ValueError('UnknownSourceInterpretation')
    if strict and (schema != 'DeclaredOnly' or carrier != 'DeclaredOnly'):
        raise ValueError('SourceInterpretationRequiresInteroperability')
