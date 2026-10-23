// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportResult.h"

namespace step_import::detail
{
struct SourceFrame
{
    Point origin;
    std::array<Point, 3> axes; // X/Y/Z columns, dimensionless, right handed.
};
// Shared existing numeric/range and build_axes rules. Input type/field binding
// is a prerequisite. Source lexemes remain immutable; scale applies only to
// physical point coordinates, never direction ratios.
// Numerical conversion refusals retain the old importer diagnostic and private
// provenance for strict-attempt observation and the assembly message adapter.
// Do not clamp out-of-range lexemes or pad a missing coordinate to obtain 3D.
[[nodiscard]] Result<double> ReadSourceNumber(const step_source::Value& value);
[[nodiscard]] Result<Point> ReadSourceTriple(const step_source::Value& value);
[[nodiscard]] Result<Point> ReadSourcePoint(const step_semantics::SourceBindings& source, std::size_t point, double lengthToMm);
[[nodiscard]] Result<Point> ReadSourceDirection(const step_semantics::SourceBindings& source, std::size_t direction);
[[nodiscard]] Result<SourceFrame> ReadSourceFrame(const step_semantics::SourceBindings& source, std::size_t placement, double lengthToMm);
} // namespace step_import::detail
