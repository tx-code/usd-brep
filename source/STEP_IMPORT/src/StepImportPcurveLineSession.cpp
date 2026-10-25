// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveLineSession.h"

#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmSTEPSurface.h>
#include <set>

namespace step_import::detail
{
Result<void> VerifyPcurveLineBindings(
    const PcurveLineSession& session,
    const SmContext& context,
    const std::function<SplineChartCallbacks(std::size_t)>& callbacks
)
{
    std::set<const SmSurface*> surfaces;
    std::set<const SmCurve*> curves;
    for (const auto& binding : session.bindings)
    {
        const auto* wrapper = binding.carrier ? dynamic_cast<const SmSTEPSurface*>(binding.carrier->GetBaseSurface()) : nullptr;
        const auto* edge = binding.carrier ? dynamic_cast<const SmEdge*>(binding.carrier->GetOwner()) : nullptr;
        // Pointer traversal is permitted only after the owning BRep's existing
        // ValidatePointers gate. A constructor/copy may leave the base backlink
        // null; do not add it here and change tolerance inheritance.
        const bool owned = binding.window && binding.witness && binding.surface && binding.uv && binding.carrier && wrapper && edge &&
                           binding.carrier->OwnsBaseSurface() && wrapper->OwnsBaseSurface() && binding.carrier->GetUVCurve() == binding.uv &&
                           wrapper->GetBaseSurface() == binding.surface && binding.uv->GetOwner() == binding.carrier &&
                           wrapper->GetOwner() == binding.carrier && (!binding.surface->GetOwner() || binding.surface->GetOwner() == wrapper) &&
                           binding.carrier->GetContext() == &context && binding.surface->GetContext() == &context &&
                           binding.uv->GetContext() == &context && wrapper->GetContext() == &context && edge->GetCurve() == binding.carrier &&
                           surfaces.insert(binding.surface).second && curves.insert(binding.uv).second;
        if (!owned)
        {
            return tl::unexpected(
                Failure{ Status::PostcheckFailure,
                         "PcurveLineOwnershipChanged",
                         "Source LINE-window evidence must remain attached to its owned defining carrier",
                         binding.sourceRecord,
                         "PcurveLine" }
            );
        }
        if (auto checked = binding.witness->Verify(*binding.surface, *binding.uv, callbacks(binding.sourceRecord)); !checked)
        {
            if (!checked.error().record)
            {
                checked.error().record = binding.sourceRecord;
            }
            return tl::unexpected(std::move(checked.error()));
        }
    }
    return {};
}
} // namespace step_import::detail
