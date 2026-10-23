// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepUnreferencedPlacements.h"

#include "StepSourceNumber.h"
#include "StepUnitRational.h"

#include <algorithm>
#include <array>
#include <unordered_set>

namespace step_semantics
{
PlacementRetentionResult ProveUnreferencedPlacements(
    const SemanticData& data,
    const std::vector<std::vector<std::size_t>>& outgoing,
    std::span<const std::size_t> records,
    const Diagnostic& original,
    const std::function<void(step_source::Span)>& visit
)
{
    using unit_dimensions::Multiply;
    using unit_dimensions::Rational;
    const auto& source = *data.source;
    const auto& references = source.References();
    std::unordered_set<std::size_t> members(records.begin(), records.end());
    RetainedPlacementDeclarations receipt;
    receipt.originalDiagnostic = original;
    receipt.records.assign(records.begin(), records.end());
    const auto value = [&](std::size_t r, std::string_view field) -> const step_source::Value&
    {
        visit(source.Records()[r].span);
        return *data.fields[r].attributes[FindFieldBinding(data, r, field)].declaration.value;
    };
    const auto ref = [&](std::size_t r, std::string_view field)
    {
        return data.referenceTargets.at(&value(r, field));
    };
    const auto direction = [&](std::size_t r) -> bool
    {
        const auto& ratios = value(r, "direction_ratios").children;
        return ratios.size() == 3 && std::any_of(
                                         ratios.begin(),
                                         ratios.end(),
                                         [](const auto& ratio)
                                         {
                                             return CompareSourceReals(ratio.text, "0.") != 0;
                                         }
                                     );
    };
    // All binding/type/global prerequisites already ran. This narrower project
    // policy admits only simple 3D frame declarations, never an entity subtype,
    // shape, wireframe, unit, transformation, metadata or opaque unknown record.
    for (auto r : records)
    {
        const auto& record = source.Records()[r];
        visit(record.span);
        if (record.components.size() != 1)
        {
            return {};
        }
        const auto& type = record.components[0].name;
        if (type == "AXIS2_PLACEMENT_3D")
        {
            receipt.placements.push_back(r);
        }
        else if (type == "CARTESIAN_POINT")
        {
            if (value(r, "coordinates").children.size() != 3)
            {
                return {};
            }
        }
        else if (type == "DIRECTION")
        {
            if (!direction(r))
            {
                return {};
            }
        }
        else
        {
            return {};
        }
    }
    if (receipt.placements.empty())
    {
        return {};
    }
    for (auto r : records)
    {
        const auto& record = source.Records()[r];
        // Complete incoming AND outgoing evidence, not a selected-root sample.
        // A declaration used by geometry/metadata/units cannot enter this set.
        for (auto index : source.Incoming(record.id))
        {
            visit(references[index].span);
            if (!members.contains(references[index].recordIndex))
            {
                return {};
            }
        }
        for (auto index : outgoing[r])
        {
            visit(references[index].span);
            if (!members.contains(references[index].targetRecordIndex))
            {
                return {};
            }
        }
    }
    std::size_t current = receipt.placements.front();
    try
    {
        for (auto r : receipt.placements)
        {
            current = r;
            const auto location = ref(r, "location");
            if (source.Records()[location].components.front().name != "CARTESIAN_POINT")
            {
                return {};
            }
            const auto& axis = value(r, "axis");
            const auto& reference = value(r, "ref_direction");
            if (reference.kind == step_source::ValueKind::Unset)
            {
                continue;
            }
            std::array<Rational, 3> z{ Rational(0), Rational(0), Rational(1) }, x;
            if (axis.kind != step_source::ValueKind::Unset)
            {
                const auto& ratios = value(data.referenceTargets.at(&axis), "direction_ratios").children;
                for (std::size_t i = 0; i < 3; ++i)
                {
                    z[i] = Rational::Decimal(ratios[i].text);
                }
            }
            const auto& ratios = value(data.referenceTargets.at(&reference), "direction_ratios").children;
            for (std::size_t i = 0; i < 3; ++i)
            {
                x[i] = Rational::Decimal(ratios[i].text);
            }
            bool regular = false;
            for (std::size_t i = 0; i < 3; ++i)
            {
                visit(source.Records()[r].span);
                const auto j = (i + 1) % 3;
                regular |= Multiply(z[i], x[j]) != Multiply(z[j], x[i]);
            }
            if (!regular)
            {
                return {};
            }
        }
    }
    catch (const unit_dimensions::Capacity&)
    {
        Diagnostic diagnostic;
        diagnostic.source.key = "UnreferencedPlacementProofCapacity";
        diagnostic.source.stage = "D2";
        diagnostic.source.primary = source.Records()[current].span;
        diagnostic.source.recordId = source.Records()[current].id;
        diagnostic.source.message = "Exact frame regularity proof exceeds bounded arithmetic capacity";
        diagnostic.classification = "ResourceLimit";
        diagnostic.responsibility = "disconnected placement retention proof, not source validity";
        return { {}, ProductStructureFailure{ Status::ResourceLimit, std::move(diagnostic) } };
    }
    std::unordered_set<std::size_t> reached;
    auto stack = receipt.placements;
    while (!stack.empty())
    {
        const auto r = stack.back();
        stack.pop_back();
        if (!reached.insert(r).second)
        {
            continue;
        }
        visit(source.Records()[r].span);
        for (auto index : outgoing[r])
        {
            visit(references[index].span);
            stack.push_back(references[index].targetRecordIndex);
        }
    }
    if (reached.size() != members.size())
    {
        return {}; // No unrelated orphan points/directions hidden in the group.
    }
    return { std::move(receipt), {} };
}
} // namespace step_semantics
