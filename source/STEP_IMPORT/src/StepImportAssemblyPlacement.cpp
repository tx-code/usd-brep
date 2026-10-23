// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyPlacement.h"

#include "StepImportSourceFrame.h"
#include "StepImportVectorMath.h"

#include <algorithm>
#include <gsl/assert>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace step_import::detail
{
Result<AssemblyPlacementCandidate> EvaluateAssemblyPlacements(
    std::shared_ptr<const step_semantics::AssemblyInputs> inputs,
    std::size_t maximumOccurrences,
    const ExecutionCheckpoint& boundary
)
{
    if (!inputs || !boundary)
    {
        throw std::invalid_argument("Assembly placement requires owning checked inputs and a checkpoint");
    }
    auto refuse = [](Status status, const char* key, std::size_t record)
    {
        return tl::unexpected(Failure{ status, key, "Assembly numeric placement prerequisite is unestablished", record, "AssemblyPlacement" });
    };
    if (auto operation = boundary(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    if (inputs->Bindings().occurrences.size() > maximumOccurrences)
    {
        return refuse(Status::ResourceLimit, "AssemblyOccurrenceLimit", inputs->Bindings().occurrences.front().usage);
    }
    AssemblyPlacementCandidate candidate;
    candidate.source = std::move(inputs);
    std::unordered_map<std::size_t, double> scales;
    for (const auto& context : candidate.source->Contexts())
    {
        auto length = ResolveLengthScale(*candidate.source, context.context, boundary);
        if (!length)
        {
            return tl::unexpected(std::move(length.error()));
        }
        const auto ratio = length->scaleMm;
        const double scale = double(ratio.Numerator()) / double(ratio.Denominator());
        if (!(scale > 0) || !std::isfinite(scale))
        {
            return refuse(Status::Indeterminate, "AssemblyUnitScaleRange", context.context);
        }
        scales.emplace(context.context, scale);
        candidate.contexts.push_back({ context.context, std::move(*length) });
    }
    const auto& fields = candidate.source->SourceFields();
    // An item can legitimately be reused in distinct contexts. Cache by BOTH
    // identities so one definition's length scale cannot leak into another.
    std::map<std::pair<std::size_t, std::size_t>, SourceFrame> frames;
    const auto frame = [&](std::size_t item, std::size_t context) -> Result<SourceFrame>
    {
        if (auto operation = boundary(); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (!fields.IsA(item, "AXIS2_PLACEMENT_3D"))
        {
            return refuse(Status::Unsupported, "AssemblyFrameTypeUnsupported", item);
        }
        const auto key = std::pair{ item, context };
        if (const auto found = frames.find(key); found != frames.end())
        {
            return found->second;
        }
        try
        {
            auto evaluated = ReadSourceFrame(fields, item, scales.at(context));
            if (!evaluated)
            {
                auto failure = std::move(evaluated.error());
                failure.record = item;
                failure.stage = "AssemblyPlacement";
                return tl::unexpected(std::move(failure));
            }
            frames.emplace(key, *evaluated);
            return *evaluated;
        }
        catch (const std::domain_error& error)
        {
            return refuse(Status::Indeterminate, error.what(), item);
        }
    };
    candidate.occurrences.reserve(candidate.source->Bindings().occurrences.size());
    for (const auto& binding : candidate.source->Bindings().occurrences)
    {
        auto child = frame(binding.childItem, binding.childContext);
        if (!child)
        {
            return tl::unexpected(std::move(child.error()));
        }
        auto parent = frame(binding.parentItem, binding.parentContext);
        if (!parent)
        {
            return tl::unexpected(std::move(parent.error()));
        }
        AssemblyOccurrencePlacement placement{};
        placement.usage = binding.usage;
        // Both origins and incoming part geometry are already in canonical mm.
        // T(p) = R_parent * R_child^T * (p - o_child_mm) + o_parent_mm.
        // Multiplying by the source unit ratio again would scale geometry twice.
        for (std::size_t r = 0; r < 3; ++r)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    placement.rotation[r][c] += parent->axes[axis][r] * child->axes[axis][c];
                }
            }
            placement.translationMm[r] = parent->origin[r] - construction::Dot(placement.rotation[r], child->origin);
            if (!std::isfinite(placement.translationMm[r]))
            {
                return refuse(Status::Indeterminate, "AssemblyPlacementRangeUnestablished", binding.usage);
            }
        }
        for (std::size_t r = 0; r < 3; ++r)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                const auto residual = std::abs(construction::Dot(placement.rotation[r], placement.rotation[c]) - (r == c ? 1.0 : 0.0));
                if (!std::isfinite(residual))
                {
                    return refuse(Status::Indeterminate, "AssemblyPlacementRangeUnestablished", binding.usage);
                }
                placement.orthonormalResidual = std::max(placement.orthonormalResidual, residual);
            }
        }
        const auto determinant = construction::Dot(placement.rotation[0], construction::Cross(placement.rotation[1], placement.rotation[2]));
        placement.orthonormalResidual = std::max(placement.orthonormalResidual, std::abs(determinant - 1));
        // Arithmetic sanity only, not an acceptance/repair tolerance in mm.
        // Never fit the matrix back to a rotation or claim bounded geometry error.
        if (!std::isfinite(determinant) || placement.orthonormalResidual > 64 * std::numeric_limits<double>::epsilon())
        {
            return refuse(Status::Indeterminate, "AssemblyFrameArithmeticUnestablished", binding.usage);
        }
        candidate.occurrences.push_back(placement);
    }
    if (auto operation = boundary(); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    return candidate;
}
} // namespace step_import::detail
