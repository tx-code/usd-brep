// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportPcurveLineLift.h"
#include "StepImportPcurveLineWindow.h"

#include <vector>
class SmCrvOnSurf;
namespace step_import::detail
{
struct PcurveLineBinding
{
    std::size_t sourceRecord = 0;
    std::shared_ptr<const PcurveLineWindow> window;
    std::shared_ptr<const PcurveLineLiftWitness> witness;
    const SmSurface* surface = nullptr;
    const SmCurve* uv = nullptr;
    const SmCrvOnSurf* carrier = nullptr;
    ParameterMapping mapping;
    std::size_t reportMapping = 0;
};
// Only defining carriers enter this session. Auxiliary comparison curves are
// temporary and must never leave borrowed pointers in publication evidence.
struct PcurveLineSession
{
    std::vector<PcurveLineBinding> bindings;
};
[[nodiscard]] Result<void> VerifyPcurveLineBindings(
    const PcurveLineSession&,
    const SmContext&,
    const std::function<SplineChartCallbacks(std::size_t)>&
);
} // namespace step_import::detail
