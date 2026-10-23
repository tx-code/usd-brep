// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"
class SmCurve;
class SmSurface;
namespace step_import::detail
{
class Execution;
struct ModelData;
struct CylindricalBandRepairTrial;
[[nodiscard]] Result<std::shared_ptr<CylindricalBandRepairTrial>> PrepareCylindricalBandRepair(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const std::optional<step_semantics::Part::Shape>& shape,
    const Options& options,
    Report& report,
    Execution& execution
);
bool NeutralizesCylindricalBandBounds(const std::shared_ptr<CylindricalBandRepairTrial>& trial, std::size_t face);
[[nodiscard]] Result<void> CaptureCylindricalBandDefinitions(
    const std::shared_ptr<CylindricalBandRepairTrial>& trial,
    std::shared_ptr<ModelData> model,
    Report& report,
    Execution& execution
);
[[nodiscard]] Result<void> CompleteCylindricalBandRepair(
    const std::shared_ptr<CylindricalBandRepairTrial>& trial,
    ModelData& model,
    Report& report,
    Execution& execution
);
[[nodiscard]] Result<void> RequireSameNativeCurveDefinition(const SmCurve& before, const SmCurve& after);
[[nodiscard]] Result<void> RequireSameNativeSurfaceDefinition(const SmSurface& before, const SmSurface& after);
} // namespace step_import::detail
