// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSourceFrame.h"

#include "StepImportVectorMath.h"

#include <charconv>
#include <cmath>

namespace step_import::detail
{
namespace
{
Failure SourceValueFailure(const char* key)
{
    // Preserve the former domain-error outcome and unfinished strict attempt.
    // This is neither a source-invalid claim nor a configured quota failure.
    // The assembly boundary retains its distinct legacy diagnostic message.
    return Failure{ Status::Indeterminate, key, "Native derived evidence is unestablished", {}, "D5", NativeFailureKind::SourceValueConversion };
}
} // namespace
Result<double> ReadSourceNumber(const step_source::Value& value)
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
        return tl::unexpected(SourceValueFailure("GeneralNumericRangeUnestablished"));
    }
    return result;
}
Result<Point> ReadSourceTriple(const step_source::Value& value)
{
    if (value.children.size() != 3)
    {
        return tl::unexpected(SourceValueFailure("GeneralRequires3DGeometry"));
    }
    Point point;
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        auto coordinate = ReadSourceNumber(value.children[axis]);
        if (!coordinate)
        {
            return tl::unexpected(std::move(coordinate.error()));
        }
        point[axis] = *coordinate;
    }
    return point;
}
Result<Point> ReadSourcePoint(const step_semantics::SourceBindings& source, std::size_t point, double lengthToMm)
{
    auto coordinatesResult = ReadSourceTriple(source.Field(point, "coordinates").Value());
    if (!coordinatesResult)
    {
        return tl::unexpected(std::move(coordinatesResult.error()));
    }
    const auto& coordinates = *coordinatesResult;
    const auto result = construction::Mul(coordinates, lengthToMm);
    for (const auto value : result)
    {
        if (!std::isfinite(value))
        {
            return tl::unexpected(SourceValueFailure("GeneralLengthRangeUnestablished"));
        }
    }
    return result;
}
Result<Point> ReadSourceDirection(const step_semantics::SourceBindings& source, std::size_t direction)
{
    auto ratiosResult = ReadSourceTriple(source.Field(direction, "direction_ratios").Value());
    if (!ratiosResult)
    {
        return tl::unexpected(std::move(ratiosResult.error()));
    }
    const auto& ratios = *ratiosResult;
    return construction::Unit(ratios);
}
Result<SourceFrame> ReadSourceFrame(const step_semantics::SourceBindings& source, std::size_t placement, double lengthToMm)
{
    using namespace construction;
    SourceFrame frame;
    auto originResult = ReadSourcePoint(source, source.Field(placement, "location").ReferenceTarget(), lengthToMm);
    if (!originResult)
    {
        return tl::unexpected(std::move(originResult.error()));
    }
    const auto& origin = *originResult;
    frame.origin = origin;
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
