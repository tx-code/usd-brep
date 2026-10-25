// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportSplineChart.h"

#include <map>
#include <vector>

class SmCrvOnSurf;
class SmFace;

namespace step_import::detail
{
struct SplineChartPlanEntry
{
    BinarySplineChart chart;
    std::shared_ptr<const SplineChartWitness> witness;
    std::string triggerPcurve;
};
struct SplineChartBinding
{
    std::size_t sourceRecord = 0;
    std::shared_ptr<const SplineChartWitness> witness;
    const SmSurface* surface = nullptr;
    const SmCurve* uv = nullptr;
    const SmCrvOnSurf* carrier = nullptr;
    const SmFace* face = nullptr;
    ParameterMapping mapping;
    std::size_t reportMapping = 0;
};
// One definition-local construction session. A source surface selects at most
// one chart; independent carriers may keep their original valid charts. All
// target-face trims are projected in world geometry, never copied under an
// assumed carrier/face UV identity. Freeze a value copy into the final snapshot.
struct SplineChartSession
{
    std::map<std::size_t, SplineChartPlanEntry> plans;
    std::vector<SplineChartBinding> bindings;
};
ParameterMapping SplineChartMapping(const BinarySplineChart& chart, std::string sourceSurface, std::string sourceUse, bool carrier);
[[nodiscard]] Result<void> VerifySplineChartBindings(
    const SplineChartSession& session,
    const SmContext& context,
    const std::function<SplineChartCallbacks(std::size_t)>& callbacks
);
} // namespace step_import::detail
