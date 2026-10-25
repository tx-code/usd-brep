// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportResult.h"

#include <memory>
class SmCurve;
namespace step_import::detail::construction
{
class GeometryConstruction;
struct PcurveDefinition
{
    std::unique_ptr<SmCurve> curve;
    Bounds conservativeBoundsMm;
};
// Construct exact S(C(t)) from a qualified source PCURVE. Source UV coordinates
// follow the basis surface's parameter units, not the 2D context's length unit.
[[nodiscard]] Result<PcurveDefinition> MakeSourcePcurve(
    const GeometryConstruction& inputs,
    std::size_t pcurve,
    std::size_t use,
    bool definingMaster = false
);
} // namespace step_import::detail::construction
