// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepGeometryDependencies.h"

#include "StepSourceNumber.h"

#include <iterator>

namespace step_semantics
{
namespace
{
using step_source::Span;
using step_source::Value;
struct Stop
{
    Diagnostic diagnostic;
};
struct SplineDirection
{
    std::string_view degree, multiplicities, knots, countClause, parameterClause;
};
// Keep fields and governing clauses paired; never exchange source U/V to fit a target convention.
constexpr SplineDirection CurveDirection{ "degree", "knot_multiplicities", "knots", "B_SPLINE_CURVE_WITH_KNOTS.WR2", "B_SPLINE_CURVE_WITH_KNOTS.WR1" };
constexpr SplineDirection UDirection{ "u_degree",
                                      "u_multiplicities",
                                      "u_knots",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR3",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR1" };
constexpr SplineDirection VDirection{ "v_degree",
                                      "v_multiplicities",
                                      "v_knots",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR4",
                                      "B_SPLINE_SURFACE_WITH_KNOTS.WR2" };
class GeometryDependencies
{
public:

    GeometryDependencies(const SemanticData& data, const std::function<void(Span)>& visit, GeometryDependencyCounts& checks)
        : m_data(data), m_visit(visit), m_checks(checks)
    {
    }
    void Run()
    {
        Expects(m_data.bindingsComplete);
        std::vector<std::size_t> trimmed;
        for (std::size_t r = 0; r < m_data.fields.size(); ++r)
        {
            const auto& record = m_data.source->Records()[r];
            m_visit(record.span);
            for (const auto& bound : m_data.fields[r].attributes)
            {
                const auto& field = bound.declaration;
                const bool positive = field.declaredType == "POSITIVE_LENGTH_MEASURE";
                const bool coneRadius = record.components[bound.component].name == "CONICAL_SURFACE" && field.name == "radius";
                if (!positive && !coneRadius)
                {
                    continue;
                }
                Count(field.value->span, positive ? PredicateKind::Type : PredicateKind::Entity);
                // Pinned AP214: positive lengths are >0; CONICAL_SURFACE.WR1 allows
                // radius0. Its angle has no extra declaration bound: never clamp it.
                const auto order = CompareSourceReals(field.value->text, "0.");
                if (positive ? order <= 0 : order < 0)
                {
                    Fail(
                        r,
                        bound,
                        *field.value,
                        positive ? "POSITIVE_LENGTH_MEASURE.WR1" : "CONICAL_SURFACE.WR1",
                        positive ? ">0" : ">=0",
                        field.value->text
                    );
                }
            }
            if (IsA(r, "B_SPLINE_CURVE_WITH_KNOTS"))
            {
                Direction(r, Field(r, "control_points_list").declaration.value->children.size(), CurveDirection);
            }
            if (IsA(r, "B_SPLINE_SURFACE_WITH_KNOTS"))
            {
                const auto& bound = Field(r, "control_points_list");
                const auto& grid = *bound.declaration.value;
                const auto width = grid.children[0].children.size(); // Typed LIST minima checked before this phase.
                Count(grid.span, PredicateKind::Dependency);
                for (std::size_t i = 0; i < grid.children.size(); ++i)
                {
                    const auto& row = grid.children[i];
                    m_visit(row.span);
                    if (row.children.size() != width)
                    {
                        // DERIVE uses one v_upper from the first row. Do not truncate,
                        // pad or transpose a ragged source into another surface.
                        Fail(
                            r,
                            bound,
                            row,
                            "B_SPLINE_SURFACE.control_points DERIVE",
                            std::to_string(width),
                            std::to_string(row.children.size()),
                            { i + 1 },
                            true
                        );
                    }
                }
                Direction(r, grid.children.size(), UDirection);
                Direction(r, width, VDirection);
            }
            if (IsA(r, "RATIONAL_B_SPLINE_CURVE"))
            {
                Weights(r, false);
            }
            if (IsA(r, "RATIONAL_B_SPLINE_SURFACE"))
            {
                Weights(r, true);
            }
            if (IsA(r, "TRIMMED_CURVE"))
            {
                trimmed.push_back(r);
                for (const auto name : { "trim_1", "trim_2" })
                {
                    const auto& bound = Field(r, name);
                    const auto& value = *bound.declaration.value;
                    Count(value.span);
                    // The closed profiles admit only CARTESIAN_POINT and typed
                    // PARAMETER_VALUE, so their different ValueKinds represent
                    // exactly the two possible TYPEOF sets in WR1/WR2.
                    if (value.children.size() == 2 && value.children[0].kind == value.children[1].kind)
                    {
                        Fail(
                            r,
                            bound,
                            value,
                            name == std::string_view("trim_1") ? "TRIMMED_CURVE.WR1" : "TRIMMED_CURVE.WR2",
                            "different SELECT member types",
                            "same SELECT member type"
                        );
                    }
                }
            }
        }
        const auto composite = Schema(m_data).Find("COMPOSITE_CURVE");
        const auto segment = Schema(m_data).Find("COMPOSITE_CURVE_SEGMENT");
        if (composite && segment && (!m_data.populations[*composite].empty() || !m_data.populations[*segment].empty()))
        {
            CompositeChecks(m_data.populations[*composite], m_data.populations[*segment]);
            CompositeClosure(trimmed, m_data.populations[*composite]);
        }
        else
        {
            TrimmedClosure(trimmed); // Preserve existing no-composite charges and diagnostics.
        }
        WireframeChecks();
        BoundedSurfaceRolePrerequisite();
    }

private:

    void BoundedSurfaceRolePrerequisite()
    {
        const auto type = Schema(m_data).Find("GEOMETRICALLY_BOUNDED_SURFACE_SHAPE_REPRESENTATION");
        if (!type || m_data.populations[*type].empty())
        {
            return; // Existing inputs retain their dependency visits and order.
        }
        // Pinned AP214E3_2010.exp (71ab140f...), GBSR.WR7 is an existential
        // query over DIRECT GEOMETRIC_SET items and their DIRECT elements.
        // A surface elsewhere in the file, in a curve's dependencies or only
        // behind a mapped item cannot satisfy it. TYPEOF includes subtypes.
        // This necessary clause does not establish WR1--WR6, ownership, units
        // or native support: the later selected-representation guard remains.
        std::vector<signed char> containsSurface(m_data.types.size(), -1);
        for (const auto record : m_data.populations[*type])
        {
            const auto& bound = Field(record, "items");
            const auto& items = *bound.declaration.value;
            Count(items.span);
            bool found = false;
            for (const auto& item : items.children)
            {
                m_visit(item.span);
                const auto set = m_data.referenceTargets.at(&item);
                if (!IsA(set, "GEOMETRIC_SET"))
                {
                    continue;
                }
                auto& cached = containsSurface[set];
                if (cached < 0)
                {
                    cached = 0;
                    const auto& elements = *Field(set, "elements").declaration.value;
                    m_visit(elements.span);
                    for (const auto& member : elements.children)
                    {
                        m_visit(member.span);
                        cached = (IsA(m_data.referenceTargets.at(&member), "SURFACE") || cached == 1) ? 1 : 0;
                    }
                }
                // Repeated references still incur their own visit above;
                // shared sets are inspected once, with no recursive expansion.
                found = found || cached == 1;
            }
            if (!found)
            {
                Fail(
                    record,
                    bound,
                    items,
                    "GEOMETRICALLY_BOUNDED_SURFACE_SHAPE_REPRESENTATION.WR7",
                    "at least one direct GEOMETRIC_SET containing a SURFACE element",
                    "no direct surface-bearing set"
                );
            }
        }
    }

    const SemanticData& m_data;
    const std::function<void(Span)>& m_visit;
    GeometryDependencyCounts& m_checks;
    std::vector<std::size_t> m_curvePostorder;
    bool m_compositeWirePrepared = false;
    enum class PredicateKind
    {
        Dependency,
        Entity,
        Type
    };
    void CompositeChecks(std::span<const std::size_t> composites, std::span<const std::size_t> segments)
    {
        std::vector<std::size_t> uses(m_data.types.size(), 0);
        for (const auto curve : composites)
        {
            const auto& bound = Field(curve, "segments");
            const auto& value = *bound.declaration.value;
            Count(value.span);
            std::size_t discontinuities = 0;
            bool lastDiscontinuous = false;
            for (const auto& occurrence : value.children)
            {
                m_visit(occurrence.span);
                const auto segment = m_data.referenceTargets.at(&occurrence);
                ++uses[segment]; // Inverse BAG counts uses, not unique records or owners.
                const auto& transition = *Field(segment, "transition").declaration.value;
                m_visit(transition.span);
                lastDiscontinuous = transition.text == "DISCONTINUOUS";
                discontinuities += lastDiscontinuous;
            }
            // Fixed CC.WR1 derives closed from the LAST transition. This is a
            // declaration check, not a proof of geometric joints or material closure.
            if (discontinuities != std::size_t(lastDiscontinuous))
            {
                Fail(curve, bound, value, "COMPOSITE_CURVE.WR1", "only an optional final discontinuous transition", std::to_string(discontinuities));
            }
        }
        for (const auto segment : segments)
        {
            const auto& bound = Field(segment, "parent_curve");
            const auto& value = *bound.declaration.value;
            Count(value.span);
            if (!IsA(m_data.referenceTargets.at(&value), "BOUNDED_CURVE"))
            {
                // A full CIRCLE/ELLIPSE passes the wireframe predicate but is
                // not BOUNDED_CURVE. Keep its explicit TRIMMED_CURVE wrapper.
                Fail(segment, bound, value, "COMPOSITE_CURVE_SEGMENT.WR1", "BOUNDED_CURVE", "other CURVE");
            }
            Count(value.span, PredicateKind::Dependency);
            if (!uses[segment])
            {
                // INVERSE is not a transmitted slot. Anchor its lower-bound
                // failure at this segment's real parent field, without inventing
                // a P21 parameter path for using_curves.
                Fail(segment, bound, value, "COMPOSITE_CURVE_SEGMENT.using_curves", "BAG[1:?] inverse use", "0");
            }
        }
    }
    void CompositeClosure(std::span<const std::size_t> trimmed, std::span<const std::size_t> composites)
    {
        struct Frame
        {
            std::size_t record, next;
        };
        std::vector<unsigned char> state(m_data.types.size(), 0);
        std::vector<Frame> stack;
        const auto walk = [&](std::size_t root)
        {
            if (state[root] == 2)
            {
                return;
            }
            state[root] = 1;
            stack.push_back({ root, 0 });
            while (!stack.empty())
            {
                auto& frame = stack.back();
                const BoundAttribute* bound = nullptr;
                std::size_t owner = frame.record;
                std::optional<Span> throughSegment;
                if (IsA(frame.record, "TRIMMED_CURVE") && !frame.next)
                {
                    ++frame.next;
                    bound = &Field(frame.record, "basis_curve");
                }
                else if (IsA(frame.record, "COMPOSITE_CURVE"))
                {
                    const auto& occurrences = Field(frame.record, "segments").declaration.value->children;
                    if (frame.next < occurrences.size())
                    {
                        const auto& occurrence = occurrences[frame.next++];
                        m_visit(occurrence.span);
                        owner = m_data.referenceTargets.at(&occurrence);
                        bound = &Field(owner, "parent_curve");
                        throughSegment = occurrence.span;
                    }
                }
                if (!bound)
                {
                    state[frame.record] = 2;
                    m_curvePostorder.push_back(frame.record);
                    stack.pop_back();
                    continue;
                }
                const auto& value = *bound->declaration.value;
                Count(value.span, PredicateKind::Dependency);
                const auto target = m_data.referenceTargets.at(&value);
                if (state[target] == 1)
                {
                    // Only forward definition edges belong in this iterative
                    // DFS. Inverse users would create a false cycle for every
                    // ordinary segment. Memoized completion preserves shared
                    // DAGs/repeated occurrences without expanding their paths.
                    std::vector<Span> related;
                    if (throughSegment)
                    {
                        related.push_back(*throughSegment);
                    }
                    Fail(
                        owner,
                        *bound,
                        value,
                        "curve definition",
                        "finite curve definition graph",
                        "cycle",
                        {},
                        true,
                        "project necessary geometry dependency closure",
                        std::move(related)
                    );
                }
                if (state[target] == 0)
                {
                    state[target] = 1;
                    stack.push_back({ target, 0 });
                }
            }
        };
        for (const auto root : trimmed)
        {
            walk(root);
        }
        for (const auto root : composites)
        {
            walk(root);
        }
    }
    bool DirectWireCurve(std::size_t record) const
    {
        return IsA(record, "B_SPLINE_CURVE") || IsA(record, "CIRCLE") || IsA(record, "ELLIPSE");
    }
    void PrepareCompositeWireValidity(std::vector<signed char>& memo)
    {
        // Fixed valid_geometrically_bounded_wf_curve evaluated in the proven
        // definition postorder. Every LIST use is charged, but shared child
        // definitions are evaluated once; source depth never consumes C++ stack.
        for (const auto record : m_curvePostorder)
        {
            m_visit(m_data.source->Records()[record].span);
            bool valid = DirectWireCurve(record);
            if (IsA(record, "TRIMMED_CURVE"))
            {
                const auto& basis = *Field(record, "basis_curve").declaration.value;
                m_visit(basis.span);
                const auto target = m_data.referenceTargets.at(&basis);
                valid = IsA(target, "LINE") || memo[target] == 1;
            }
            if (IsA(record, "COMPOSITE_CURVE"))
            {
                valid = true;
                for (const auto& occurrence : Field(record, "segments").declaration.value->children)
                {
                    m_visit(occurrence.span);
                    const auto& parent = *Field(m_data.referenceTargets.at(&occurrence), "parent_curve").declaration.value;
                    m_visit(parent.span);
                    valid = (memo[m_data.referenceTargets.at(&parent)] == 1) && valid;
                }
            }
            memo[record] = valid ? 1 : 0;
        }
        m_compositeWirePrepared = true;
    }
    void TrimmedClosure(std::span<const std::size_t> roots)
    {
        if (roots.empty())
        {
            return;
        }
        // Entity-reference nesting has no lexical depth limit. Iterative
        // three-colour traversal proves finite basis dependencies without
        // recursing on an attacker-controlled chain or repeatedly walking it.
        std::vector<unsigned char> state(m_data.types.size(), 0);
        std::vector<std::size_t> path;
        for (const auto root : roots)
        {
            if (state[root] == 2)
            {
                continue;
            }
            path.clear();
            auto current = root;
            while (IsA(current, "TRIMMED_CURVE") && state[current] != 2)
            {
                const auto& bound = Field(current, "basis_curve");
                const auto& value = *bound.declaration.value;
                Count(value.span);
                if (state[current] == 1)
                {
                    Fail(
                        current,
                        bound,
                        value,
                        "TRIMMED_CURVE.basis_curve",
                        "finite basis dependency chain",
                        "cycle",
                        {},
                        true,
                        "project necessary geometry dependency closure"
                    );
                }
                state[current] = 1;
                path.push_back(current);
                current = m_data.referenceTargets.at(&value);
            }
            for (const auto record : path)
            {
                state[record] = 2;
            }
        }
    }
    bool IsA(std::size_t r, std::string_view type) const
    {
        const auto index = Schema(m_data).Find(type);
        return index && m_data.types[r].test(*index);
    }
    const BoundAttribute& Field(std::size_t r, std::string_view name) const
    {
        const auto& fields = m_data.fields[r];
        const auto found = fields.named.equal_range(name);
        // Names below are pinned descriptor fields after complete typed binding.
        // This is a catalog/programmer invariant, never a bad-STEP precondition.
        Expects(found.first != found.second && std::next(found.first) == found.second);
        return fields.attributes[found.first->second];
    }
    void Count(Span span, PredicateKind kind = PredicateKind::Entity)
    {
        m_visit(span); // Budget before decimal work or counting an attempted predicate.
        ++m_checks.total;
        if (kind == PredicateKind::Type)
        {
            ++m_checks.typePredicates;
        }
        if (kind == PredicateKind::Entity)
        {
            ++m_checks.entityPredicates;
        }
    }
    [[noreturn]] void Fail(
        std::size_t r,
        const BoundAttribute& bound,
        const Value& value,
        std::string_view clause,
        std::string expected,
        std::string actual,
        std::vector<std::size_t> suffix = {},
        bool dependency = false,
        std::string_view authorityOverride = {},
        std::vector<Span> related = {}
    )
    {
        const auto& record = m_data.source->Records()[r];
        const auto& field = bound.declaration;
        Diagnostic issue;
        issue.source.key = dependency ? "SourceGeometryDependencyViolation" : "SourceRuleViolation:" + std::string(clause);
        issue.source.stage = "D3";
        issue.source.message = m_data.profile.kind == ProfileKind::Ap203N916Source ?
                                   "Source geometry parameter violates the selected modified N916 prerequisite" :
                               m_data.profile.kind == ProfileKind::Ap203e2GeneralSource ?
                                   "Source geometry parameter violates the selected N2635 prerequisite" :
                                   "Source geometry parameter violates the selected AP214 prerequisite";
        issue.source.primary = value.span;
        issue.source.recordId = record.id;
        issue.source.component = record.components[bound.component].name;
        issue.source.related = std::move(related);
        issue.source.parameterPath = { bound.parameter + 1 };
        issue.source.parameterPath.insert(issue.source.parameterPath.end(), suffix.begin(), suffix.end());
        issue.classification = "SourceInvalid";
        issue.field = field.name;
        issue.declaredIn = field.declaredIn;
        issue.declaredType = field.declaredType;
        issue.expected = std::move(expected);
        issue.actual = std::move(actual);
        issue.responsibility = std::string(SourceArtifactName(m_data.profile.kind)) + " " + std::string(clause);
        if (!authorityOverride.empty())
        {
            issue.responsibility = authorityOverride;
        }
        throw Stop{ std::move(issue) };
    }
    bool ValidWireCurve(std::size_t record, std::vector<signed char>& memo)
    {
        if (!m_curvePostorder.empty() && !m_compositeWirePrepared)
        {
            PrepareCompositeWireValidity(memo);
        }
        // Fixed valid_geometrically_bounded_wf_curve, restricted to registered
        // branches. The preceding definition traversal has proved a finite graph.
        // A LINE is valid as a TRIMMED_CURVE basis, not as a bare set element.
        std::vector<std::size_t> path;
        auto current = record;
        while (memo[current] < 0)
        {
            m_visit(m_data.source->Records()[current].span);
            if (DirectWireCurve(current))
            {
                memo[current] = 1;
                break;
            }
            if (!IsA(current, "TRIMMED_CURVE"))
            {
                memo[current] = 0;
                break;
            }
            const auto& basis = *Field(current, "basis_curve").declaration.value;
            m_visit(basis.span);
            const auto next = m_data.referenceTargets.at(&basis);
            if (IsA(next, "LINE"))
            {
                memo[current] = 1;
                break;
            }
            path.push_back(current);
            current = next;
        }
        for (const auto parent : path)
        {
            memo[parent] = memo[current];
        }
        return memo[record] != 0;
    }
    void WireframeChecks()
    {
        const auto& catalog = Schema(m_data);
        const auto sets = catalog.Find("GEOMETRIC_CURVE_SET");
        const auto representations = catalog.Find("GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION");
        if (!sets || !representations)
        {
            return;
        }
        // Population traversal avoids another full-record pass and preserves
        // existing predicate/visit counts when this source has no wireframes.
        for (const auto record : m_data.populations[*sets])
        {
            const auto& bound = Field(record, "elements");
            const auto& value = *bound.declaration.value;
            Count(value.span);
            for (std::size_t i = 0; i < value.children.size(); ++i)
            {
                const auto& member = value.children[i];
                m_visit(member.span);
                if (IsA(m_data.referenceTargets.at(&member), "SURFACE"))
                {
                    Fail(record, bound, member, "GEOMETRIC_CURVE_SET.WR1", "POINT or CURVE", "SURFACE", { i + 1 });
                }
            }
        }
        if (m_data.populations[*representations].empty())
        {
            return;
        }
        std::vector<signed char> validCurves(m_data.types.size(), -1);
        constexpr std::string_view prefix = "GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION.";
        for (const auto record : m_data.populations[*representations])
        {
            const auto& bound = Field(record, "items");
            const auto& value = *bound.declaration.value;
            std::size_t curveSets = 0;
            Count(value.span); // WR1, followed by WR2, in source WHERE order.
            for (std::size_t i = 0; i < value.children.size(); ++i)
            {
                const auto& item = value.children[i];
                m_visit(item.span);
                const auto target = m_data.referenceTargets.at(&item);
                const auto categories = std::size_t(IsA(target, "GEOMETRIC_CURVE_SET")) + std::size_t(IsA(target, "AXIS2_PLACEMENT_3D"));
                if (categories != 1)
                {
                    Fail(record, bound, item, std::string(prefix) + "WR1", "one admitted GCS/AXIS2_PLACEMENT_3D category", "other RI", { i + 1 });
                }
                curveSets += IsA(target, "GEOMETRIC_CURVE_SET") ? 1 : 0;
            }
            Count(value.span);
            if (!curveSets)
            {
                Fail(record, bound, value, std::string(prefix) + "WR2", "at least one GEOMETRIC_CURVE_SET", "placements only");
            }
            // MAPPED_ITEM, POLYLINE and replicated geometry are not
            // registered here. Their WR6/WR7 branches cannot bypass the closed
            // type guard. Direct conics already require a 3D placement in the
            // bound SELECT; still check their actual WR5 below.
            for (const auto clause : { "WR3", "WR4", "WR5" })
            {
                Count(value.span);
                for (const auto& item : value.children)
                {
                    const auto target = m_data.referenceTargets.at(&item);
                    if (!IsA(target, "GEOMETRIC_CURVE_SET"))
                    {
                        continue;
                    }
                    const auto& members = Field(target, "elements");
                    const auto& elements = members.declaration.value->children;
                    for (std::size_t i = 0; i < elements.size(); ++i)
                    {
                        const auto& member = elements[i];
                        m_visit(member.span);
                        const auto geometry = m_data.referenceTargets.at(&member);
                        bool valid = true;
                        if (clause == std::string_view("WR3") && IsA(geometry, "CURVE"))
                        {
                            valid = ValidWireCurve(geometry, validCurves);
                        }
                        if (clause == std::string_view("WR4") && IsA(geometry, "POINT"))
                        {
                            valid = IsA(geometry, "CARTESIAN_POINT");
                        }
                        if (clause == std::string_view("WR5") && IsA(geometry, "CONIC"))
                        {
                            const auto& position = *Field(geometry, "position").declaration.value;
                            m_visit(position.span);
                            valid = IsA(m_data.referenceTargets.at(&position), "AXIS2_PLACEMENT_3D");
                        }
                        if (!valid)
                        {
                            // The primary path stays inside the owning GCS
                            // field; a reference hop is a related span, never a
                            // fictitious nested parameter index in the parent.
                            Fail(
                                target,
                                members,
                                member,
                                std::string(prefix) + clause,
                                "valid fixed-source wireframe predicate",
                                "FALSE",
                                { i + 1 },
                                false,
                                {},
                                { item.span }
                            );
                        }
                    }
                }
            }
        }
    }
    void Weights(std::size_t r, bool surface)
    {
        const auto& bound = Field(r, "weights_data");
        const auto& data = *bound.declaration.value;
        const auto& controls = *Field(r, "control_points_list").declaration.value;
        const std::string prefix = surface ? "RATIONAL_B_SPLINE_SURFACE" : "RATIONAL_B_SPLINE_CURVE";
        Count(data.span, PredicateKind::Entity);
        if (data.children.size() != controls.children.size())
        {
            Fail(r, bound, data, prefix + ".WR1", std::to_string(controls.children.size()), std::to_string(data.children.size()));
        }
        if (surface)
        {
            for (std::size_t i = 0; i < data.children.size(); ++i)
            {
                const auto& row = data.children[i];
                m_visit(row.span);
                if (row.children.size() != controls.children[0].children.size())
                {
                    // WR1 compares the first row. Subsequent ragged rows also
                    // cannot form the declared DERIVE array: reject, never pad,
                    // truncate or transpose weights to match control points.
                    Fail(
                        r,
                        bound,
                        row,
                        i ? prefix + ".weights DERIVE" : prefix + ".WR1",
                        std::to_string(controls.children[0].children.size()),
                        std::to_string(row.children.size()),
                        { i + 1 },
                        i != 0
                    );
                }
            }
        }
        Count(data.span, PredicateKind::Entity);
        const auto positive = [&](const Value& value, std::vector<std::size_t> path)
        {
            m_visit(value.span);
            if (CompareSourceReals(value.text, "0.") <= 0)
            {
                Fail(r, bound, value, prefix + ".WR2", ">0", value.text, std::move(path));
            }
        };
        // Both artifacts' *_weights_positive functions test each source REAL.
        // Weights are dimensionless; do not round, rescale or turn signed zero
        // into a positive value to let the target constructor accept it.
        for (std::size_t i = 0; i < data.children.size(); ++i)
        {
            if (surface)
            {
                m_visit(data.children[i].span);
                for (std::size_t j = 0; j < data.children[i].children.size(); ++j)
                {
                    positive(data.children[i].children[j], { i + 1, j + 1 });
                }
            }
            else
            {
                positive(data.children[i], { i + 1 });
            }
        }
    }
    void Direction(std::size_t r, std::size_t controls, const SplineDirection& direction)
    {
        const auto& mult = Field(r, direction.multiplicities);
        const auto& knots = Field(r, direction.knots);
        const auto& multiplicities = mult.declaration.value->children;
        const auto& values = knots.declaration.value->children;
        Count(mult.declaration.value->span);
        // Size clauses precede the parameter function's indexing; preserve the
        // distinct knot list and multiplicities without expansion/normalization.
        if (multiplicities.size() != values.size())
        {
            Fail(r, mult, *mult.declaration.value, direction.countClause, std::to_string(values.size()), std::to_string(multiplicities.size()));
        }
        const auto& degreeField = Field(r, direction.degree);
        const auto& degreeValue = *degreeField.declaration.value;
        Count(degreeValue.span);
        const auto degree = SourceIndexAtMost(degreeValue.text, controls - 1);
        if (!degree || *degree < 1)
        {
            Fail(r, degreeField, degreeValue, direction.parameterClause, "1 <= degree <= control count - 1", degreeValue.text);
        }
        // AP214 constraints_param_b_spline, pinned lines12267ff. End multiplicities
        // <=degree+1, interior <=degree; sum=control count+degree+1. Source node/byte
        // limits bound these size sums; large source integers are bounded before conversion.
        const auto required = controls + *degree + 1;
        std::size_t sum = 0;
        for (std::size_t i = 0; i < multiplicities.size(); ++i)
        {
            const auto& value = multiplicities[i];
            m_visit(value.span);
            const auto maximum = (i == 0 || i + 1 == multiplicities.size()) ? *degree + 1 : *degree;
            const auto n = SourceIndexAtMost(value.text, maximum);
            if (!n || *n < 1 || *n > required - sum)
            {
                Fail(r, mult, value, direction.parameterClause, "positive bounded knot multiplicity with required total", value.text, { i + 1 });
            }
            sum += *n;
        }
        if (sum != required)
        {
            Fail(r, mult, *mult.declaration.value, direction.parameterClause, std::to_string(required), std::to_string(sum));
        }
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            m_visit(values[i].span);
            if (i && CompareSourceReals(values[i - 1].text, values[i].text) >= 0)
            {
                Fail(r, knots, values[i], direction.parameterClause, "strictly increasing distinct source knots", values[i].text, { i + 1 });
            }
        }
    }
};
} // namespace
std::optional<Diagnostic> CheckAp214GeometryDependencies(
    const SemanticData& data,
    const std::function<void(step_source::Span)>& visit,
    GeometryDependencyCounts& checks
)
{
    try
    {
        GeometryDependencies(data, visit, checks).Run();
        return {};
    }
    catch (Stop& stop)
    {
        return std::move(stop.diagnostic);
    }
}
} // namespace step_semantics
