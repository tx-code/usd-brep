// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSourceFrame.h"

#include "StepImportVectorMath.h"

#include <charconv>
#include <cmath>
#include <stdexcept>

namespace step_import::detail
{
double ReadSourceNumber(const step_source::Value& value)
{
    std::string_view text = value.text;
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
    }
    double result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(result))
    {
        throw std::domain_error("GeneralNumericRangeUnestablished");
    }
    return result;
}
Point ReadSourceTriple(const step_source::Value& value)
{
    if (value.children.size() != 3)
    {
        throw std::domain_error("GeneralRequires3DGeometry");
    }
    return { ReadSourceNumber(value.children[0]), ReadSourceNumber(value.children[1]), ReadSourceNumber(value.children[2]) };
}
Point ReadSourcePoint(const step_semantics::SourceBindings& source, std::size_t point, double lengthToMm)
{
    const auto result = construction::Mul(ReadSourceTriple(source.Field(point, "coordinates").Value()), lengthToMm);
    for (const auto value : result)
    {
        if (!std::isfinite(value))
        {
            throw std::domain_error("GeneralLengthRangeUnestablished");
        }
    }
    return result;
}
Result<Point> ReadSourceDirection(const step_semantics::SourceBindings& source, std::size_t direction)
{
    return construction::Unit(ReadSourceTriple(source.Field(direction, "direction_ratios").Value()));
}
Result<SourceFrame> ReadSourceFrame(const step_semantics::SourceBindings& source, std::size_t placement, double lengthToMm)
{
    using namespace construction;
    SourceFrame frame;
    frame.origin = ReadSourcePoint(source, source.Field(placement, "location").ReferenceTarget(), lengthToMm);
    const auto axis = source.Field(placement, "axis"), reference = source.Field(placement, "ref_direction");
    if (axis.Value().kind == step_source::ValueKind::Unset)
    {
        frame.axes[2] = { 0, 0, 1 };
    }
    else
    {
        auto direction = ReadSourceDirection(source, axis.ReferenceTarget());
        if (!direction)
        {
            return tl::unexpected(std::move(direction.error()));
        }
        frame.axes[2] = *direction;
    }
    Point x;
    if (reference.Value().kind != step_source::ValueKind::Unset)
    {
        auto direction = ReadSourceDirection(source, reference.ReferenceTarget());
        if (!direction)
        {
            return tl::unexpected(std::move(direction.error()));
        }
        x = *direction;
    }
    else
    {
        x = frame.axes[2][1] != 0 || frame.axes[2][2] != 0 ? Point{ 1, 0, 0 } : Point{ 0, 1, 0 };
    }
    // Pinned AP214E3_2010.exp (71ab140f...), build_axes/FIRST_PROJ_AXIS:
    // the existing STEP_IMPORT frame contract chooses X unless
    // exactly parallel to Z, project onto its normal plane, then Y=Z cross X.
    // Dimensionless direction magnitudes never encode a physical scale. This
    // double cross and its range failures are shared with native construction.
    auto projected = Unit(Cross(frame.axes[2], Cross(x, frame.axes[2])));
    if (!projected)
    {
        return tl::unexpected(std::move(projected.error()));
    }
    frame.axes[0] = *projected;
    auto perpendicular = Unit(Cross(frame.axes[2], frame.axes[0]));
    if (!perpendicular)
    {
        return tl::unexpected(std::move(perpendicular.error()));
    }
    frame.axes[1] = *perpendicular;
    return frame;
}
} // namespace step_import::detail
