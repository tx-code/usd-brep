// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSurfaceCurveDependencies.h"

#include "StepSourceNumber.h"

#include <algorithm>
#include <bit>
#include <iterator>

namespace step_semantics
{
namespace
{
using Index = std::size_t;
using step_source::Span;
using step_source::Value;
constexpr std::string_view Roots[] = { "SURFACE_CURVE", "PCURVE", "DEFINITIONAL_REPRESENTATION", "AXIS1_PLACEMENT", "SURFACE_OF_REVOLUTION" };

class Checks
{
public:

    Checks(const SemanticData& data, const std::function<void(Span)>& visit, GeometryDependencyCounts& counts)
        : m_data(data), m_visit(visit), m_counts(counts)
    {
    }
    GeometryDependencyResult Run()
    {
        bool active = false;
        for (const auto name : Roots)
        {
            active = active || !Population(name).empty();
        }
        if (!active)
        {
            return {}; // Existing input keeps its prerequisite order, visits and budgets.
        }
        Expects(m_data.bindingsComplete);
        IndexReferences();
        for (const auto r : Population("DEFINITIONAL_REPRESENTATION"))
        {
            const auto& context = Field(r, "context_of_items");
            Predicate(context.declaration.value->span);
            if (!IsA(Target(*context.declaration.value), "PARAMETRIC_REPRESENTATION_CONTEXT"))
            {
                return Error(r, context, *context.declaration.value, "DEFINITIONAL_REPRESENTATION.WR1", "PARAMETRIC_REPRESENTATION_CONTEXT");
            }
        }
        for (const auto r : Population("PCURVE"))
        {
            const auto& reference = Field(r, "reference_to_curve");
            const auto definition = Target(*reference.declaration.value);
            const auto& items = *Field(definition, "items").declaration.value;
            Predicate(reference.declaration.value->span);
            m_visit(items.span);
            if (items.children.size() != 1)
            {
                return Error(r, reference, *reference.declaration.value, "PCURVE.WR1", "exactly one representation item", {}, { items.span });
            }
            Predicate(reference.declaration.value->span);
            m_visit(items.children[0].span);
            const auto curve = Target(items.children[0]);
            if (!IsA(curve, "CURVE"))
            {
                return Error(r, reference, *reference.declaration.value, "PCURVE.WR2", "CURVE item", {}, { items.children[0].span });
            }
        }
        if (auto finite = FiniteDefinitions(); !finite)
        {
            return finite;
        }
        RepresentationDimensions();
        const auto footprint = Footprint();
        for (Index r = 0; r < footprint.size(); ++r)
        {
            if (footprint[r])
            {
                if (auto compatible = CompatibleDimensions(r); !compatible)
                {
                    return compatible;
                }
            }
        }
        for (const auto r : Population("PCURVE"))
        {
            const auto& reference = Field(r, "reference_to_curve");
            // PCURVE itself may have dim=3. WR3 concerns the referenced curve
            // in its own parameter representation, not its lifted geometry.
            // Reuse immutable bindings after WR1/WR2; no record-sized duplicate
            // parameter-curve index or new interpretation is needed.
            const auto definition = Target(*reference.declaration.value);
            const auto curve = Target(Field(definition, "items").declaration.value->children[0]);
            if (auto checked = RequireDimension(r, reference, curve, 2, "PCURVE.WR3"); !checked)
            {
                return checked;
            }
        }
        for (const auto r : Population("SURFACE_CURVE"))
        {
            const auto& curve = Field(r, "curve_3d");
            const auto target = Target(*curve.declaration.value);
            if (auto checked = RequireDimension(r, curve, target, 3, "SURFACE_CURVE.WR1"); !checked)
            {
                return checked;
            }
            const auto& associations = Field(r, "associated_geometry");
            const auto& values = *associations.declaration.value;
            const auto& master = *Field(r, "master_representation").declaration.value;
            for (Index which = 0; which != 2; ++which)
            {
                Predicate(values.span);
                const bool selected = master.text == (which ? "PCURVE_S2" : "PCURVE_S1");
                // Source LIST positions are one-based and retain repetition.
                // An absent second item cannot satisfy an explicitly selected S2.
                if (selected && (which >= values.children.size() || !IsA(Target(values.children[which]), "PCURVE")))
                {
                    const auto& primary = which < values.children.size() ? values.children[which] : values;
                    return Error(
                        r,
                        associations,
                        primary,
                        which ? "SURFACE_CURVE.WR3" : "SURFACE_CURVE.WR2",
                        "PCURVE at selected LIST position",
                        which < values.children.size() ? std::vector<Index>{ which + 1 } : std::vector<Index>{}
                    );
                }
            }
            Predicate(curve.declaration.value->span);
            if (IsA(target, "PCURVE"))
            {
                return Error(r, curve, *curve.declaration.value, "SURFACE_CURVE.WR4", "curve_3d is not PCURVE");
            }
        }
        for (const auto r : Population("AXIS1_PLACEMENT"))
        {
            const auto& location = Field(r, "location");
            if (auto checked = RequireDimension(r, location, r, 3, "AXIS1_PLACEMENT.WR1"); !checked)
            {
                return checked;
            }
        }
        return {};
    }

private:

    const SemanticData& m_data;
    const std::function<void(Span)>& m_visit;
    GeometryDependencyCounts& m_counts;
    std::vector<Index> m_offsets, m_references;
    std::vector<unsigned char> m_dimensions;

    bool IsA(Index r, std::string_view name) const
    {
        const auto type = Schema(m_data).Find(name);
        return type && m_data.types[r].test(*type);
    }
    std::span<const Index> Population(std::string_view name) const
    {
        const auto type = Schema(m_data).Find(name);
        return type ? std::span<const Index>(m_data.populations[*type]) : std::span<const Index>{};
    }
    const BoundAttribute& Field(Index r, std::string_view name) const
    {
        const auto& fields = m_data.fields[r];
        const auto found = fields.named.equal_range(name);
        Expects(found.first != found.second && std::next(found.first) == found.second);
        return fields.attributes[found.first->second];
    }
    Index Target(const Value& value) const
    {
        return m_data.referenceTargets.at(&value); // Actual immutable bound value, never a reconstructed reference.
    }
    void Predicate(Span span, bool entity = true)
    {
        m_visit(span);
        ++m_counts.total;
        m_counts.entityPredicates += entity ? 1 : 0;
    }
    tl::unexpected<GeometryDependencyFailure> Error(
        Index r,
        const BoundAttribute& bound,
        const Value& value,
        std::string_view clause,
        std::string expected,
        std::vector<Index> suffix = {},
        std::vector<Span> related = {},
        Status status = Status::Rejected,
        std::string key = {}
    ) const
    {
        Diagnostic issue;
        const auto& record = m_data.source->Records()[r];
        issue.source.key = key.empty() ? "SourceRuleViolation:" + std::string(clause) : std::move(key);
        issue.source.stage = status == Status::Rejected ? "D3" : "D2";
        issue.source.message = status == Status::Rejected ? "Source surface-curve prerequisite is false" :
                                                            "Required surface-curve evidence is not established";
        issue.source.primary = value.span;
        issue.source.recordId = record.id;
        issue.source.component = record.components[bound.component].name;
        issue.source.parameterPath = { bound.parameter + 1 };
        issue.source.parameterPath.insert(issue.source.parameterPath.end(), suffix.begin(), suffix.end());
        issue.source.related = std::move(related);
        issue.classification = status == Status::Rejected ? "SourceInvalid" : "ProfileUnsupported";
        issue.field = bound.declaration.name;
        issue.declaredIn = bound.declaration.declaredIn;
        issue.declaredType = bound.declaration.declaredType;
        issue.expected = std::move(expected);
        issue.actual = status == Status::Rejected ? "FALSE" : "Unestablished";
        issue.responsibility = status == Status::Rejected ? "AP214E3_2010.exp (71ab140f...) " + std::string(clause) :
                                                            "project necessary finite-definition/dimension evidence; not a full schema verdict";
        return tl::unexpected(GeometryDependencyFailure{ status, std::move(issue) });
    }
    void IndexReferences()
    {
        // Request-local CSR borrows the source reference inventory, without
        // another value graph or per-record heap-allocated adjacency lists.
        const auto& refs = m_data.source->References();
        m_offsets.assign(m_data.types.size() + 1, 0);
        for (const auto& ref : refs)
        {
            m_visit(ref.span);
            ++m_offsets[ref.recordIndex + 1];
        }
        for (Index r = 0; r < m_data.types.size(); ++r)
        {
            m_visit(m_data.source->Records()[r].span);
            m_offsets[r + 1] += m_offsets[r];
        }
        auto cursors = m_offsets;
        m_references.resize(refs.size());
        for (Index i = 0; i < refs.size(); ++i)
        {
            m_references[cursors[refs[i].recordIndex]++] = i;
        }
    }
    bool DefinitionEdge(Index from, Index to) const
    {
        return IsA(to, "CURVE") || IsA(to, "SURFACE") || (IsA(from, "PCURVE") && IsA(to, "DEFINITIONAL_REPRESENTATION")) ||
               (IsA(from, "COMPOSITE_CURVE") && IsA(to, "COMPOSITE_CURVE_SEGMENT"));
    }
    GeometryDependencyResult FiniteDefinitions()
    {
        struct Frame
        {
            Index record, next;
        };
        std::vector<unsigned char> state(m_data.types.size(), 0);
        std::vector<Frame> pending;
        const auto& refs = m_data.source->References();
        for (Index root = 0; root < m_data.types.size(); ++root)
        {
            if (state[root] ||
                !(IsA(root, "CURVE") || IsA(root, "SURFACE") || IsA(root, "DEFINITIONAL_REPRESENTATION") || IsA(root, "COMPOSITE_CURVE_SEGMENT")))
            {
                continue;
            }
            state[root] = 1;
            pending.push_back({ root, m_offsets[root] });
            while (!pending.empty())
            {
                auto& frame = pending.back();
                if (frame.next == m_offsets[frame.record + 1])
                {
                    state[frame.record] = 2;
                    pending.pop_back();
                    continue;
                }
                const auto& ref = refs[m_references[frame.next++]];
                m_visit(ref.span);
                if (!DefinitionEdge(frame.record, ref.targetRecordIndex))
                {
                    continue;
                }
                if (state[ref.targetRecordIndex] == 1)
                {
                    // This is a supported-definition restriction, not a claim
                    // that every circular EXPRESS relation is invalid. Never
                    // recurse through representation inverse/context links.
                    const auto& fields = m_data.fields[frame.record].attributes;
                    const auto field = std::find_if(
                        fields.begin(),
                        fields.end(),
                        [&](const auto& bound)
                        {
                            return bound.component == ref.componentIndex && bound.parameter + 1 == ref.parameterPath.front();
                        }
                    );
                    Expects(field != fields.end());
                    auto error = Error(
                        frame.record,
                        *field,
                        *field->declaration.value,
                        "geometry definition",
                        "finite forward definition",
                        {},
                        {},
                        Status::Unsupported,
                        "SourceGeometryDependencyUnestablished"
                    );
                    error.value().diagnostic.source.primary = ref.span;
                    error.value().diagnostic.source.parameterPath = ref.parameterPath;
                    return error;
                }
                if (!state[ref.targetRecordIndex])
                {
                    state[ref.targetRecordIndex] = 1;
                    pending.push_back({ ref.targetRecordIndex, m_offsets[ref.targetRecordIndex] });
                }
            }
        }
        return {};
    }
    bool Item(Index r) const
    {
        return IsA(r, "REPRESENTATION_ITEM") || IsA(r, "FOUNDED_ITEM");
    }
    unsigned char ContextDimension(Index context) const
    {
        if (!IsA(context, "GEOMETRIC_REPRESENTATION_CONTEXT"))
        {
            return 8;
        }
        const auto& value = *Field(context, "coordinate_space_dimension").declaration.value;
        m_visit(value.span);
        const auto dimension = SourceIndexAtMost(value.text, 3);
        return dimension && *dimension == 2 ? 1 : dimension && *dimension == 3 ? 2 : 4;
    }
    std::vector<Span> ContextWitness(Index item, unsigned char incompatible) const
    {
        // Recover one original context only on failure. Reuse the source's
        // incoming index instead of storing four context IDs for every record.
        // The RI/founded-item walk stops at each representation's own items;
        // a parameter definition cannot acquire an outer 3D context here.
        std::vector<unsigned char> seen(m_data.types.size(), 0);
        std::vector<Index> pending{ item };
        const auto& records = m_data.source->Records();
        const auto& refs = m_data.source->References();
        while (!pending.empty())
        {
            const auto r = pending.back();
            pending.pop_back();
            if (seen[r])
            {
                continue;
            }
            seen[r] = 1;
            m_visit(records[r].span);
            for (const auto index : m_data.source->Incoming(records[r].id))
            {
                const auto& ref = refs[index];
                m_visit(ref.span);
                if (IsA(ref.recordIndex, "REPRESENTATION"))
                {
                    const auto& items = *Field(ref.recordIndex, "items").declaration.value;
                    if (ref.span.begin >= items.span.begin && ref.span.end <= items.span.end)
                    {
                        const auto& context = *Field(ref.recordIndex, "context_of_items").declaration.value;
                        m_visit(context.span);
                        if (ContextDimension(Target(context)) & incompatible)
                        {
                            return { context.span, records[Target(context)].span };
                        }
                    }
                }
                else if (Item(ref.recordIndex))
                {
                    pending.push_back(ref.recordIndex);
                }
            }
        }
        return {};
    }
    void RepresentationDimensions()
    {
        // Fixed dimension_of / using_representations: a point/direction/vector
        // has intrinsic dimension; other GRI dimensions come from using
        // representations. Traverse RI/founded-item dependencies only, so a
        // PCURVE's definitional REPRESENTATION is a separate parameter context.
        // Four monotone labels bound shared DAGs/cycles to O(V+E), not paths.
        m_dimensions.assign(m_data.types.size(), 0);
        std::vector<Index> pending;
        const auto add = [&](Index r, unsigned char label)
        {
            if ((m_dimensions[r] | label) != m_dimensions[r])
            {
                m_dimensions[r] |= label;
                pending.push_back(r);
            }
        };
        for (const auto r : Population("REPRESENTATION"))
        {
            const auto& contextValue = *Field(r, "context_of_items").declaration.value;
            m_visit(contextValue.span);
            const auto context = Target(contextValue);
            const auto label = ContextDimension(context);
            for (const auto& item : Field(r, "items").declaration.value->children)
            {
                m_visit(item.span);
                add(Target(item), label);
            }
        }
        const auto& refs = m_data.source->References();
        for (Index at = 0; at < pending.size(); ++at)
        {
            const auto r = pending[at];
            m_visit(m_data.source->Records()[r].span);
            for (Index i = m_offsets[r]; i < m_offsets[r + 1]; ++i)
            {
                const auto& ref = refs[m_references[i]];
                m_visit(ref.span);
                if (Item(ref.targetRecordIndex))
                {
                    add(ref.targetRecordIndex, m_dimensions[r]);
                }
            }
        }
    }
    std::vector<unsigned char> Footprint() const
    {
        std::vector<unsigned char> marked(m_data.types.size(), 0);
        std::vector<Index> pending;
        for (const auto name : Roots)
        {
            const auto roots = Population(name);
            pending.insert(pending.end(), roots.begin(), roots.end());
        }
        const auto& refs = m_data.source->References();
        while (!pending.empty())
        {
            const auto r = pending.back();
            pending.pop_back();
            if (marked[r])
            {
                continue;
            }
            marked[r] = 1;
            m_visit(m_data.source->Records()[r].span);
            for (Index i = m_offsets[r]; i < m_offsets[r + 1]; ++i)
            {
                const auto& ref = refs[m_references[i]];
                m_visit(ref.span);
                if (Item(ref.targetRecordIndex) || IsA(ref.targetRecordIndex, "DEFINITIONAL_REPRESENTATION"))
                {
                    pending.push_back(ref.targetRecordIndex);
                }
            }
        }
        return marked;
    }
    GeometryDependencyResult CompatibleDimensions(Index r)
    {
        if (!IsA(r, "GEOMETRIC_REPRESENTATION_ITEM"))
        {
            return {};
        }
        // Scoped necessary GRI/compatible_dimension/dimensionality predicates,
        // not a claim that all global rules ran for every source entity.
        const auto& anchor = m_data.fields[r].attributes.front();
        if (m_dimensions[r] & 8)
        {
            Predicate(anchor.declaration.value->span);
            return Error(
                r,
                anchor,
                *anchor.declaration.value,
                "GEOMETRIC_REPRESENTATION_ITEM.WR1",
                "geometric using contexts",
                {},
                ContextWitness(r, 8)
            );
        }
        if (m_dimensions[r] & 4)
        {
            Predicate(anchor.declaration.value->span);
            return Error(
                r,
                anchor,
                *anchor.declaration.value,
                "DIMENSIONALITY_IS_TWO_OR_THREE.WR1",
                "using context dimension 2 or 3",
                {},
                ContextWitness(r, 4)
            );
        }
        const bool point = IsA(r, "CARTESIAN_POINT"), direction = IsA(r, "DIRECTION");
        if (point || direction)
        {
            const auto& bound = Field(r, point ? "coordinates" : "direction_ratios");
            const auto& values = *bound.declaration.value;
            Predicate(values.span);
            const auto n = values.children.size();
            if (((m_dimensions[r] & 1) && n != 2) || ((m_dimensions[r] & 2) && n != 3))
            {
                const auto incompatible = static_cast<unsigned char>((n == 2 ? 0 : 1) | (n == 3 ? 0 : 2));
                return Error(
                    r,
                    bound,
                    values,
                    point ? "COMPATIBLE_DIMENSION.WR1" : "COMPATIBLE_DIMENSION.WR2",
                    "dimension agrees with every using context",
                    {},
                    ContextWitness(r, incompatible)
                );
            }
            if (direction)
            {
                Predicate(values.span);
                bool nonzero = false;
                for (const auto& value : values.children)
                {
                    m_visit(value.span);
                    nonzero = nonzero || CompareSourceReals(value.text, "0.") != 0;
                }
                if (!nonzero)
                {
                    return Error(r, bound, values, "DIRECTION.WR1", "nonzero direction");
                }
            }
        }
        if (IsA(r, "VECTOR"))
        {
            const auto& bound = Field(r, "magnitude");
            Predicate(bound.declaration.value->span);
            if (CompareSourceReals(bound.declaration.value->text, "0.") < 0)
            {
                return Error(r, bound, *bound.declaration.value, "VECTOR.WR1", "nonnegative magnitude");
            }
        }
        return {};
    }
    GeometryDependencyResult RequireDimension(Index owner, const BoundAttribute& bound, Index item, unsigned expected, std::string_view clause)
    {
        Predicate(bound.declaration.value->span);
        const auto mask = m_dimensions[item];
        if (!mask || std::popcount(static_cast<unsigned>(mask)) != 1)
        {
            return Error(
                owner,
                bound,
                *bound.declaration.value,
                clause,
                "one established using-context dimension",
                {},
                {},
                Status::Unsupported,
                "SourceGeometryDimensionUnestablished"
            );
        }
        if (mask != (expected == 2 ? 1 : 2))
        {
            return Error(owner, bound, *bound.declaration.value, clause, "required source dimension " + std::to_string(expected));
        }
        return {};
    }
};
} // namespace

GeometryDependencyResult CheckSurfaceCurveDependencies(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    GeometryDependencyCounts& checks
)
{
    return Checks(data, visit, checks).Run();
}
bool SurfaceCurvePrerequisitesComplete(const step_source::InspectOutcome& source, const Outcome& checks)
{
    if (!source.source || checks.geometryDependencies != "Passed" || !checks.schemaSelection || !checks.schemaSelection->qualified ||
        !IsAp214SourceProfile(checks.schemaSelection->qualified->kind))
    {
        return false;
    }
    for (const auto& record : source.source->Records())
    {
        for (const auto& component : record.components)
        {
            if (std::find(std::begin(Roots), std::end(Roots), component.name) != std::end(Roots))
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace step_semantics
