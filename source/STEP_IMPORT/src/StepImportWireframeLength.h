// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <array>

class SmCurve;

namespace step_import::detail
{
// Private numerical query, shared with independent native controls. The caller
// keeps the actual curve/context alive and serializes access to native caches.
// Fixed evaluation/span/depth/deadline limits may refuse; the returned length
// is numerical quadrature evidence, not a continuous error certificate. This
// entrypoint neither establishes source qualification nor substitutes a chord.
[[nodiscard]] Result<double> QueryWireframeCurveLength(const SmCurve& curve, std::array<double, 2> interval, double absoluteAccuracyMm);
} // namespace step_import::detail
