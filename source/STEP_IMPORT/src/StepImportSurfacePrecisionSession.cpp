// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSurfacePrecisionSession.h"

#include "StepImportNativeStatus.h"

#include <SmFace.h>
#include <SmSurfOfRevolution.h>
#include <SmSurfaceWorkPrecision.h>
#include <SmTol.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace step_import::detail
{
namespace
{
bool Label(std::string_view value)
{
    return !value.empty() && value.size() <= 64 && (value.size() == 1 || value.front() != '0') &&
           std::all_of(
               value.begin(),
               value.end(),
               [](char c)
               {
                   return c >= '0' && c <= '9';
               }
           );
}
auto Refuse(const char* key, Status status = Status::Unsupported)
{
    return tl::unexpected(Failure{ status, key, "Explicit target-surface geometry precision is unestablished", {}, "TargetSurfacePrecision" });
}
double Resolution(const std::array<std::int64_t, 2>& value)
{
    return static_cast<double>(value[0]) / static_cast<double>(value[1]);
}
} // namespace
void ValidateTargetSurfacePrecisionOptions(const Options& options)
{
    if (!options.targetSurfacePrecision)
    {
        return;
    }
    const auto& r = *options.targetSurfacePrecision;
    if (!options.generalBrep || options.normalizePeriodicFaces || options.restrictUnusedSupportDomains || options.faceOrientationRepair ||
        options.cylindricalBandRepair || options.definitionSelection || options.sourceErrorIsolation || r.sourceSha256.size() != 64 ||
        !std::all_of(
            r.sourceSha256.begin(),
            r.sourceSha256.end(),
            [](char c)
            {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }
        ) ||
        !r.maximumFaces || r.maximumFaces > options.sourceLimits.records || r.faces.empty() || r.faces.size() > r.maximumFaces ||
        r.workResolutionMm[0] <= 0 || r.workResolutionMm[1] <= 0)
    {
        throw std::invalid_argument("Target surface precision requires explicit general source-bound scope, positive resolution and face budget");
    }
    const Fraction requested(r.workResolutionMm[0], r.workResolutionMm[1]);
    const auto resolution = Resolution(r.workResolutionMm);
    if (!std::isfinite(resolution) || !(resolution > 0) || requested > options.quality.initialWorkToleranceMm)
    {
        throw std::invalid_argument("Target surface calculation resolution must not exceed the unchanged topology work zone");
    }
    std::set<std::tuple<std::string, std::string, std::string>> identities;
    for (const auto& face : r.faces)
    {
        if (!Label(face.definitionId) || !Label(face.shapeId) || !Label(face.faceId) || !Label(face.surfaceId) ||
            !identities.emplace(face.definitionId, face.shapeId, face.faceId).second)
        {
            throw std::invalid_argument(
                "Target surface precision requires unique canonical definition/shape/face uses and exact source surface identities"
            );
        }
    }
}
TargetSurfacePrecisionSession::TargetSurfacePrecisionSession(const Options& options)
{
    ValidateTargetSurfacePrecisionOptions(options);
    if (!options.targetSurfacePrecision)
    {
        throw std::invalid_argument("Target surface precision session requires an explicit request");
    }
    m_receipt = std::make_shared<TargetSurfacePrecisionReceipt>();
    const auto request = *options.targetSurfacePrecision;
    m_receipt->sourceSha256 = request.sourceSha256;
    m_receipt->qualityId = options.quality.id;
    m_receipt->maximumFaces = request.maximumFaces;
    m_receipt->workResolutionMm = request.workResolutionMm;
    m_receipt->plans = request.faces;
    m_applied.assign(request.faces.size(), false);
    for (std::size_t i = 0; i < request.faces.size(); ++i)
    {
        const auto& face = request.faces[i];
        m_uses.emplace(std::make_tuple(face.definitionId, face.shapeId, face.faceId), i);
    }
}
Result<void> TargetSurfacePrecisionSession::BindSource(const step_source::SourceSnapshot& source)
{
    if (m_receipt->requestComplete)
    {
        return Refuse("TargetSurfacePrecisionSessionCompleted");
    }
    if (source.Sha256() != m_receipt->sourceSha256)
    {
        return Refuse("TargetSurfacePrecisionSourceMismatch");
    }
    m_bound = true;
    return {};
}
Result<std::optional<std::size_t>> TargetSurfacePrecisionSession::Apply(
    std::string_view definition,
    std::string_view shape,
    std::string_view face,
    std::string_view sourceSurface,
    SmFace& native,
    std::size_t targetFace
)
{
    if (!m_bound)
    {
        return Refuse("TargetSurfacePrecisionSourceNotBound");
    }
    if (m_receipt->requestComplete)
    {
        return Refuse("TargetSurfacePrecisionSessionCompleted");
    }
    const auto found = m_uses.find(std::make_tuple(std::string(definition), std::string(shape), std::string(face)));
    if (found == m_uses.end())
    {
        return std::optional<std::size_t>{};
    }
    const auto plan = found->second;
    if (m_receipt->plans[plan].surfaceId != sourceSurface)
    {
        return Refuse("TargetSurfacePrecisionBindingMismatch");
    }
    if (m_applied[plan])
    {
        return Refuse("TargetSurfacePrecisionUseAlreadyApplied");
    }
    auto* surface = native.GetSurface();
    if (!surface || surface->GetType() != SmSurfOfRevolution_TYPE || surface->GetOwner() != &native || surface->GetContext() != native.GetContext())
    {
        return Refuse("TargetSurfacePrecisionOwnershipUnestablished");
    }
    const auto before = static_cast<double>(native.GetTolerance());
    const auto requested = Resolution(m_receipt->workResolutionMm);
    if (!std::isfinite(before) || !(before > 0) || requested > before)
    {
        return Refuse("TargetSurfacePrecisionTopologyZoneMismatch");
    }
    if (auto good = CheckNativeStatus(SmSurfaceWorkPrecision::Set(*surface, requested), NativeStatusOrigin::GeneralConstruction); !good)
    {
        return tl::unexpected(std::move(good.error()));
    }
    double recorded = 0;
    double effective = 0;
    if (SmSurfaceWorkPrecision::Get(*surface, recorded) != SM_SUCCESS || recorded != requested ||
        SmSurfaceWorkPrecision::GetEffective(*surface, effective) != SM_SUCCESS || !std::isfinite(effective) || effective != requested ||
        static_cast<double>(SmTol::GetZoneTol3d(surface)) != before || native.GetTolerance() != before)
    {
        return Refuse("TargetSurfacePrecisionApplicationUnestablished", Status::Indeterminate);
    }
    // The setter changes only numerical metadata on this privately owned
    // surface. GetEffective is the numerical degeneracy resolution, not the
    // SmTol coincidence zone: seam/edge agreement must retain the original
    // physical thresholds. Source geometry, UV and topology zones are unedited.
    const auto action = m_receipt->actions.size();
    m_receipt->actions.push_back({ plan, targetFace, requested, effective, before, false });
    m_applied[plan] = true;
    return std::optional<std::size_t>{ action };
}
Result<void> TargetSurfacePrecisionSession::Verify(std::size_t action, SmFace& native)
{
    if (!m_bound || m_receipt->requestComplete || action >= m_receipt->actions.size())
    {
        return Refuse("TargetSurfacePrecisionActionNotApplied");
    }
    auto& observed = m_receipt->actions[action];
    auto* surface = native.GetSurface();
    double recorded = 0;
    double effective = 0;
    if (!surface || surface->GetType() != SmSurfOfRevolution_TYPE || surface->GetOwner() != &native || surface->GetContext() != native.GetContext() ||
        SmSurfaceWorkPrecision::Get(*surface, recorded) != SM_SUCCESS || recorded != observed.requestedMm ||
        SmSurfaceWorkPrecision::GetEffective(*surface, effective) != SM_SUCCESS || effective != observed.effectiveMm ||
        static_cast<double>(SmTol::GetZoneTol3d(surface)) != observed.topologyFaceToleranceMm ||
        native.GetTolerance() != observed.topologyFaceToleranceMm)
    {
        return Refuse("TargetSurfacePrecisionFinalStateUnestablished", Status::Indeterminate);
    }
    observed.nativeChecksPassed = true;
    return {};
}
Result<void> TargetSurfacePrecisionSession::Complete()
{
    if (m_receipt->requestComplete)
    {
        return {};
    }
    if (!m_bound || m_receipt->actions.size() != m_applied.size() || std::find(m_applied.begin(), m_applied.end(), false) != m_applied.end())
    {
        return Refuse("TargetSurfacePrecisionPlanNotCompleted");
    }
    if (std::any_of(
            m_receipt->actions.begin(),
            m_receipt->actions.end(),
            [](const auto& a)
            {
                return !a.nativeChecksPassed;
            }
        ))
    {
        return Refuse("TargetSurfacePrecisionNativeChecksUnestablished", Status::Indeterminate);
    }
    m_receipt->requestComplete = true;
    return {};
}
} // namespace step_import::detail
