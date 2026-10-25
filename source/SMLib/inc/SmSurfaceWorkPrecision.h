// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <SmTypes.h>

class SmSurface;

namespace SmSurfaceWorkPrecision
{
// System attribute registration: 314 is reserved for this policy. Its COPY
// generic payload is version 1 plus four unsigned 16-bit words of binary64.
// Integer payloads retain the exact scalar in both native archive formats.
inline constexpr ULONG AttributeId = 314;

// Explicit numerical resolution in model millimetres, only for an exact
// SmSurfOfRevolution which is standalone, really owned by its face, or a
// derived copy owned by an exact SmCrvOnSurf (flags 2/3, reciprocal support
// and owned UV, all contexts matching). Borrowed/fake backlinks refuse. Zero
// clears the policy. Positive finite raw values are retained exactly. This
// selects numerical work resolution only: SmTol keeps its original geometric
// coincidence/approximation zones. No geometry, topology tolerance, global or
// context setting is changed.
// Native face/standalone-surface archives retain it. The legacy COS inline
// geometry archive has no nested attribute table and explicitly refuses it.
// Malformed existing metadata/ownership refuses; actual allocation exceptions
// propagate. Failed edits are not promised to roll back application changes.
SM_EXPORT SmStatus Set(SmSurface& surface, double value);

// Zero means absent, including the legacy path on other surface classes.
// A present malformed, duplicated or shared attribute returns SM_ERR and zero.
// This is a local, nonrecursive query: it never calls SmTol or a validator.
SM_EXPORT SmStatus Get(const SmSurface& surface, double& value);

// Numerical work resolution in mm: min(the legacy surface zone, the raw
// request). Without policy, return that legacy zone. Invalid policy returns
// SM_ERR and zero; it never selects a fallback precision or changes SmTol.
// Numerical callers must opt in explicitly; this is no physical error budget.
SM_EXPORT SmStatus GetEffective(const SmSurface& surface, double& value);
} // namespace SmSurfaceWorkPrecision
