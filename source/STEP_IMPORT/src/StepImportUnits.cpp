// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportUnits.h"

#include "StepImportExecution.h"

#include <gsl/assert>
#include <stdexcept>
#include <utility>

namespace step_import::detail
{
namespace
{
[[noreturn]] void Refuse(Status status, const char* key, std::size_t record, const char* message)
{
    throw Failure{ status, key, message, record, "GeneralBrep" };
}
} // namespace
ResolvedLengthScale ResolveLengthScale(const step_semantics::Snapshot& source, std::size_t root, const std::function<void()>& boundary)
{
    Expects(bool(boundary));
    boundary();
    const auto count = source.Source().Records().size();
    Expects(root < count);
    // Dense source indices bound lookup and cycle bookkeeping by the captured
    // graph. Avoid O(chain length * all SI definitions), recursion, and label-
    // based selection. SI entries are borrowed from the owning source snapshot.
    std::vector<const step_semantics::SiUnit*> si(count, nullptr);
    for (const auto& definition : source.UnitContext().siDefinitions)
    {
        boundary();
        Expects(definition.recordIndex < count);
        si[definition.recordIndex] = &definition;
    }
    std::vector<unsigned char> active(count);
    std::vector<ConversionUnitDefinition> path;
    auto unit = root;
    ResolvedLengthScale result;
    try
    {
        for (;;)
        {
            boundary();
            Expects(unit < count);
            if (active[unit])
            {
                Refuse(Status::Unsupported, "GeneralCyclicUnitDefinition", unit, "Canonical physical scale requires an acyclic SI-rooted chain");
            }
            if (const auto terminal = si[unit])
            {
                if (terminal->quantity != "Length" || terminal->siName != "METRE")
                {
                    Refuse(Status::Unsupported, "GeneralConversionQuantityUnsupported", unit, "Length scale requires a nominal SI length root");
                }
                result.scaleMm = Fraction::Decimal("1E" + std::to_string(terminal->canonicalPower10));
                break;
            }
            if (!source.IsA(unit, "LENGTH_UNIT") || !source.IsA(unit, "CONVERSION_BASED_UNIT"))
            {
                Refuse(
                    Status::Unsupported,
                    "GeneralConversionBasisUnsupported",
                    unit,
                    "Only nominal conversion length units ending at SI METRE are supported"
                );
            }
            active[unit] = 1;
            const auto factor = source.Field(unit, "conversion_factor").ReferenceTarget();
            boundary();
            const auto& value = source.Field(factor, "value_component").Value();
            const auto base = source.Field(factor, "unit_component").ReferenceTarget();
            // Source WHERE/dimensional checks were already run. This is the
            // additional project prerequisite for a positive, reversible
            // physical length scale, not an invented AP214 positivity clause.
            Expects(value.kind == step_source::ValueKind::Typed && value.children.size() == 1);
            const auto numeric = Fraction::Decimal(value.children.front().text);
            if (!(numeric > Fraction()))
            {
                Refuse(Status::Unsupported, "GeneralUnitScaleNotPositive", factor, "Every physical conversion factor must be positive");
            }
            path.push_back({ unit, factor, base, value.text, numeric, {} });
            unit = base;
        }
        while (!path.empty())
        {
            boundary();
            auto definition = std::move(path.back());
            path.pop_back();
            unit = definition.unit;
            result.scaleMm = definition.factorValue * result.scaleMm;
            definition.scaleMm = result.scaleMm;
            result.definitions.push_back(std::move(definition));
        }
    }
    catch (const std::overflow_error&)
    {
        // Capacity exhaustion is target numerical evidence not established;
        // never round the source factor or relabel it a source-schema error.
        Refuse(Status::Indeterminate, "GeneralUnitScaleRange", unit, "Exact conversion exceeds the native rational evidence capacity");
    }
    boundary();
    return result;
}
} // namespace step_import::detail
