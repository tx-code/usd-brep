// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyInstances.h"

#include "StepImportVectorMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace step_import::detail
{
Result<AssemblyInstanceCandidate> ExpandAssemblyInstances(
    std::shared_ptr<const step_semantics::AssemblyInputs> inputs,
    std::span<const std::size_t> roots,
    std::size_t maximumOccurrences,
    std::size_t maximumInstances,
    std::size_t maximumDepth,
    const ExecutionCheckpoint& boundary
)
{
    if (!inputs || !boundary)
    {
        throw std::invalid_argument("Assembly expansion requires owning checked inputs and a checkpoint");
    }
    const auto refuse = [](Status status, const char* key, std::optional<std::size_t> record = {})
    {
        return tl::unexpected(Failure{ status, key, "Complete assembly instance expansion is unestablished", record, "AssemblyInstances" });
    };
    if (auto step = boundary(); !step)
    {
        return tl::unexpected(std::move(step.error()));
    }
    if (roots.empty())
    {
        return refuse(Status::Unsupported, "AssemblyRootSelectionRequired");
    }
    if (roots.size() > maximumInstances)
    {
        return refuse(Status::ResourceLimit, "AssemblyInstanceLimit");
    }
    const auto& graph = *inputs->Bindings().structure;
    std::unordered_set<std::size_t> declaredRoots, selected;
    for (auto root : graph.Roots())
    {
        if (auto step = boundary(); !step)
        {
            return tl::unexpected(std::move(step.error()));
        }
        declaredRoots.insert(root);
    }
    for (auto root : roots)
    {
        if (auto step = boundary(); !step)
        {
            return tl::unexpected(std::move(step.error()));
        }
        if (!declaredRoots.contains(root))
        {
            return refuse(Status::Unsupported, "AssemblySelectedRootUnavailable", root);
        }
        if (!selected.insert(root).second)
        {
            return refuse(Status::Unsupported, "AssemblyDuplicateRootSelection", root);
        }
    }
    // Re-evaluate from the owning source receipt, never trust caller-edited
    // matrix structs. Unselected declarations remain global prerequisites;
    // root choice is not authorization for source-error isolation.
    auto placements = EvaluateAssemblyPlacements(inputs, maximumOccurrences, boundary);
    if (!placements)
    {
        return tl::unexpected(std::move(placements.error()));
    }
    AssemblyInstanceCandidate candidate;
    candidate.placements = std::move(*placements);
    candidate.roots.assign(roots.begin(), roots.end());
    std::unordered_map<std::size_t, std::vector<std::size_t>> outgoing;
    const auto& bindings = inputs->Bindings().occurrences;
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        if (auto step = boundary(); !step)
        {
            return tl::unexpected(std::move(step.error()));
        }
        outgoing[bindings[i].parentDefinition].push_back(i);
    }
    for (std::size_t i = 0; i < roots.size(); ++i)
    {
        candidate.instances.push_back({ roots[i], i, 0, {}, {}, { { Point{ 1, 0, 0 }, Point{ 0, 1, 0 }, Point{ 0, 0, 1 } } }, { 0, 0, 0 } });
    }
    // Iterative breadth-first expansion preserves source occurrence order within
    // each parent. A shared definition reached along two paths yields two nodes;
    // a global "visited definitions" set would silently lose repeated instances.
    // Source usage acyclicity was checked before inputs were published, but the
    // explicit node/depth budgets also bound exponential expansion of a valid DAG.
    for (std::size_t parentIndex = 0; parentIndex < candidate.instances.size(); ++parentIndex)
    {
        if (auto step = boundary(); !step)
        {
            return tl::unexpected(std::move(step.error()));
        }
        const auto parent = candidate.instances[parentIndex]; // Vector growth cannot invalidate this value.
        const auto found = outgoing.find(parent.definition);
        if (found == outgoing.end())
        {
            continue;
        }
        for (const auto index : found->second)
        {
            if (auto step = boundary(); !step)
            {
                return tl::unexpected(std::move(step.error()));
            }
            const auto& binding = bindings[index];
            if (candidate.instances.size() >= maximumInstances)
            {
                return refuse(Status::ResourceLimit, "AssemblyInstanceLimit", binding.usage);
            }
            if (parent.depth >= maximumDepth)
            {
                return refuse(Status::ResourceLimit, "AssemblyInstanceDepthLimit", binding.usage);
            }
            const auto& local = candidate.placements.occurrences[index];
            AssemblyInstance node{ binding.childDefinition, parent.rootIndex, parent.depth + 1, parentIndex, binding.usage, {}, {} };
            for (std::size_t r = 0; r < 3; ++r)
            {
                for (std::size_t c = 0; c < 3; ++c)
                {
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        node.rotationToRoot[r][c] += parent.rotationToRoot[r][k] * local.rotation[k][c];
                    }
                }
                node.translationToRootMm[r] = parent.translationToRootMm[r] + construction::Dot(parent.rotationToRoot[r], local.translationMm);
                if (!std::isfinite(node.translationToRootMm[r]))
                {
                    return refuse(Status::Indeterminate, "AssemblyInstanceRangeUnestablished", binding.usage);
                }
                for (auto value : node.rotationToRoot[r])
                {
                    if (!std::isfinite(value))
                    {
                        return refuse(Status::Indeterminate, "AssemblyInstanceRangeUnestablished", binding.usage);
                    }
                }
            }
            // Arithmetic residual bound scales with composed path length; this
            // is not a geometric tolerance, and no matrix is repaired/projected.
            const double allowed = 64 * std::numeric_limits<double>::epsilon() * double(node.depth);
            for (std::size_t r = 0; r < 3; ++r)
            {
                for (std::size_t c = 0; c < 3; ++c)
                {
                    if (std::abs(construction::Dot(node.rotationToRoot[r], node.rotationToRoot[c]) - (r == c ? 1. : 0.)) > allowed)
                    {
                        return refuse(Status::Indeterminate, "AssemblyInstanceArithmeticUnestablished", binding.usage);
                    }
                }
            }
            candidate.instances.push_back(node);
        }
    }
    if (auto step = boundary(); !step)
    {
        return tl::unexpected(std::move(step.error()));
    }
    return candidate;
}
} // namespace step_import::detail
