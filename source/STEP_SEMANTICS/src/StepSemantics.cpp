// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"
#include "StepGeometryDependencies.h"
#include "StepSchemaSelection.h"
#include "StepUnitDependencies.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace step_semantics
{
namespace
{
using step_source::Span;
using step_source::Value;
using step_source::ValueKind;
struct Failure
{
    Status status;
    Diagnostic diagnostic;
};
bool IsA(const SemanticData& data, std::size_t record, std::string_view name)
{
    const auto type = Schema(data).Find(name);
    return type && data.types.at(record).test(*type);
}
std::size_t IndexOf(const SemanticData& data, std::string_view id)
{
    const auto* record = data.source->Find(id);
    if (!record)
    {
        throw std::invalid_argument("Unknown source instance ID");
    }
    return static_cast<std::size_t>(record - data.source->Records().data());
}
std::size_t ResolveReference(const SemanticData& data, const Value& value)
{
    if (value.kind != ValueKind::Reference)
    {
        throw std::invalid_argument("Source slot is not an explicit reference");
    }
    const auto found = data.referenceTargets.find(&value);
    if (found == data.referenceTargets.end())
    {
        throw std::invalid_argument("Reference value is not owned by this source snapshot");
    }
    return found->second;
}
std::size_t FindFieldBinding(const SemanticData& data, std::size_t record, std::string_view name, std::optional<std::string_view> declaredIn = {})
{
    if (!data.bindingsComplete)
    {
        throw std::logic_error("Semantic field bindings are not complete");
    }
    const auto& bound = data.fields.at(record);
    const auto matches = bound.named.equal_range(name);
    if (!declaredIn)
    {
        if (matches.first == matches.second || std::next(matches.first) != matches.second)
        {
            throw std::invalid_argument("Missing or ambiguous source field");
        }
        return matches.first->second;
    }
    std::optional<std::size_t> found;
    for (auto at = matches.first; at != matches.second; ++at)
    {
        if (bound.attributes[at->second].declaration.declaredIn == *declaredIn)
        {
            if (found)
            {
                throw std::invalid_argument("Ambiguous declared source field");
            }
            found = at->second;
        }
    }
    if (!found)
    {
        throw std::invalid_argument("Missing declared source field");
    }
    return *found;
}
bool PositiveInteger(std::string_view text)
{
    if (!text.empty() && text.front() == '-')
    {
        return false;
    }
    return text.find_first_of("123456789") != std::string_view::npos;
}
bool Three(std::string_view text)
{
    if (!text.empty() && text.front() == '+')
    {
        text.remove_prefix(1);
    }
    const auto first = text.find_first_not_of('0');
    return first != std::string_view::npos && text.substr(first) == "3";
}

class Checker
{
public:

    Checker(std::shared_ptr<SemanticData> data, Outcome& outcome) : m_data(std::move(data)), m_outcome(outcome)
    {
    }
    void Run()
    {
        RunPhase(
            Phase::Binding,
            [&]
            {
                Binding();
            }
        );
        RunPhase(
            Phase::Types,
            [&]
            {
                Mapping();
                Fields();
            }
        );
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            RunPhase(
                Phase::UnitDependencies,
                [&]
                {
                    if (auto issue = CheckAp214UnitDependencies(
                            *m_data,
                            [this](Span span)
                            {
                                Visit(span);
                            },
                            m_outcome.unitDependencyChecks
                        ))
                    {
                        State(Phase::UnitDependencies) = FailureProgress(issue->status);
                        throw Failure{ issue->status, std::move(issue->diagnostic) };
                    }
                }
            );
            RunPhase(
                Phase::GeometryDependencies,
                [&]
                {
                    if (auto issue = CheckAp214GeometryDependencies(
                            *m_data,
                            [this](Span span)
                            {
                                Visit(span);
                            },
                            m_outcome.geometryDependencyChecks
                        ))
                    {
                        State(Phase::GeometryDependencies) = Progress::Rejected;
                        throw Failure{ Status::Rejected, std::move(*issue) };
                    }
                }
            );
        }
        RunPhase(
            Phase::Part,
            [&]
            {
                Product();
                Closure();
            }
        );
        RunPhase(
            Phase::Units,
            [&]
            {
                UnitContext();
            }
        );
        if (!m_data->units.lengthPower10Mm)
        {
            State(Phase::Units) = Progress::Deferred;
        }
        if (Interoperability())
        {
            RunPhase(
                Phase::Interoperability,
                [&]
                {
                    ApprovePreservedMetadata();
                }
            );
        }
    }
    void PublishProgress()
    {
        m_outcome.binding = ProgressName(State(Phase::Binding));
        m_outcome.types = ProgressName(State(Phase::Types));
        m_outcome.unitDependencies = ProgressName(State(Phase::UnitDependencies));
        m_outcome.geometryDependencies = ProgressName(State(Phase::GeometryDependencies));
        m_outcome.part = ProgressName(State(Phase::Part));
        m_outcome.siUnits = ProgressName(State(Phase::Units));
        m_outcome.interoperability = ProgressName(State(Phase::Interoperability));
        for (const auto& diagnostic : m_data->preservedDiagnostics)
        {
            const auto& key = diagnostic.source.key;
            if (key == "RequiredAttributeMissing" || key == "ReferenceTypeMismatch")
            {
                m_outcome.types = "CheckedWithDiagnostics";
            }
            else if (key == "UnsupportedProductCategory" || key == "UnsupportedDefinitionContextRole" || key == "UnownedOrOrphanProfileContent")
            {
                m_outcome.part = "CheckedWithDiagnostics";
            }
            else
            {
                m_outcome.unitDependencies = "CheckedWithDiagnostics";
            }
        }
        // A source-region deferral qualifies a completed ownership check. It
        // must never hide a later orphan failure or a traversal resource limit.
        if (State(Phase::Part) == Progress::Passed && !m_data->part.deferredRegionFaces.empty())
        {
            m_outcome.part = "Deferred";
        }
    }

private:

    enum class Phase
    {
        Binding,
        Types,
        UnitDependencies,
        GeometryDependencies,
        Part,
        Units,
        Interoperability
    };
    enum class Progress
    {
        NotRun,
        Running,
        Passed,
        Rejected,
        Unsupported,
        ResourceLimit,
        Deferred
    };
    std::shared_ptr<SemanticData> m_data;
    Outcome& m_outcome;
    std::optional<Phase> m_phase;
    std::array<Progress, 7> m_progress{};

    std::string SourceAuthority() const
    {
        // Used only after binding: attribute failures belong to the snapshot's pinned
        // source declarations, not the downstream rule profile or project restrictions.
        // Keep the existing AP203 labels so its diagnostic contract remains unchanged.
        Expects(m_data->catalog.has_value());
        switch (m_data->profile.kind)
        {
            case ProfileKind::Ap203e2Planar:
            case ProfileKind::Ap203e2GeneralSource:
                return "N2635";
            case ProfileKind::Ap214e3Source:
                return "AP214E3_2010.exp";
        }
        throw std::logic_error("Unknown source profile authority");
    }
    Progress& State(Phase phase)
    {
        return m_progress.at(static_cast<std::size_t>(phase));
    }
    void RunPhase(Phase phase, auto&& operation)
    {
        if (m_phase || State(phase) != Progress::NotRun)
        {
            throw std::logic_error("Semantic phase cannot be entered twice or recursively");
        }
        m_phase = phase;
        State(phase) = Progress::Running;
        operation();
        State(phase) = Progress::Passed;
        m_phase.reset();
    }
    static Progress FailureProgress(Status status)
    {
        switch (status)
        {
            case Status::Rejected:
                return Progress::Rejected;
            case Status::Unsupported:
                return Progress::Unsupported;
            case Status::ResourceLimit:
                return Progress::ResourceLimit;
            case Status::Checked:
            case Status::CheckedWithDiagnostics:
            case Status::Deferred:
            case Status::SourceFailure:
                break;
        }
        throw std::logic_error("Invalid semantic phase failure status");
    }
    static const char* ProgressName(Progress progress)
    {
        switch (progress)
        {
            case Progress::NotRun:
                return "NotRun";
            case Progress::Running:
                return "Running";
            case Progress::Passed:
                return "Passed";
            case Progress::Rejected:
                return "Rejected";
            case Progress::Unsupported:
                return "Unsupported";
            case Progress::ResourceLimit:
                return "ResourceLimit";
            case Progress::Deferred:
                return "Deferred";
        }
        throw std::logic_error("Unknown semantic phase progress");
    }
    std::vector<std::vector<std::size_t>> m_outgoing;
    std::vector<unsigned char> m_administrativeAttachments;
    std::size_t m_context = 0;
    std::size_t m_representation = 0;

    [[noreturn]] void Fail(
        std::string key,
        std::string stage,
        std::string message,
        Span span,
        Status status = Status::Rejected,
        std::optional<std::size_t> owner = {},
        std::size_t component = 0,
        std::vector<std::size_t> path = {},
        std::string expected = {},
        std::string actual = {},
        std::string responsibility = {}
    )
    {
        if (m_phase)
        {
            State(*m_phase) = FailureProgress(status);
        }
        Diagnostic diagnostic;
        diagnostic.classification = status == Status::Unsupported   ? "ProfileUnsupported" :
                                    status == Status::ResourceLimit ? "ResourceLimit" :
                                                                      "SourceInvalid";
        diagnostic.source.key = std::move(key);
        diagnostic.source.stage = std::move(stage);
        diagnostic.source.message = std::move(message);
        diagnostic.source.primary = span;
        diagnostic.source.parameterPath = path;
        diagnostic.expected = std::move(expected);
        diagnostic.actual = std::move(actual);
        diagnostic.responsibility = std::move(responsibility);
        if (owner)
        {
            const auto& record = m_data->source->Records()[*owner];
            diagnostic.source.recordId = record.id;
            diagnostic.source.component = record.components[component].name;
            if (!path.empty() && path[0] <= record.components[component].parameters.size())
            {
                const auto view = AttributeAt(*m_data, *owner, component, path[0] - 1);
                diagnostic.field = view.name;
                diagnostic.declaredIn = view.declaredIn;
                diagnostic.declaredType = view.declaredType;
            }
        }
        throw Failure{ status, std::move(diagnostic) };
    }
    void Visit(Span span)
    {
        if (++m_outcome.dependencyVisits > m_outcome.limits.dependencyVisits)
        {
            Fail("DependencyVisitLimit", "D2", "Configured semantic traversal budget exceeded", span, Status::ResourceLimit);
        }
    }
    bool Interoperability() const
    {
        return m_data->policy == SourcePolicy::PreserveNonGeometric && m_data->profile.kind != ProfileKind::Ap203e2Planar;
    }
    bool NonGeometricStyle(std::size_t record) const
    {
        // Closed declarations whose outgoing references stay in the style chain.
        // STYLED_ITEM is deliberately absent: it references a geometric RI and
        // must still pass the existing selected-body attachment proof.
        constexpr std::string_view names[] = {
            "PRESENTATION_STYLE_ASSIGNMENT", "SURFACE_STYLE_USAGE", "SURFACE_SIDE_STYLE", "SURFACE_STYLE_FILL_AREA", "FILL_AREA_STYLE",
            "FILL_AREA_STYLE_COLOUR",        "COLOUR_RGB"
        };
        return std::any_of(
            std::begin(names),
            std::end(names),
            [&](auto name)
            {
                return IsA(*m_data, record, name);
            }
        );
    }
    void Preserve(Failure failure)
    {
        // Capture the original strict diagnostic, not a replacement verdict. It
        // remains provisional until attachment and physical-dependency checks pass.
        m_data->pendingIssues.push_back({ failure.status, std::move(failure.diagnostic) });
        // Fail marked the phase terminal. Restore it only for this provisional
        // issue; the final proof still precedes every typed snapshot publication.
        State(*m_phase) = Progress::Running;
        if (m_data->pendingIssues.size() > m_outcome.limits.preservedDiagnostics)
        {
            Fail(
                "PreservedDiagnosticLimit",
                "D2",
                "Interoperability diagnostic capacity exceeded",
                m_data->pendingIssues.back().diagnostic.source.primary,
                Status::ResourceLimit
            );
        }
    }
    const step_source::Record& Record(std::size_t index) const
    {
        return m_data->source->Records().at(index);
    }
    const BoundAttribute& Binding(std::size_t record, std::string_view name) const
    {
        return m_data->fields.at(record).attributes.at(FindFieldBinding(*m_data, record, name));
    }
    const Value& FieldValue(std::size_t record, std::string_view name) const
    {
        return *Binding(record, name).declaration.value;
    }
    std::size_t Target(const Value& value) const
    {
        return ResolveReference(*m_data, value);
    }
    std::size_t UnboundTarget(const Value& value) const
    {
        return IndexOf(*m_data, value.text);
    }
    enum class FieldDetail
    {
        SpanOnly, // Preserve record/component but omit parameter and declaration details.
        AttributePath
    };
    [[noreturn]] void FieldFailure(
        std::string key,
        std::string stage,
        std::string message,
        Span span,
        Status status,
        std::size_t owner,
        std::string_view name,
        FieldDetail detail,
        std::string expected = {},
        std::string actual = {},
        std::string responsibility = {}
    )
    {
        const auto& bound = Binding(owner, name);
        Fail(
            std::move(key),
            std::move(stage),
            std::move(message),
            span,
            status,
            owner,
            bound.component,
            detail == FieldDetail::AttributePath ? std::vector<std::size_t>{ bound.parameter + 1 } : std::vector<std::size_t>{},
            std::move(expected),
            std::move(actual),
            std::move(responsibility)
        );
    }

    void Binding()
    {
        m_outcome.schemaSelection = SelectSchema(*m_data->source);
        // An explicit general-BRep request selects a separate source subset.
        // The exact N2635 HEADER identity stays unchanged; default P0 checking
        // must not acquire newly admitted geometry or management declarations.
        if (m_data->geometryScope == GeometryScope::GeneralBrep && m_outcome.schemaSelection->qualified &&
            m_outcome.schemaSelection->qualified->kind == ProfileKind::Ap203e2Planar)
        {
            m_outcome.schemaSelection->qualified->profile = Ap203GeneralSourceProfile;
            m_outcome.schemaSelection->qualified->kind = ProfileKind::Ap203e2GeneralSource;
        }
        const auto& identifiers = m_data->source->Header()[2].parameters[0].children;
        if (identifiers.size() != 1)
        {
            Fail(
                "UnsupportedSchemaBinding",
                "D2",
                "One precise schema name/OID is required",
                m_data->source->Header()[2].span,
                Status::Unsupported,
                {},
                0,
                {},
                "one N2635 identifier",
                std::to_string(identifiers.size()),
                "profile registry"
            );
        }
        // Header recognition alone cannot select an arbitrary type/rule implementation.
        const auto& literal = identifiers[0].text;
        if (!m_outcome.schemaSelection->qualified)
        {
            Fail(
                "UnsupportedSchemaBinding",
                "D2",
                "HEADER identifier is not bound to the fixed N2635 registry entry",
                identifiers[0].span,
                Status::Unsupported,
                {},
                0,
                {},
                std::string(SchemaName) + " {1 0 10303 403 2 1 2}",
                literal,
                "profile registry; no APD/year inference"
            );
        }
        m_data->profile = *m_outcome.schemaSelection->qualified;
        m_data->schemaIdentity = m_outcome.schemaSelection->identifiers.front();
        // Fix declaration identity before any membership bits, reference types or derived slots are bound.
        m_data->catalog.emplace(&Schema(m_data->profile.kind));
    }

    void Mapping()
    {
        const auto& catalog = Schema(*m_data);
        const auto& records = m_data->source->Records();
        m_data->types.resize(records.size());
        m_data->populations.resize(catalog.names.size());
        for (std::size_t r = 0; r < records.size(); ++r)
        {
            Visit(records[r].span);
            std::vector<std::string> explicitTypes;
            std::unordered_set<std::string> unique;
            for (std::size_t c = 0; c < records[r].components.size(); ++c)
            {
                const auto& part = records[r].components[c];
                if (!unique.insert(part.name).second)
                {
                    Fail(
                        "DuplicateComplexConstituent",
                        "D1",
                        "A partial entity occurs twice in one instance",
                        part.span,
                        Status::Rejected,
                        r,
                        c,
                        {},
                        "unique constituent",
                        part.name,
                        "external mapping"
                    );
                }
                const auto type = catalog.Find(part.name);
                if (!type)
                {
                    Fail(
                        records[r].complex ? "UnsupportedComplexConstituent" : "UnsupportedEntityType",
                        "D2",
                        "Required content is outside the closed source profile",
                        part.span,
                        Status::Unsupported,
                        r,
                        c,
                        {},
                        "registered profile entity",
                        part.name,
                        "closed type guard; unknown content is not skipped"
                    );
                }
                m_data->types[r] |= catalog.ancestors[*type];
                explicitTypes.push_back(part.name);
                if (!records[r].complex && !catalog.declarations[*type].simple)
                {
                    Fail(
                        part.name == "FACE_SURFACE" ? "UnsupportedFaceSubtype" : "UnsupportedEntityMapping",
                        "D2",
                        "This declaration is not an admitted simple leaf",
                        part.span,
                        Status::Unsupported,
                        r,
                        c,
                        {},
                        "admitted simple leaf",
                        part.name,
                        "profile mapping"
                    );
                }
            }
            if (records[r].complex)
            {
                if (catalog.HasSubtypeConflict(m_data->types[r]))
                {
                    Fail(
                        "ComplexSubtypeConflict",
                        "D1",
                        "Audited mutually exclusive subtype branches occur together",
                        records[r].span,
                        Status::Rejected,
                        r,
                        0,
                        {},
                        "compatible partial types",
                        "ONEOF conflict",
                        SourceAuthority() + " audited subtype groups"
                    );
                }
                if (!std::is_sorted(explicitTypes.begin(), explicitTypes.end()))
                {
                    Fail(
                        "ComplexConstituentOrderInvalid",
                        "D1",
                        "External mapping facets must follow ascending type-name order",
                        records[r].span,
                        Status::Rejected,
                        r,
                        0,
                        {},
                        "ascending type names",
                        "source order",
                        "Part21 external mapping"
                    );
                }
                for (std::size_t t = 0; t < catalog.names.size(); ++t)
                {
                    if (m_data->types[r].test(t) && !unique.count(catalog.names[t]))
                    {
                        Fail(
                            "MissingComplexConstituent",
                            "D1",
                            "An inherited partial entity is missing",
                            records[r].span,
                            Status::Rejected,
                            r,
                            0,
                            {},
                            catalog.names[t],
                            "absent",
                            "Part21 external mapping"
                        );
                    }
                }
                if (!catalog.AdmitsComplex(explicitTypes))
                {
                    Fail(
                        "UnsupportedComplexSignature",
                        "D2",
                        m_data->profile.kind == ProfileKind::Ap203e2GeneralSource ?
                            "Complete complex mapping is outside the selected N2635 general source signatures" :
                        m_data->profile.kind == ProfileKind::Ap214e3Source ?
                            "Complete complex mapping is outside the selected AP214 source signatures" :
                            "Complete complex mapping is outside the six admitted signatures",
                        records[r].span,
                        Status::Unsupported,
                        r,
                        0,
                        {},
                        m_data->profile.kind == ProfileKind::Ap203e2GeneralSource ? "registered N2635 general source signature" :
                        m_data->profile.kind == ProfileKind::Ap214e3Source        ? "registered AP214 source signature" :
                                                                                    "one of six signatures",
                        "other signature",
                        "profile mapping"
                    );
                }
            }
            for (std::size_t t = 0; t < catalog.names.size(); ++t)
            {
                if (m_data->types[r].test(t))
                {
                    m_data->populations[t].push_back(r);
                }
            }
        }
    }

    void Scalar(const Value& value, FieldKind kind, const Field& field, std::size_t r, std::size_t c, std::vector<std::size_t> path)
    {
        const auto mismatch = [&]
        {
            Fail(
                "AttributeTypeMismatch",
                "D1",
                "Serialized value does not match the declared field type",
                value.span,
                Status::Rejected,
                r,
                c,
                path,
                field.type,
                step_source::ValueKindName(value.kind),
                SourceAuthority() + " explicit attribute"
            );
        };
        switch (kind)
        {
            case FieldKind::String:
                if (value.kind != ValueKind::String)
                {
                    mismatch();
                }
                break;
            case FieldKind::Real:
                if (value.kind != ValueKind::Real)
                {
                    mismatch();
                }
                break;
            case FieldKind::Integer:
                if (value.kind != ValueKind::Integer)
                {
                    mismatch();
                }
                break;
            case FieldKind::Boolean:
                if (value.kind != ValueKind::Enumeration || (value.text != "T" && value.text != "F"))
                {
                    Fail(
                        "BooleanValueInvalid",
                        "D1",
                        "BOOLEAN accepts T/F, not LOGICAL U",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        "BOOLEAN T/F",
                        std::string(m_data->source->Raw(value.span)),
                        SourceAuthority() + " BOOLEAN"
                    );
                }
                break;
            case FieldKind::Enumeration:
            {
                if (value.kind != ValueKind::Enumeration)
                {
                    mismatch();
                }
                if (!Schema(*m_data).ContainsEnumeration(field.type, value.text))
                {
                    Fail(
                        "EnumerationValueInvalid",
                        "D1",
                        "Enumeration token is absent from the fixed " + SourceAuthority() + " type",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        field.type,
                        value.text,
                        "fixed " + SourceAuthority() + " enumeration"
                    );
                }
                break;
            }
            case FieldKind::Logical:
                if (value.kind != ValueKind::Enumeration || (value.text != "T" && value.text != "F" && value.text != "U"))
                {
                    Fail(
                        "LogicalValueInvalid",
                        "D1",
                        "LOGICAL accepts T/F/U without resolving unknown",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        "LOGICAL T/F/U",
                        std::string(m_data->source->Raw(value.span)),
                        SourceAuthority() + " LOGICAL"
                    );
                }
                break;
            case FieldKind::Reference:
                if (value.kind != ValueKind::Reference)
                {
                    mismatch();
                }
                if (!Schema(*m_data).MatchesReference(m_data->types.at(UnboundTarget(value)), field.type))
                {
                    try
                    {
                        Fail(
                            "ReferenceTypeMismatch",
                            "D1",
                            "Reference target does not instantiate the declared type/SELECT",
                            value.span,
                            Status::Rejected,
                            r,
                            c,
                            path,
                            field.type,
                            Record(UnboundTarget(value)).components[0].name,
                            SourceAuthority() + " explicit reference type"
                        );
                    }
                    catch (Failure& failure)
                    {
                        failure.diagnostic.source.targetId = value.text;
                        failure.diagnostic.source.related.push_back(Record(UnboundTarget(value)).idSpan);
                        // N2635 APPROVED_ITEM excludes PD/formation, although legacy
                        // CC exports use those targets. Preserve the exact mismatch
                        // provisionally ONLY for these administrative uses; selected
                        // part identity and absence from physical dependencies must
                        // be proved before the snapshot is allowed to publish.
                        const auto target = UnboundTarget(value);
                        if (!Interoperability() || m_data->profile.kind != ProfileKind::Ap203e2GeneralSource || field.type != "APPROVED_ITEM" ||
                            !IsA(*m_data, r, "CC_DESIGN_APPROVAL") ||
                            (!IsA(*m_data, target, "PRODUCT_DEFINITION") && !IsA(*m_data, target, "PRODUCT_DEFINITION_FORMATION")))
                        {
                            throw;
                        }
                        Preserve(std::move(failure));
                    }
                }
                m_data->referenceTargets.emplace(&value, UnboundTarget(value));
                break;
            case FieldKind::Measure:
                if (value.kind != ValueKind::Typed || value.children.size() != 1)
                {
                    mismatch();
                }
                if (!Schema(*m_data).AdmitsMeasure(value.text))
                {
                    Fail(
                        "UnsupportedMeasureBranch",
                        "D2",
                        m_data->profile.kind == ProfileKind::Ap214e3Source ?
                            "This source profile consumes the registered length/mass/ratio measure families" :
                            "This profile consumes only the registered length measure family",
                        value.span,
                        Status::Unsupported,
                        r,
                        c,
                        path,
                        m_data->profile.kind == ProfileKind::Ap214e3Source ? "registered typed length/mass/ratio measure" : "typed length measure",
                        value.text,
                        "profile measure SELECT guard"
                    );
                }
                // N2635 count_measure/numeric_measure are NUMBER, unlike the
                // existing length/angle REAL branches. Retain INTEGER lexemes.
                if (value.children[0].kind != ValueKind::Real &&
                    !(m_data->profile.kind == ProfileKind::Ap203e2GeneralSource &&
                      (value.text == "COUNT_MEASURE" || value.text == "NUMERIC_MEASURE") && value.children[0].kind == ValueKind::Integer))
                {
                    mismatch();
                }
                break;
            case FieldKind::Aggregate:
                throw std::logic_error("Nested aggregate descriptor not expected");
        }
    }

    void AggregateValue(
        const Value& value,
        const Field& field,
        std::size_t r,
        std::size_t c,
        const std::vector<std::size_t>& path,
        std::string_view declaredIn,
        std::size_t depth = 0
    )
    {
        // Validate each declared LIST dimension without flattening source rows. The
        // outer field/member visit and error order is unchanged for existing AP203;
        // only nested fields recurse, adding their actual one-based source indices.
        const auto* inner = depth ? &field.nestedAggregates.at(depth - 1) : nullptr;
        const auto minimum = inner ? inner->minimum : field.minimum;
        const auto maximum = inner ? inner->maximum.value_or(static_cast<std::size_t>(-1)) : field.maximum;
        const auto unique = inner ? inner->uniqueElements : field.unique != Unique::None;
        const auto set = inner ? inner->aggregate == "SET" : field.unique == Unique::Set;
        if (value.kind != ValueKind::Aggregate)
        {
            Fail(
                "AttributeTypeMismatch",
                "D1",
                "Expected an aggregate value",
                value.span,
                Status::Rejected,
                r,
                c,
                path,
                "aggregate",
                step_source::ValueKindName(value.kind),
                SourceAuthority() + " aggregate"
            );
        }
        if (value.children.size() < minimum || value.children.size() > maximum)
        {
            Fail(
                "AggregateCardinalityViolation",
                "D1",
                "Aggregate cardinality violates the declared bounds",
                value.span,
                Status::Rejected,
                r,
                c,
                path,
                "declared bounds",
                std::to_string(value.children.size()),
                SourceAuthority() + " aggregate"
            );
        }
        std::unordered_map<std::string, Span> members;
        for (std::size_t i = 0; i < value.children.size(); ++i)
        {
            const auto& child = value.children[i];
            auto childPath = path;
            childPath.push_back(i + 1);
            Visit(child.span);
            if (depth < field.nestedAggregates.size())
            {
                AggregateValue(child, field, r, c, childPath, declaredIn, depth + 1);
            }
            else
            {
                Scalar(child, field.elementKind, field, r, c, childPath);
            }
            if (unique && !members.emplace(child.text, child.span).second)
            {
                try
                {
                    Fail(
                        set ? "SetDuplicateMember" : "AggregateUniqueViolation",
                        "D1",
                        "Aggregate repeats an entity identity",
                        child.span,
                        Status::Rejected,
                        r,
                        c,
                        childPath,
                        "unique instance identity",
                        child.text,
                        std::string(declaredIn) + "." + field.name
                    );
                }
                catch (Failure& failure)
                {
                    failure.diagnostic.source.related.push_back(members.at(child.text));
                    throw;
                }
            }
        }
    }

    void Fields()
    {
        const auto& catalog = Schema(*m_data);
        m_data->fields.resize(m_data->source->Records().size());
        for (std::size_t r = 0; r < m_data->source->Records().size(); ++r)
        {
            const auto& record = Record(r);
            for (std::size_t c = 0; c < record.components.size(); ++c)
            {
                const auto& part = record.components[c];
                const auto type = catalog.ids.at(part.name);
                const auto count = record.complex ? catalog.declarations[type].fields.size() : catalog.flattened[type].size();
                if (part.parameters.size() != count)
                {
                    Fail(
                        "AttributeCountMismatch",
                        "D1",
                        "Explicit attribute slot count does not match this mapping",
                        part.span,
                        Status::Rejected,
                        r,
                        c,
                        {},
                        std::to_string(count),
                        std::to_string(part.parameters.size()),
                        SourceAuthority() + " attribute layout"
                    );
                }
                for (std::size_t p = 0; p < count; ++p)
                {
                    const auto view = AttributeAt(*m_data, r, c, p);
                    const auto& value = *view.value;
                    const auto& field = record.complex ? catalog.declarations[type].fields[p] : *catalog.flattened[type][p].field;
                    const std::vector<std::size_t> path{ p + 1 };
                    Visit(value.span);
                    auto& bound = m_data->fields[r];
                    const auto index = bound.attributes.size();
                    bound.attributes.push_back(
                        { view,
                          c,
                          p,
                          field.kind == FieldKind::Aggregate && field.elementKind == FieldKind::Reference && field.nestedAggregates.empty() }
                    );
                    bound.named.emplace(view.name, index);
                    if (view.derivedSlot)
                    {
                        if (value.kind != ValueKind::Derived)
                        {
                            Fail(
                                "DerivedSlotMarkerRequired",
                                "D1",
                                "Redeclared explicit slot must contain '*'",
                                value.span,
                                Status::Rejected,
                                r,
                                c,
                                path,
                                "*",
                                std::string(m_data->source->Raw(value.span)),
                                "explicit-to-derived redeclaration"
                            );
                        }
                        continue;
                    }
                    if (value.kind == ValueKind::Unset)
                    {
                        if (field.optional)
                        {
                            continue;
                        }
                        try
                        {
                            Fail(
                                "RequiredAttributeMissing",
                                "D1",
                                "A required attribute is missing; empty string is a distinct value",
                                value.span,
                                Status::Rejected,
                                r,
                                c,
                                path,
                                field.type,
                                "$",
                                SourceAuthority() + " required attribute"
                            );
                        }
                        catch (Failure& failure)
                        {
                            // Only RI/REP labels are nondefining. Keep '$' in the
                            // original Value; axes, points, references and units
                            // never acquire a default through this policy.
                            if (!Interoperability() || field.name != "name" || field.kind != FieldKind::String ||
                                (!IsA(*m_data, r, "REPRESENTATION_ITEM") && !IsA(*m_data, r, "REPRESENTATION") && !NonGeometricStyle(r)))
                            {
                                throw;
                            }
                            Preserve(std::move(failure));
                            continue;
                        }
                    }
                    if (field.kind != FieldKind::Aggregate)
                    {
                        Scalar(value, field.kind, field, r, c, path);
                        continue;
                    }
                    AggregateValue(value, field, r, c, path, view.declaredIn);
                }
            }
        }
        m_data->bindingsComplete = true;
    }

    const std::vector<std::size_t>& Population(std::string_view name) const
    {
        return m_data->populations[Schema(*m_data).ids.at(std::string(name))];
    }
    std::size_t One(std::string_view type)
    {
        const auto& members = Population(type);
        if (members.size() != 1)
        {
            const auto span = members.empty() ? m_data->source->Header()[2].span : Record(members.back()).idSpan;
            const auto owner = members.empty() ? std::nullopt : std::optional<std::size_t>(members.back());
            std::string ids;
            for (auto r : members)
            {
                ids += "#" + Record(r).id + " ";
            }
            try
            {
                Fail(
                    "UnsupportedSinglePartGraph",
                    "D2",
                    "The selected profile requires a unique product/representation chain",
                    span,
                    Status::Unsupported,
                    owner,
                    0,
                    {},
                    std::string(type) + " count=1",
                    std::to_string(members.size()) + " " + ids,
                    "single-part profile guard; not PRODUCT.id uniqueness"
                );
            }
            catch (Failure& failure)
            {
                if (members.size() > 1)
                {
                    failure.diagnostic.source.related.push_back(Record(members[0]).idSpan);
                }
                throw;
            }
        }
        return members[0];
    }
    void Link(std::size_t owner, std::string_view name, std::size_t target, std::optional<std::size_t> member = {})
    {
        const auto& value = FieldValue(owner, name);
        const auto& field = member ? value.children.at(*member) : value;
        if (Target(field) != target)
        {
            FieldFailure(
                "UnsupportedSinglePartGraph",
                "D2",
                "Product chain roles do not refer to the same source definition",
                field.span,
                Status::Unsupported,
                owner,
                name,
                FieldDetail::SpanOnly,
                Record(target).id,
                field.text,
                "single-part identity guard"
            );
        }
    }
    struct SourceRepresentations
    {
        std::size_t definition;
        std::size_t body;
        std::optional<std::size_t> relationship;
    };
    SourceRepresentations SelectAp214Representations(std::size_t sdr)
    {
        const auto root = Target(FieldValue(sdr, "used_representation"));
        SourceRepresentations selected{ root, root, {} };
        const auto isBody = [&](std::size_t record)
        {
            return IsA(*m_data, record, "ADVANCED_BREP_SHAPE_REPRESENTATION") || IsA(*m_data, record, "MANIFOLD_SURFACE_SHAPE_REPRESENTATION");
        };
        const auto unsupported = [&](std::size_t owner, const char* message)
        {
            Fail(
                "UnsupportedRepresentationGraph",
                "D2",
                message,
                Record(owner).span,
                Status::Unsupported,
                owner,
                0,
                {},
                {},
                {},
                "profile shape/presentation graph guard"
            );
        };
        const auto& relationships = Population("SHAPE_REPRESENTATION_RELATIONSHIP");
        if (isBody(root))
        {
            if (!relationships.empty())
            {
                unsupported(relationships[0], "A direct body with additional shape relationships is outside this subset");
            }
        }
        else
        {
            if (!IsA(*m_data, root, "SHAPE_REPRESENTATION") || relationships.size() != 1)
            {
                unsupported(root, "The source root requires one unambiguous shape relationship to its body");
            }
            const auto relationship = relationships[0];
            Visit(Record(relationship).span);
            const auto first = Target(FieldValue(relationship, "rep_1")), second = Target(FieldValue(relationship, "rep_2"));
            if (first == root && second != root)
            {
                selected.body = second;
            }
            else if (second == root && first != root)
            {
                selected.body = first;
            }
            else
            {
                unsupported(relationship, "The shape relationship does not join the selected root to a distinct body");
            }
            if (!isBody(selected.body))
            {
                unsupported(relationship, "The linked representation is not an admitted exact BRep body");
            }
            // AP214E3_2010.exp, SHAPE_REPRESENTATION_RELATIONSHIP inherits rep_1/rep_2
            // without a transformation operator. Preserve both directions and identities;
            // this project subset requires the same context instead of guessing a mapping.
            if (Target(FieldValue(root, "context_of_items")) != Target(FieldValue(selected.body, "context_of_items")))
            {
                unsupported(relationship, "Linked shape representations require the identical source context in this subset");
            }
            for (const auto& value : FieldValue(root, "items").children)
            {
                Visit(value.span);
                if (!IsA(*m_data, Target(value), "AXIS2_PLACEMENT_3D"))
                {
                    unsupported(root, "The source wrapper may contain only retained placement items");
                }
            }
            selected.relationship = relationship;
        }
        for (const auto representation : Population("REPRESENTATION"))
        {
            Visit(Record(representation).span);
            // A plain representation is only a metadata candidate here. Closure
            // grants ownership only after proving its property link and part.
            if (representation != selected.definition && representation != selected.body &&
                !IsA(*m_data, representation, "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION") && !IsPlainRepresentation(representation))
            {
                unsupported(representation, "Additional representation is outside the selected root/body/presentation graph");
            }
        }
        return selected;
    }
    bool IsPlainRepresentation(std::size_t record) const
    {
        return Record(record).components.size() == 1 && Record(record).components.front().name == "REPRESENTATION";
    }
    void Product()
    {
        const auto ac = One("APPLICATION_CONTEXT"), apd = One("APPLICATION_PROTOCOL_DEFINITION"), pc = One("PRODUCT_CONTEXT");
        const auto pdc = One("PRODUCT_DEFINITION_CONTEXT"), product = One("PRODUCT"), formation = One("PRODUCT_DEFINITION_FORMATION");
        const auto pd = One("PRODUCT_DEFINITION"), pds = One("PRODUCT_DEFINITION_SHAPE"), sdr = One("SHAPE_DEFINITION_REPRESENTATION");
        std::size_t definitionRepresentation = 0;
        std::optional<std::size_t> shapeRelationship;
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            // Source SDR selects the root; presentation order and representation counts
            // cannot select the body's geometry or override its unit context.
            const auto selected = SelectAp214Representations(sdr);
            definitionRepresentation = selected.definition;
            m_representation = selected.body;
            shapeRelationship = selected.relationship;
            m_context = Target(FieldValue(m_representation, "context_of_items"));
        }
        else
        {
            m_representation = One("REPRESENTATION");
            m_context = One("REPRESENTATION_CONTEXT");
            definitionRepresentation = m_representation;
        }
        m_data->part.roles = { { "application_context", ac },
                               { "application_protocol", apd },
                               { "product_context", pc },
                               { "definition_context", pdc },
                               { "product", product },
                               { "formation", formation },
                               { "definition", pd },
                               { "product_shape", pds },
                               { "shape_definition_representation", sdr },
                               { "representation", m_representation },
                               { "unit_context", m_context } };
        if (shapeRelationship)
        {
            m_data->part.roles.push_back({ "definition_representation", definitionRepresentation });
            m_data->part.roles.push_back({ "shape_representation_relationship", *shapeRelationship });
        }
        Link(apd, "application", ac);
        Link(pc, "frame_of_reference", ac);
        Link(pdc, "frame_of_reference", ac);
        if (FieldValue(product, "frame_of_reference").children.size() != 1)
        {
            FieldFailure(
                "UnsupportedSinglePartGraph",
                "D2",
                "Only one product context is supported",
                FieldValue(product, "frame_of_reference").span,
                Status::Unsupported,
                product,
                "frame_of_reference",
                FieldDetail::SpanOnly
            );
        }
        Link(product, "frame_of_reference", pc, 0);
        Link(formation, "of_product", product);
        Link(pd, "formation", formation);
        Link(pd, "frame_of_reference", pdc);
        Link(pds, "definition", pd);
        Link(sdr, "definition", pds);
        Link(sdr, "used_representation", definitionRepresentation);
        Link(m_representation, "context_of_items", m_context);
        m_data->part.apdStatus = FieldValue(apd, "status").text;
        m_data->part.apdSchemaClaim = FieldValue(apd, "application_interpreted_model_schema_name").text;
        m_data->part.apdYear = FieldValue(apd, "application_protocol_year").text; // APD global RULE is ticket 04; never substitute HEADER text here.
        if (IsA(*m_data, pc, "MECHANICAL_CONTEXT") && FieldValue(pc, "discipline_type").text != "mechanical")
        {
            FieldFailure(
                "SourceRuleViolation:MECHANICAL_CONTEXT.WR1",
                "D3",
                "Legacy mechanical context discipline is invalid",
                FieldValue(pc, "discipline_type").span,
                Status::Rejected,
                pc,
                "discipline_type",
                FieldDetail::AttributePath,
                "mechanical",
                FieldValue(pc, "discipline_type").text,
                "MECHANICAL_CONTEXT.WR1"
            );
        }
        if (IsA(*m_data, pdc, "DESIGN_CONTEXT") && FieldValue(pdc, "life_cycle_stage").text != "design")
        {
            FieldFailure(
                "SourceRuleViolation:DESIGN_CONTEXT.WR1",
                "D3",
                "Legacy design context lifecycle is invalid",
                FieldValue(pdc, "life_cycle_stage").span,
                Status::Rejected,
                pdc,
                "life_cycle_stage",
                FieldDetail::AttributePath,
                "design",
                FieldValue(pdc, "life_cycle_stage").text,
                "DESIGN_CONTEXT.WR1"
            );
        }
        if (FieldValue(pc, "discipline_type").text != "mechanical")
        {
            FieldFailure(
                "UnsupportedProductContextDiscipline",
                "D2",
                "Product discipline is outside this profile",
                FieldValue(pc, "discipline_type").span,
                Status::Unsupported,
                pc,
                "discipline_type",
                FieldDetail::AttributePath,
                "mechanical",
                FieldValue(pc, "discipline_type").text,
                "profile role guard"
            );
        }
        if (FieldValue(pdc, "name").text != "part definition")
        {
            try
            {
                FieldFailure(
                    "UnsupportedDefinitionContextRole",
                    "D2",
                    "Definition context is outside the single-part profile",
                    FieldValue(pdc, "name").span,
                    Status::Unsupported,
                    pdc,
                    "name",
                    FieldDetail::AttributePath,
                    "part definition",
                    FieldValue(pdc, "name").text,
                    "profile role guard"
                );
            }
            catch (Failure& failure)
            {
                // The unique selected PD->PDC link was already checked above.
                // Its administrative label cannot choose or transform geometry.
                if (!Interoperability())
                {
                    throw;
                }
                Preserve(std::move(failure));
            }
        }
        if (FieldValue(pdc, "life_cycle_stage").text != "design")
        {
            FieldFailure(
                "UnsupportedDefinitionLifecycleStage",
                "D2",
                "Definition lifecycle is outside this profile",
                FieldValue(pdc, "life_cycle_stage").span,
                Status::Unsupported,
                pdc,
                "life_cycle_stage",
                FieldDetail::AttributePath,
                "design",
                FieldValue(pdc, "life_cycle_stage").text,
                "profile role guard"
            );
        }
        const std::unordered_set<std::string> specialNames{ "visual appearance", "tactile appearance", "contact ratio",
                                                            "hardness",          "treatment result",   "surface texture" };
        if (specialNames.count(FieldValue(pds, "name").text))
        {
            FieldFailure(
                "UnsupportedShapePurpose",
                "D2",
                "Special surface-condition purpose is not supported",
                FieldValue(pds, "name").span,
                Status::Unsupported,
                pds,
                "name",
                FieldDetail::AttributePath
            );
        }
        if (FieldValue(m_representation, "name").text == "centre of mass" || FieldValue(m_representation, "name").text == "treatment result")
        {
            FieldFailure(
                "UnsupportedRepresentationPurpose",
                "D2",
                "Special representation purpose is not supported",
                FieldValue(m_representation, "name").span,
                Status::Unsupported,
                m_representation,
                "name",
                FieldDetail::AttributePath
            );
        }
        const auto& categories = Population("PRODUCT_RELATED_PRODUCT_CATEGORY");
        if (categories.size() > 1)
        {
            Fail(
                "UnsupportedProductCategory",
                "D2",
                "Multiple categories are outside this profile",
                Record(categories[1]).span,
                Status::Unsupported,
                categories[1]
            );
        }
        if (!categories.empty())
        {
            const auto category = categories[0];
            const auto& products = FieldValue(category, "products");
            if (products.children.size() != 1 || Target(products.children[0]) != product || FieldValue(category, "name").text != "part")
            {
                try
                {
                    FieldFailure(
                        "UnsupportedProductCategory",
                        "D2",
                        "Category name/products are outside the single-part guard",
                        FieldValue(category, "name").span,
                        Status::Unsupported,
                        category,
                        "name",
                        FieldDetail::AttributePath,
                        "part for this product",
                        FieldValue(category, "name").text,
                        "profile category guard"
                    );
                }
                catch (Failure& failure)
                {
                    // An arbitrary source category label is not geometry, but
                    // membership must still be exactly the selected product.
                    if (!Interoperability() || products.children.size() != 1 || Target(products.children[0]) != product)
                    {
                        throw;
                    }
                    Preserve(std::move(failure));
                }
            }
            m_data->part.roles.push_back({ "category", category });
        }
        std::vector<std::size_t> shapes;
        for (const auto& item : FieldValue(m_representation, "items").children)
        {
            const auto target = Target(item);
            if (IsA(*m_data, target, "AXIS2_PLACEMENT_3D"))
            {
                continue;
            }
            const bool solid = IsA(*m_data, m_representation, "ADVANCED_BREP_SHAPE_REPRESENTATION");
            if (!IsA(*m_data, target, solid ? "MANIFOLD_SOLID_BREP" : "SHELL_BASED_SURFACE_MODEL"))
            {
                FieldFailure(
                    "UnsupportedRepresentationItem",
                    "D2",
                    "Only the declared shape and auxiliary placements are supported",
                    item.span,
                    Status::Unsupported,
                    m_representation,
                    "items",
                    FieldDetail::AttributePath,
                    "one MSB/SBSM",
                    Record(target).components[0].name,
                    "profile shape guard"
                );
            }
            shapes.push_back(target);
        }
        if (shapes.size() != 1)
        {
            FieldFailure(
                "UnsupportedShapeMultiplicity",
                "D2",
                "Exactly one shape item is required",
                FieldValue(m_representation, "items").span,
                Status::Unsupported,
                m_representation,
                "items",
                FieldDetail::SpanOnly
            );
        }
        const auto shape = shapes[0];
        m_data->part.roles.push_back({ "shape", shape });
        std::size_t shell = 0;
        if (IsA(*m_data, shape, "MANIFOLD_SOLID_BREP"))
        {
            shell = Target(FieldValue(shape, "outer"));
            m_data->part.shapeRole = "MaterialSolid";
        }
        else
        {
            if (FieldValue(shape, "sbsm_boundary").children.size() != 1)
            {
                FieldFailure(
                    "UnsupportedShapeMultiplicity",
                    "D2",
                    "One source shell is required",
                    FieldValue(shape, "sbsm_boundary").span,
                    Status::Unsupported,
                    shape,
                    "sbsm_boundary",
                    FieldDetail::SpanOnly
                );
            }
            shell = Target(FieldValue(shape, "sbsm_boundary").children[0]);
            m_data->part.shapeRole = IsA(*m_data, shell, "CLOSED_SHELL") ? "ClosedSurfaceModel" : "OpenSurfaceModel";
        }
        m_data->part.roles.push_back({ "shell", shell });
        for (const auto face : Population("FACE"))
        {
            std::size_t outer = 0;
            for (const auto& bound : FieldValue(face, "bounds").children)
            {
                outer += IsA(*m_data, Target(bound), "FACE_OUTER_BOUND") ? 1 : 0;
            }
            if (outer != 1)
            {
                if (outer == 0 && m_data->profile.kind != ProfileKind::Ap203e2Planar && m_data->geometryScope == GeometryScope::GeneralBrep)
                {
                    // Ordinary FACE_BOUND is legal source data. Do not guess its
                    // role or silently change the source type: general native
                    // construction must classify all regions before publication.
                    m_data->part.deferredRegionFaces.push_back(face);
                    continue;
                }
                FieldFailure(
                    "UnsupportedExplicitOuterBoundRequired",
                    "D2",
                    "This profile requires one explicit FACE_OUTER_BOUND",
                    FieldValue(face, "bounds").span,
                    Status::Unsupported,
                    face,
                    "bounds",
                    FieldDetail::AttributePath,
                    "one explicit outer bound",
                    std::to_string(outer),
                    "profile face-region guard"
                );
            }
        }
    }

    std::vector<unsigned char> Reach(std::vector<std::size_t> pending, bool riOnly)
    {
        std::vector<unsigned char> visited(m_data->types.size(), 0);
        while (!pending.empty())
        {
            const auto r = pending.back();
            pending.pop_back();
            if (visited[r])
            {
                continue;
            }
            visited[r] = 1;
            Visit(Record(r).span);
            for (auto index : m_outgoing[r])
            {
                const auto& reference = m_data->source->References()[index];
                Visit(reference.span);
                if (!riOnly || IsA(*m_data, reference.targetRecordIndex, "REPRESENTATION_ITEM"))
                {
                    pending.push_back(reference.targetRecordIndex);
                }
            }
        }
        return visited;
    }
    void AttachAp214Properties()
    {
        // Product() already proved one selected PD/PDS and their identity link.
        // AP214E3_2010.exp PDR/PD fields supply source references; allowing these
        // metadata roots is a project attachment guard, not full EXPRESS WHERE.
        const auto& definitions = Population("PRODUCT_DEFINITION");
        const auto& shapes = Population("PRODUCT_DEFINITION_SHAPE");
        Expects(definitions.size() == 1 && shapes.size() == 1);
        const auto pd = definitions.front(), pds = shapes.front();
        std::vector<Part::PropertyAttachment> attached;
        const auto reject = [&](std::size_t owner,
                                std::string_view field,
                                const Value& at,
                                const char* key,
                                const char* expected,
                                std::optional<std::size_t> item = {})
        {
            const auto& bound = Binding(owner, field);
            std::vector<std::size_t> path{ bound.parameter + 1 };
            if (item)
            {
                path.push_back(*item);
            }
            Fail(
                key,
                "D2",
                "Property metadata is outside the selected-part attachment subset",
                at.span,
                Status::Unsupported,
                owner,
                bound.component,
                std::move(path),
                expected,
                at.text,
                "profile property attachment guard; not complete EXPRESS WHERE"
            );
        };
        for (const auto link : Population("PROPERTY_DEFINITION_REPRESENTATION"))
        {
            if (IsA(*m_data, link, "SHAPE_DEFINITION_REPRESENTATION"))
            {
                continue; // The SDR is the existing geometry root, not a metadata link.
            }
            ++m_outcome.propertyAttachmentChecks;
            Visit(Record(link).span);
            const auto& definition = FieldValue(link, "definition");
            Visit(definition.span);
            const auto property = Target(definition);
            const auto& characterized = FieldValue(property, "definition");
            Visit(characterized.span);
            const auto owner = Target(characterized);
            if (owner != pd && owner != pds)
            {
                reject(property, "definition", characterized, "UnsupportedPropertyAttachment", "selected PD or PDS identity");
            }
            const auto& used = FieldValue(link, "used_representation");
            Visit(used.span);
            const auto representation = Target(used);
            if (!IsPlainRepresentation(representation))
            {
                reject(link, "used_representation", used, "UnsupportedPropertyRepresentation", "plain property REPRESENTATION");
            }
            const auto& items = FieldValue(representation, "items").children;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto& value = items[i];
                Visit(value.span);
                const auto target = Target(value);
                // Restrict the entire metadata item boundary, not just its name.
                // MRI can reference only units; DRI has text. Neither can smuggle
                // geometry in through a style, placement or another BRep shape.
                const bool valueItem = m_data->profile.kind == ProfileKind::Ap203e2GeneralSource && IsA(*m_data, target, "VALUE_REPRESENTATION_ITEM");
                if (!IsA(*m_data, target, "MEASURE_REPRESENTATION_ITEM") && !IsA(*m_data, target, "DESCRIPTIVE_REPRESENTATION_ITEM") && !valueItem)
                {
                    reject(representation, "items", value, "UnsupportedPropertyItem", "measure or descriptive item", i + 1);
                }
                if (valueItem && !IsA(*m_data, Target(FieldValue(representation, "context_of_items")), "GLOBAL_UNIT_ASSIGNED_CONTEXT"))
                {
                    FieldFailure(
                        "SourceRuleViolation:VALUE_REPRESENTATION_ITEM.WR1",
                        "D3",
                        "Value items require an assigned unit context",
                        FieldValue(representation, "context_of_items").span,
                        Status::Rejected,
                        representation,
                        "context_of_items",
                        FieldDetail::AttributePath,
                        "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                        "context without unit assignment",
                        "N2635 VALUE_REPRESENTATION_ITEM.WR1"
                    );
                }
            }
            const auto& context = FieldValue(representation, "context_of_items");
            Visit(context.span);
            attached.push_back({ link, property, owner, pd, representation, Target(context) });
        }
        // Grant roots only after every link has proven its owner and item boundary.
        // Preserve every source link, shared representation/item and original context;
        // property units never replace m_context or the body's selected unit scale.
        for (std::size_t i = 0; i < attached.size(); ++i)
        {
            m_data->part.roles.push_back({ "property_link_" + std::to_string(i), attached[i].link });
        }
        m_data->part.propertyAttachments = std::move(attached);
    }
    void AttachAp214Presentations()
    {
        std::vector<std::size_t> items;
        for (const auto& value : FieldValue(m_representation, "items").children)
        {
            items.push_back(Target(value));
        }
        const auto bodyUse = Reach(std::move(items), true);
        const auto& presentations = Population("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION");
        for (std::size_t i = 0; i < presentations.size(); ++i)
        {
            const auto presentation = presentations[i];
            for (const auto& value : FieldValue(presentation, "items").children)
            {
                Visit(value.span);
                const auto styled = Target(value);
                if (!IsA(*m_data, styled, "STYLED_ITEM"))
                {
                    FieldFailure(
                        "UnsupportedPresentationAttachment",
                        "D2",
                        "This presentation subset requires styled items",
                        value.span,
                        Status::Unsupported,
                        presentation,
                        "items",
                        FieldDetail::AttributePath
                    );
                }
                const auto& target = FieldValue(styled, "item");
                // This is a project attachment guard, not MDGPR WHERE evaluation. Prove
                // the style refers to the body's existing RI closure before granting it
                // ownership. A style/management path cannot smuggle in another shape.
                if (!bodyUse[Target(target)])
                {
                    FieldFailure(
                        "UnsupportedPresentationAttachment",
                        "D2",
                        "Styled target is not used by the selected body",
                        target.span,
                        Status::Unsupported,
                        styled,
                        "item",
                        FieldDetail::AttributePath,
                        "RI used by selected body",
                        target.text,
                        "profile presentation attachment guard"
                    );
                }
            }
            // Retain the presentation's own context and complete style chain through
            // source references. Its context never replaces the body's unit context.
            m_data->part.roles.push_back({ "presentation_" + std::to_string(i), presentation });
        }
    }
    void AttachAp203Management()
    {
        // N2635 inverse administrative associations are not geometry roots. Admit
        // a root only when ALL of its target items are already owned by this
        // selected part. Fixed-point expansion handles approval/security links
        // without allowing an administrative cycle to invent its own ownership.
        std::vector<std::size_t> candidates;
        for (const auto name : { "CC_DESIGN_APPROVAL",
                                 "CC_DESIGN_SECURITY_CLASSIFICATION",
                                 "CC_DESIGN_PERSON_AND_ORGANIZATION_ASSIGNMENT",
                                 "CC_DESIGN_DATE_AND_TIME_ASSIGNMENT",
                                 "APPROVAL_PERSON_ORGANIZATION",
                                 "APPROVAL_DATE_TIME",
                                 "PRESENTATION_LAYER_ASSIGNMENT" })
        {
            for (const auto r : Population(name))
            {
                candidates.push_back(r);
            }
        }
        m_administrativeAttachments.assign(m_data->types.size(), 0);
        bool changed = true;
        while (changed)
        {
            changed = false;
            std::vector<std::size_t> roots;
            for (const auto& role : m_data->part.roles)
            {
                roots.push_back(role.recordIndex);
            }
            const auto owned = Reach(std::move(roots), false);
            for (const auto record : candidates)
            {
                if (m_administrativeAttachments[record])
                {
                    continue;
                }
                Visit(Record(record).span);
                const char* field = IsA(*m_data, record, "APPROVAL_PERSON_ORGANIZATION")  ? "authorized_approval" :
                                    IsA(*m_data, record, "APPROVAL_DATE_TIME")            ? "dated_approval" :
                                    IsA(*m_data, record, "PRESENTATION_LAYER_ASSIGNMENT") ? "assigned_items" :
                                                                                            "items";
                const auto& value = FieldValue(record, field);
                bool attached = true;
                if (value.kind == ValueKind::Aggregate)
                {
                    for (const auto& item : value.children)
                    {
                        Visit(item.span);
                        attached = attached && owned[Target(item)];
                    }
                }
                else
                {
                    Visit(value.span);
                    attached = owned[Target(value)];
                }
                if (!attached)
                {
                    continue;
                }
                m_administrativeAttachments[record] = 1;
                m_data->part.roles.push_back({ "administrative_attachment_" + std::to_string(record), record });
                changed = true;
            }
        }
        for (const auto record : candidates)
        {
            if (!m_administrativeAttachments[record])
            {
                Fail(
                    "UnsupportedAdministrativeAttachment",
                    "D2",
                    "Administrative targets are outside the selected part closure",
                    Record(record).span,
                    Status::Unsupported,
                    record
                );
            }
        }
    }

    void Closure()
    {
        m_outgoing.resize(m_data->types.size());
        const auto& references = m_data->source->References();
        for (std::size_t i = 0; i < references.size(); ++i)
        {
            m_outgoing[references[i].recordIndex].push_back(i);
        }
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            AttachAp214Properties();
            AttachAp214Presentations();
        }
        if (m_data->profile.kind == ProfileKind::Ap203e2GeneralSource)
        {
            AttachAp203Management();
        }
        std::vector<std::size_t> seeds;
        for (const auto& role : m_data->part.roles)
        {
            seeds.push_back(role.recordIndex);
        }
        const auto reached = Reach(std::move(seeds), false);
        std::vector<std::size_t> orphans;
        std::string ids;
        for (std::size_t r = 0; r < reached.size(); ++r)
        {
            if (!reached[r])
            {
                orphans.push_back(r);
                ids += "#" + Record(r).id + " ";
            }
        }
        if (!orphans.empty())
        {
            try
            {
                Fail(
                    "UnownedOrOrphanProfileContent",
                    "D2",
                    "Records are not owned by the closed single-part input",
                    Record(orphans[0]).span,
                    Status::Unsupported,
                    orphans[0],
                    0,
                    {},
                    "reachable from selected roles",
                    ids,
                    "profile ownership, not RI.WR1"
                );
            }
            catch (Failure& failure)
            {
                for (std::size_t i = 1; i < orphans.size(); ++i)
                {
                    failure.diagnostic.source.related.push_back(Record(orphans[i]).idSpan);
                }
                std::vector<unsigned char> bodyUse;
                const auto styledItem = [&](auto r)
                {
                    return IsA(*m_data, r, "STYLED_ITEM");
                };
                if (Interoperability() && std::any_of(orphans.begin(), orphans.end(), styledItem))
                {
                    std::vector<std::size_t> bodyItems;
                    for (const auto& value : FieldValue(m_representation, "items").children)
                    {
                        bodyItems.push_back(Target(value));
                    }
                    bodyUse = Reach(std::move(bodyItems), true);
                }
                const auto nondefiningResidue = [&](std::size_t r)
                {
                    Visit(Record(r).span);
                    if (styledItem(r))
                    {
                        // An absent presentation owner cannot hide another shape.
                        // The explicit target must already be used by this body.
                        const auto& target = FieldValue(r, "item");
                        Visit(target.span);
                        return !bodyUse.empty() && bodyUse[Target(target)];
                    }
                    // Standalone category dictionaries have no source references,
                    // shape role or unit effect. Keep them as unattached source
                    // metadata with the original orphan diagnostic, never fake roots.
                    if (m_data->profile.kind == ProfileKind::Ap203e2GeneralSource && Record(r).components.size() == 1 &&
                        Record(r).components.front().name == "PRODUCT_CATEGORY")
                    {
                        return m_outgoing[r].empty();
                    }
                    if (!NonGeometricStyle(r))
                    {
                        return false;
                    }
                    for (auto reference : m_outgoing[r])
                    {
                        Visit(references[reference].span);
                        if (!NonGeometricStyle(references[reference].targetRecordIndex))
                        {
                            return false;
                        }
                    }
                    return true;
                };
                const bool nonphysicalResidue = Interoperability() && std::all_of(orphans.begin(), orphans.end(), nondefiningResidue);
                if (!nonphysicalResidue)
                {
                    throw;
                }
                // Preserve source-only residue separately. No part role or fake
                // geometry/presentation attachment is manufactured for these IDs.
                m_data->unattachedNonGeometric = orphans;
                Preserve(std::move(failure));
            }
        }
        std::vector<std::size_t> items;
        for (const auto& value : FieldValue(m_representation, "items").children)
        {
            items.push_back(Target(value));
        }
        if (m_data->profile.kind != ProfileKind::Ap203e2Planar)
        {
            // RI use follows actual representation items and RI references, never the
            // non-RI style/unit/management dependencies traversed for ownership above.
            for (const auto representation : Population("REPRESENTATION"))
            {
                if (representation == m_representation)
                {
                    continue;
                }
                for (const auto& value : FieldValue(representation, "items").children)
                {
                    items.push_back(Target(value));
                }
            }
        }
        m_data->representationUse = Reach(std::move(items), true);
    }

    void ApprovePreservedMetadata()
    {
        if (m_data->pendingIssues.empty())
        {
            return;
        }
        Expects(Interoperability());
        // All property roots are now proven. A bad MRI may be retained only if
        // it is an actual metadata item and is absent from body geometry and
        // physical-unit dependencies (including conversion-factor references).
        const auto physical = Reach({ m_representation, m_context }, false);
        std::vector<unsigned char> metadata(m_data->types.size());
        for (const auto& attachment : m_data->part.propertyAttachments)
        {
            for (const auto& value : FieldValue(attachment.representation, "items").children)
            {
                Visit(value.span);
                metadata[Target(value)] = 1;
            }
        }
        if (m_data->pendingIssues.size() > m_outcome.limits.preservedDiagnostics)
        {
            Fail(
                "PreservedDiagnosticLimit",
                "D2",
                "Interoperability diagnostic capacity exceeded",
                m_data->pendingIssues.back().diagnostic.source.primary,
                Status::ResourceLimit
            );
        }
        for (auto& issue : m_data->pendingIssues)
        {
            Visit(issue.diagnostic.source.primary);
            const auto record = IndexOf(*m_data, issue.diagnostic.source.recordId);
            const auto& key = issue.diagnostic.source.key;
            if (key == "ReferenceTypeMismatch")
            {
                const auto target = IndexOf(*m_data, issue.diagnostic.source.targetId);
                const bool selected = std::any_of(
                    m_data->part.roles.begin(),
                    m_data->part.roles.end(),
                    [&](const auto& role)
                    {
                        return (role.name == "definition" || role.name == "formation") && role.recordIndex == target;
                    }
                );
                // Consult the ownership proof itself. Exported role labels are
                // report text and cannot authorize a source-type exception.
                const bool attached = record < m_administrativeAttachments.size() && m_administrativeAttachments[record];
                if (!selected || !attached || physical[record])
                {
                    State(Phase::Types) = FailureProgress(issue.status);
                    State(Phase::Interoperability) = FailureProgress(issue.status);
                    throw Failure{ issue.status, std::move(issue.diagnostic) };
                }
            }
            const bool measure = key == "SourceRuleViolation:MEASURE_WITH_UNIT.WR1" || key == "SourceRuleViolation:POSITIVE_RATIO_MEASURE.WR1";
            if (measure && (!metadata[record] || physical[record]))
            {
                State(Phase::UnitDependencies) = FailureProgress(issue.status);
                State(Phase::Interoperability) = FailureProgress(issue.status);
                throw Failure{ issue.status, std::move(issue.diagnostic) };
            }
        }
        for (auto& issue : m_data->pendingIssues)
        {
            m_data->preservedDiagnostics.push_back(std::move(issue.diagnostic));
        }
        m_data->pendingIssues.clear();
    }
    void UnitContext()
    {
        const auto& dimension = FieldValue(m_context, "coordinate_space_dimension");
        if (!PositiveInteger(dimension.text))
        {
            FieldFailure(
                "SourceRuleViolation:DIMENSION_COUNT.WR1",
                "D3",
                "dimension_count must be positive",
                dimension.span,
                Status::Rejected,
                m_context,
                "coordinate_space_dimension",
                FieldDetail::AttributePath,
                ">0",
                dimension.text,
                "DIMENSION_COUNT.WR1"
            );
        }
        if (!Three(dimension.text))
        {
            FieldFailure(
                "UnsupportedCoordinateDimension",
                "D2",
                "This profile requires a 3D representation context",
                dimension.span,
                Status::Unsupported,
                m_context,
                "coordinate_space_dimension",
                FieldDetail::AttributePath,
                "3",
                dimension.text,
                "profile dimension guard"
            );
        }
        std::vector<std::size_t> lengths, angles, solids;
        const auto& assignments = FieldValue(m_context, "units");
        for (const auto& unit : assignments.children)
        {
            const auto target = Target(unit);
            if (IsA(*m_data, target, "LENGTH_UNIT"))
            {
                lengths.push_back(target);
            }
            else if (IsA(*m_data, target, "PLANE_ANGLE_UNIT"))
            {
                angles.push_back(target);
            }
            else if (IsA(*m_data, target, "SOLID_ANGLE_UNIT"))
            {
                solids.push_back(target);
            }
        }
        if (lengths.size() != 1 || angles.size() != 1 || solids.size() > 1)
        {
            FieldFailure(
                "UnsupportedUnitAssignment",
                "D2",
                "Exactly one length and plane angle, at most one solid angle are required",
                assignments.span,
                Status::Unsupported,
                m_context,
                "units",
                FieldDetail::AttributePath,
                "length=1 angle=1 solid-angle<=1",
                "different nominal quantities/counts",
                "profile unit guard; no defaults"
            );
        }
        m_data->units.lengthUnit = lengths[0];
        m_data->units.planeAngleUnit = angles[0];
        if (!solids.empty())
        {
            m_data->units.solidAngleUnit = solids[0];
        }
        for (auto r : Population("SI_UNIT"))
        {
            Visit(Record(r).span);
            SiUnit definition;
            definition.recordIndex = r;
            definition.siName = FieldValue(r, "name").text;
            definition.prefix = FieldValue(r, "prefix").kind == ValueKind::Unset ? "" : FieldValue(r, "prefix").text;
            const bool length = IsA(*m_data, r, "LENGTH_UNIT");
            const bool angle = IsA(*m_data, r, "PLANE_ANGLE_UNIT");
            const bool mass = IsA(*m_data, r, "MASS_UNIT");
            definition.quantity = length ? "Length" : angle ? "PlaneAngle" : mass ? "Mass" : "SolidAngle";
            const std::string expected = length ? "METRE" : angle ? "RADIAN" : mass ? "GRAM" : "STERADIAN";
            if (definition.siName != expected)
            {
                FieldFailure(
                    "UnsupportedSiUnitKind",
                    "D2",
                    "SI name/nominal quantity pairing is outside this profile",
                    FieldValue(r, "name").span,
                    Status::Unsupported,
                    r,
                    "name",
                    FieldDetail::AttributePath,
                    expected,
                    definition.siName,
                    "profile nominal SI-unit guard"
                );
            }
            // Canonical mass metadata uses kg; the source SI enumeration remains GRAM
            // and original prefix/value data are retained. No derived scale is inferred.
            definition.canonicalPower10 = Schema(*m_data).PrefixPower10(definition.prefix) + (length ? 3 : mass ? -3 : 0);
            if (length)
            {
                definition.dimensions[0] = 1;
            }
            if (mass)
            {
                definition.dimensions[1] = 1;
            }
            if (r == lengths[0])
            {
                m_data->units.lengthPower10Mm = definition.canonicalPower10;
            }
            m_data->units.siDefinitions.push_back(std::move(definition));
        }
        // AP214 source dimensions/cycles were checked before this phase. Conversion
        // scale/positivity and uncertainty evaluation are still deferred; AP203 retains
        // its existing interpretation boundary for conversion dimensions and cycles.
    }
};
} // namespace

Snapshot::Snapshot(std::shared_ptr<const SemanticData> data) : m_data(std::move(data))
{
    if (!m_data || !m_data->source || !m_data->bindingsComplete || m_data->fields.size() != m_data->source->Records().size())
    {
        throw std::invalid_argument("Semantic snapshot requires complete type/part data");
    }
}
const step_source::SourceSnapshot& Snapshot::Source() const
{
    return *m_data->source;
}
const Part& Snapshot::PartDefinition() const
{
    return m_data->part;
}
const QualifiedSchemaProfile& Snapshot::SelectedProfile() const
{
    return m_data->profile;
}
const SchemaIdentifier& Snapshot::SchemaIdentity() const
{
    return m_data->schemaIdentity;
}
const Units& Snapshot::UnitContext() const
{
    return m_data->units;
}
const std::vector<Diagnostic>& Snapshot::PreservedDiagnostics() const
{
    return m_data->preservedDiagnostics;
}
const std::vector<std::size_t>& Snapshot::UnattachedNonGeometricDeclarations() const
{
    return m_data->unattachedNonGeometric;
}
const std::vector<std::string>& Snapshot::RegisteredTypes() const
{
    return Schema(*m_data).names;
}
const std::vector<std::size_t>* Snapshot::Instances(std::string_view type) const
{
    const auto id = Schema(*m_data).Find(type);
    return id ? &m_data->populations[*id] : nullptr;
}
bool Snapshot::IsA(std::size_t record, std::string_view type) const
{
    const auto id = Schema(*m_data).Find(type);
    if (!id)
    {
        throw std::invalid_argument("Unregistered source type query");
    }
    return m_data->types.at(record).test(*id);
}
std::vector<std::string> Snapshot::TypesOf(std::size_t record) const
{
    std::vector<std::string> names;
    for (std::size_t t = 0; t < Schema(*m_data).names.size(); ++t)
    {
        if (m_data->types.at(record).test(t))
        {
            names.push_back(Schema(*m_data).names[t]);
        }
    }
    return names;
}
AttributeView Snapshot::Attribute(std::size_t r, std::size_t c, std::size_t p) const
{
    return AttributeAt(*m_data, r, c, p);
}
FieldView Snapshot::Field(std::size_t record, std::string_view name) const
{
    return FieldView(m_data, record, FindFieldBinding(*m_data, record, name));
}
FieldView Snapshot::Field(std::size_t record, std::string_view declaredIn, std::string_view name) const
{
    return FieldView(m_data, record, FindFieldBinding(*m_data, record, name, declaredIn));
}
std::size_t Snapshot::ReferenceTarget(const step_source::Value& value) const
{
    return ResolveReference(*m_data, value);
}

FieldView::FieldView(std::shared_ptr<const SemanticData> data, std::size_t record, std::size_t binding)
    : m_data(std::move(data)), m_record(record), m_binding(binding)
{
}
const AttributeView& FieldView::Declaration() const
{
    return m_data->fields.at(m_record).attributes.at(m_binding).declaration;
}
const step_source::Value& FieldView::Value() const
{
    return *Declaration().value;
}
AttributeLocation FieldView::Location() const
{
    const auto& bound = m_data->fields.at(m_record).attributes.at(m_binding);
    const auto& record = m_data->source->Records().at(m_record);
    return { record.id, record.components.at(bound.component).name, Value().span, { bound.parameter + 1 } };
}
std::size_t FieldView::ReferenceTarget() const
{
    return ResolveReference(*m_data, Value());
}
std::vector<std::size_t> FieldView::ReferenceTargets() const
{
    const auto& bound = m_data->fields.at(m_record).attributes.at(m_binding);
    if (!bound.referenceAggregate || Value().kind != step_source::ValueKind::Aggregate)
    {
        throw std::invalid_argument("Source slot is not an explicit reference aggregate");
    }
    std::vector<std::size_t> result;
    result.reserve(Value().children.size());
    for (const auto& member : Value().children)
    {
        result.push_back(ResolveReference(*m_data, member));
    }
    return result;
}
std::vector<std::size_t> Snapshot::UsedIn(std::string_view target, std::string_view type, std::string_view field) const
{
    const auto typeId = Schema(*m_data).Find(type);
    if (!typeId || !Schema(*m_data).HasField(*typeId, field))
    {
        throw std::invalid_argument("Unregistered USEDIN type/attribute role");
    }
    IndexOf(*m_data, target);
    std::vector<std::size_t> result;
    for (auto index : m_data->source->Incoming(target))
    {
        const auto& reference = m_data->source->References()[index];
        if (IsA(reference.recordIndex, type) &&
            Attribute(reference.recordIndex, reference.componentIndex, reference.parameterPath[0] - 1).name == field)
        {
            result.push_back(index);
        }
    }
    return result;
}
bool Snapshot::UsedByRepresentation(std::size_t record) const
{
    return IsA(record, "REPRESENTATION_ITEM") && m_data->representationUse.at(record) != 0;
}
Outcome Check(const step_source::InspectOutcome& source, Limits limits)
{
    return Check(source, limits, SourcePolicy::Strict);
}
Outcome Check(const step_source::InspectOutcome& source, Limits limits, SourcePolicy policy, GeometryScope scope)
{
    if ((policy != SourcePolicy::Strict && policy != SourcePolicy::PreserveNonGeometric) ||
        (scope != GeometryScope::ExplicitOuterBounds && scope != GeometryScope::GeneralBrep))
    {
        throw std::invalid_argument("Unknown source checking policy or geometry scope");
    }
    Outcome result;
    result.limits = limits;
    result.policy = policy;
    result.geometryScope = scope;
    if (!source.source || source.status != step_source::Status::Inspected)
    {
        for (const auto& diagnostic : source.diagnostics)
        {
            Diagnostic wrapped;
            wrapped.source = diagnostic;
            wrapped.classification = "SourceInspectionFailure";
            wrapped.responsibility = "ticket 02 syntax/index/reference check";
            result.diagnostics.push_back(std::move(wrapped));
        }
        return result;
    }
    if (limits.dependencyVisits > 10000000)
    {
        result.status = Status::ResourceLimit;
        Diagnostic diagnostic;
        diagnostic.source.key = "InvalidSemanticLimits";
        diagnostic.source.stage = "D2";
        diagnostic.source.message = "Semantic visit budget exceeds implementation ceiling";
        diagnostic.classification = "ResourceLimit";
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    auto data = std::make_shared<SemanticData>();
    data->source = source.source;
    data->policy = policy;
    data->geometryScope = scope;
    data->preservedDiagnosticLimit = limits.preservedDiagnostics;
    Checker checker(data, result);
    try
    {
        checker.Run();
        result.status = data->units.lengthPower10Mm ? Status::Checked : Status::Deferred;
        if (!data->part.deferredRegionFaces.empty())
        {
            result.status = Status::Deferred;
        }
        result.preservedDiagnostics = data->preservedDiagnostics;
        if (result.status == Status::Checked && !result.preservedDiagnostics.empty())
        {
            result.status = Status::CheckedWithDiagnostics;
        }
        result.snapshot = std::make_shared<const Snapshot>(std::move(data));
    }
    catch (const Failure& failure)
    {
        result.status = failure.status;
        result.diagnostics.push_back(failure.diagnostic);
    }
    checker.PublishProgress();
    return result;
}
const char* StatusName(Status status)
{
    switch (status)
    {
        case Status::Checked:
            return "Checked";
        case Status::CheckedWithDiagnostics:
            return "CheckedWithDiagnostics";
        case Status::Deferred:
            return "Deferred";
        case Status::Rejected:
            return "Rejected";
        case Status::Unsupported:
            return "Unsupported";
        case Status::ResourceLimit:
            return "ResourceLimit";
        case Status::SourceFailure:
            return "SourceFailure";
    }
    return "Unknown";
}
} // namespace step_semantics
