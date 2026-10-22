// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportTrimMeasurement.h"

#include <SmBSplineCurve.h>
#include <functional>
#include <memory>
#include <optional>

class SmEdgeuse;
class SmExtent1d;
class SmSurface;

namespace step_import::detail
{
enum class DerivedUvStatus
{
    Unestablished,
    ProjectionFailed,
    ProjectionGapExceeded,
    RawGapExceeded,
    EndpointEvaluationFailed,
    EndpointEditFailed,
    MatchedGapExceeded,
    Ready
};
struct DerivedUvCandidate
{
    DerivedUvStatus status = DerivedUvStatus::Unestablished;
    std::unique_ptr<SmBSplineCurve> curve; // Caller-owned, unattached and with no association owner.
    std::optional<double> projectionGapMm;
    std::optional<TrimMeasurement> rawPair, matchedPair;
};
// Internal finite derived-UV operation, never a source pcurve or 3D geometry
// repair. The supplied projection is consumed. Preserve its parameterization
// while matching installed UV endpoints; both paired 3D measurements must pass.
// The callback precedes each native operation and may throw to cancel/limit work.
DerivedUvCandidate MatchProjectedUvEndpoints(
    std::unique_ptr<SmBSplineCurve> projected,
    const SmCurve& edge,
    const SmCurve& installed,
    const SmSurface& surface,
    const SmExtent1d& interval,
    double maximumMm,
    const std::function<void()>& beforeNativeCall
);
// The caller holds NativeMutex and owns an unpublished, complete face/edge
// candidate. Restores the surface owner on every exit and never installs a
// returned UV curve. The legacy kernel projection can grow edge tolerances on
// retry; the caller's final tolerance observation must reject budget excess.
DerivedUvCandidate RebuildDerivedUv(const SmEdgeuse& use, double maximumMm, const std::function<void()>& beforeNativeCall);
} // namespace step_import::detail
