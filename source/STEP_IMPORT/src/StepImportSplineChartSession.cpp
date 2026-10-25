// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSplineChartSession.h"

#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmSTEPSurface.h>
#include <set>

namespace step_import::detail
{
ParameterMapping SplineChartMapping(const BinarySplineChart& chart, std::string surface, std::string use, bool carrier)
{
    ParameterMapping mapping;
    mapping.kind = carrier ? "ExactBinarySplinePcurveChart" : "ExactBinarySplineFaceChart";
    mapping.sourceGeometryId = surface;
    mapping.sourceBasisGeometryId = std::move(surface);
    mapping.sourceUseId = std::move(use);
    mapping.equation =
        "target_uv=diag(2^ku,2^kv)*original_native_uv; complete stored XYZ/W unchanged and knots/UV controls reversibly scaled; "
        "ideal 3D definition identical, curve t unchanged; metric sample selects a chart, not a global conditioning/error proof";
    const auto original = chart.OriginalDomain(), target = chart.TargetDomain();
    const auto at = chart.MetricUv(), lengths = chart.MetricDerivativeLengths();
    mapping.parameters = { { "binary_exponents", { double(chart.UExponent()), double(chart.VExponent()) } },
                           { "original_native_uv_domain", { original.begin(), original.end() } },
                           { "target_native_uv_domain", { target.begin(), target.end() } },
                           { "metric_sample_original_uv", { at.begin(), at.end() } },
                           { "metric_derivative_lengths_mm_per_parameter", { lengths.begin(), lengths.end() } },
                           { "metric_length_ratio", { chart.MetricRatio() } },
                           { "metric_axis_cosine", { chart.MetricCosine() } },
                           { "surface_controls_checked", { double(chart.SurfaceControlCount()) } } };
    return mapping;
}
Result<void> VerifySplineChartBindings(
    const SplineChartSession& session,
    const SmContext& context,
    const std::function<SplineChartCallbacks(std::size_t)>& callbacks
)
{
    std::set<const SmSurface*> surfaces;
    std::set<const SmCurve*> curves;
    for (const auto& binding : session.bindings)
    {
        bool owned = binding.witness && binding.surface && binding.surface->GetContext() == &context && surfaces.insert(binding.surface).second;
        if (owned && binding.carrier)
        {
            const auto* wrapper = dynamic_cast<const SmSTEPSurface*>(binding.carrier->GetBaseSurface());
            const auto* edge = dynamic_cast<const SmEdge*>(binding.carrier->GetOwner());
            owned = binding.uv && !binding.face && binding.carrier->OwnsBaseSurface() && wrapper && wrapper->OwnsBaseSurface() &&
                    binding.carrier->GetUVCurve() == binding.uv && wrapper->GetBaseSurface() == binding.surface &&
                    binding.uv->GetOwner() == binding.carrier && wrapper->GetOwner() == binding.carrier &&
                    (!binding.surface->GetOwner() || binding.surface->GetOwner() == wrapper) && binding.carrier->GetContext() == &context &&
                    binding.uv->GetContext() == &context && wrapper->GetContext() == &context && edge && edge->GetCurve() == binding.carrier;
            // SmSTEPSurface's owning constructor does not require a base
            // backlink; its copy path installs one. Do not add a backlink here
            // and thereby change tolerance inheritance while checking evidence.
            owned = owned && curves.insert(binding.uv).second;
        }
        else if (owned)
        {
            owned = !binding.uv && binding.face && binding.face->GetContext() == &context && binding.face->GetSurface() == binding.surface &&
                    binding.surface->GetOwner() == binding.face;
        }
        if (!owned)
        {
            return tl::unexpected(
                Failure{ Status::PostcheckFailure,
                         "SplineChartOwnershipChanged",
                         "Exact parameter-chart evidence must remain attached to its owned defining geometry",
                         binding.sourceRecord,
                         "SplineChart" }
            );
        }
        if (auto verified = binding.witness->Verify(*binding.surface, binding.uv, callbacks(binding.sourceRecord)); !verified)
        {
            if (!verified.error().record)
            {
                verified.error().record = binding.sourceRecord;
            }
            return tl::unexpected(std::move(verified.error()));
        }
    }
    return {};
}
} // namespace step_import::detail
