// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepGeometryDependencies.h"

namespace step_semantics
{
// Necessary source declarations/dependencies only. No physical-unit conversion,
// source/native UV map, curve/surface agreement or native admission is established.
GeometryDependencyResult CheckSurfaceCurveDependencies(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    GeometryDependencyCounts& checks
);
// Reporting qualifier only: complete prerequisite phase plus an admitted new
// declaration; it does not infer source validity from mere schema recognition.
bool SurfaceCurvePrerequisitesComplete(const step_source::InspectOutcome& source, const Outcome& checks);
} // namespace step_semantics
