// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCurvePointConstruction.h"

#include <concepts>
#include <type_traits>

using namespace step_import;
using namespace step_import::detail;

template <class Group, class Evidence>
concept RoleAppend = requires(CurvePointDefinitions& definitions, SourceRecord record, Group& group, Evidence& evidence) {
    definitions.Append(record, group, evidence);
};
template <class T>
concept LvalueFinish = requires(T& definitions) { definitions.Finish(); };
template <class Index>
concept CurveLookup = requires(const OwnedCurvePointDefinitions& definitions, Index index) {
    { definitions.CurveAt(index) } -> std::same_as<const SmCurve&>;
};

static_assert(RoleAppend<WireframeInfo, WireframeEvidence>);
static_assert(RoleAppend<SupportingGeometryInfo, SupportingEvidence>);
static_assert(
    std::same_as<
        decltype(std::declval<CurvePointDefinitions&>().Append(SourceRecord{ 0 }, std::declval<WireframeInfo&>(), std::declval<WireframeEvidence&>())),
        Result<void>>
);
static_assert(std::same_as<
              decltype(std::declval<CurvePointDefinitions&>()
                           .Append(SourceRecord{ 0 }, std::declval<SupportingGeometryInfo&>(), std::declval<SupportingEvidence&>())),
              Result<SupportingAppendResult>>);
static_assert(!RoleAppend<WireframeInfo, SupportingEvidence>);
static_assert(!RoleAppend<SupportingGeometryInfo, WireframeEvidence>);
static_assert(!std::convertible_to<SupportingAppendResult, bool>);
static_assert(!std::convertible_to<std::size_t, SourceRecord>);
static_assert(!std::convertible_to<std::size_t, CurveIndex>);
static_assert(!std::convertible_to<PointIndex, CurveIndex>);
static_assert(CurveLookup<CurveIndex>);
static_assert(!CurveLookup<PointIndex>);
static_assert(!CurveLookup<std::size_t>);
static_assert(!std::is_copy_constructible_v<OwnedCurvePointDefinitions>);
static_assert(!std::is_copy_assignable_v<OwnedCurvePointDefinitions>);
static_assert(std::is_nothrow_move_constructible_v<OwnedCurvePointDefinitions>);
static_assert(std::is_nothrow_move_assignable_v<OwnedCurvePointDefinitions>);
static_assert(!std::is_copy_constructible_v<CurvePointDefinitions>);
static_assert(!LvalueFinish<CurvePointDefinitions>);
static_assert(std::same_as<decltype(WireframeEvidence{}.SourceResidualMm()), double>);
static_assert(std::same_as<decltype(SupportingEvidence{}.SourceResidualMm()), std::optional<double>>);

int main()
{
    // No test-only kernel adapter: the language checks the construction seam.
    return 0;
}
