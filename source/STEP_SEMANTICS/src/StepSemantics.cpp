// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"
#include "StepSchemaSelection.h"

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
    const auto type = Schema().Find(name);
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
    }
    void PublishProgress()
    {
        m_outcome.binding = ProgressName(State(Phase::Binding));
        m_outcome.types = ProgressName(State(Phase::Types));
        m_outcome.part = ProgressName(State(Phase::Part));
        m_outcome.siUnits = ProgressName(State(Phase::Units));
    }

private:

    enum class Phase
    {
        Binding,
        Types,
        Part,
        Units
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
    std::array<Progress, 4> m_progress{};

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
        // Only the registered qualified profile may enter the existing N2635 catalog.
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
    }

    void Mapping()
    {
        const auto& catalog = Schema();
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
                        "N2635 audited subtype groups"
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
                        "Complete complex mapping is outside the six admitted signatures",
                        records[r].span,
                        Status::Unsupported,
                        r,
                        0,
                        {},
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
                "N2635 explicit attribute"
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
                        "N2635 BOOLEAN"
                    );
                }
                break;
            case FieldKind::Enumeration:
            {
                if (value.kind != ValueKind::Enumeration)
                {
                    mismatch();
                }
                if (!Schema().ContainsEnumeration(field.type, value.text))
                {
                    Fail(
                        "EnumerationValueInvalid",
                        "D1",
                        "Enumeration token is absent from the fixed N2635 type",
                        value.span,
                        Status::Rejected,
                        r,
                        c,
                        path,
                        field.type,
                        value.text,
                        "fixed N2635 enumeration"
                    );
                }
                break;
            }
            case FieldKind::Reference:
                if (value.kind != ValueKind::Reference)
                {
                    mismatch();
                }
                if (!Schema().MatchesReference(m_data->types.at(UnboundTarget(value)), field.type))
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
                            "N2635 explicit reference type"
                        );
                    }
                    catch (Failure& failure)
                    {
                        failure.diagnostic.source.targetId = value.text;
                        failure.diagnostic.source.related.push_back(Record(UnboundTarget(value)).idSpan);
                        throw;
                    }
                }
                m_data->referenceTargets.emplace(&value, UnboundTarget(value));
                break;
            case FieldKind::Measure:
                if (value.kind != ValueKind::Typed || value.children.size() != 1)
                {
                    mismatch();
                }
                if (!Schema().AdmitsMeasure(value.text))
                {
                    Fail(
                        "UnsupportedMeasureBranch",
                        "D2",
                        "This profile consumes only the registered length measure family",
                        value.span,
                        Status::Unsupported,
                        r,
                        c,
                        path,
                        "typed length measure",
                        value.text,
                        "profile measure SELECT guard"
                    );
                }
                if (value.children[0].kind != ValueKind::Real)
                {
                    mismatch();
                }
                break;
            case FieldKind::Aggregate:
                throw std::logic_error("Nested aggregate descriptor not expected");
        }
    }

    void Fields()
    {
        const auto& catalog = Schema();
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
                        "N2635 attribute layout"
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
                    bound.attributes.push_back({ view, c, p, field.kind == FieldKind::Aggregate && field.elementKind == FieldKind::Reference });
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
                            "N2635 required attribute"
                        );
                    }
                    if (field.kind != FieldKind::Aggregate)
                    {
                        Scalar(value, field.kind, field, r, c, path);
                        continue;
                    }
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
                            "N2635 aggregate"
                        );
                    }
                    if (value.children.size() < field.minimum || value.children.size() > field.maximum)
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
                            "N2635 aggregate"
                        );
                    }
                    std::unordered_map<std::string, Span> members;
                    for (std::size_t i = 0; i < value.children.size(); ++i)
                    {
                        const auto& child = value.children[i];
                        auto childPath = path;
                        childPath.push_back(i + 1);
                        Visit(child.span);
                        Scalar(child, field.elementKind, field, r, c, childPath);
                        if (field.unique != Unique::None && !members.emplace(child.text, child.span).second)
                        {
                            try
                            {
                                Fail(
                                    field.unique == Unique::Set ? "SetDuplicateMember" : "AggregateUniqueViolation",
                                    "D1",
                                    "Aggregate repeats an entity identity",
                                    child.span,
                                    Status::Rejected,
                                    r,
                                    c,
                                    childPath,
                                    "unique instance identity",
                                    child.text,
                                    std::string(view.declaredIn) + "." + field.name
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
            }
        }
        m_data->bindingsComplete = true;
    }

    const std::vector<std::size_t>& Population(std::string_view name) const
    {
        return m_data->populations[Schema().ids.at(std::string(name))];
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
    void Product()
    {
        const auto ac = One("APPLICATION_CONTEXT"), apd = One("APPLICATION_PROTOCOL_DEFINITION"), pc = One("PRODUCT_CONTEXT");
        const auto pdc = One("PRODUCT_DEFINITION_CONTEXT"), product = One("PRODUCT"), formation = One("PRODUCT_DEFINITION_FORMATION");
        const auto pd = One("PRODUCT_DEFINITION"), pds = One("PRODUCT_DEFINITION_SHAPE"), sdr = One("SHAPE_DEFINITION_REPRESENTATION");
        m_representation = One("REPRESENTATION");
        m_context = One("REPRESENTATION_CONTEXT");
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
        Link(sdr, "used_representation", m_representation);
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
            if (FieldValue(category, "name").text != "part" || products.children.size() != 1 || Target(products.children[0]) != product)
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
    void Closure()
    {
        m_outgoing.resize(m_data->types.size());
        const auto& references = m_data->source->References();
        for (std::size_t i = 0; i < references.size(); ++i)
        {
            m_outgoing[references[i].recordIndex].push_back(i);
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
                throw;
            }
        }
        std::vector<std::size_t> items;
        for (const auto& value : FieldValue(m_representation, "items").children)
        {
            items.push_back(Target(value));
        }
        m_data->representationUse = Reach(std::move(items), true);
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
            definition.quantity = length ? "Length" : angle ? "PlaneAngle" : "SolidAngle";
            const std::string expected = length ? "METRE" : angle ? "RADIAN" : "STERADIAN";
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
            definition.canonicalPower10 = Schema().PrefixPower10(definition.prefix) + (length ? 3 : 0);
            if (length)
            {
                definition.dimensions[0] = 1;
            }
            if (r == lengths[0])
            {
                m_data->units.lengthPower10Mm = definition.canonicalPower10;
            }
            m_data->units.siDefinitions.push_back(std::move(definition));
        }
        // Conversion factor/dimensions/positivity/cycles and uncertainty evaluation belong to later tickets.
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
const Units& Snapshot::UnitContext() const
{
    return m_data->units;
}
const std::vector<std::string>& Snapshot::RegisteredTypes() const
{
    return Schema().names;
}
const std::vector<std::size_t>* Snapshot::Instances(std::string_view type) const
{
    const auto id = Schema().Find(type);
    return id ? &m_data->populations[*id] : nullptr;
}
bool Snapshot::IsA(std::size_t record, std::string_view type) const
{
    const auto id = Schema().Find(type);
    if (!id)
    {
        throw std::invalid_argument("Unregistered source type query");
    }
    return m_data->types.at(record).test(*id);
}
std::vector<std::string> Snapshot::TypesOf(std::size_t record) const
{
    std::vector<std::string> names;
    for (std::size_t t = 0; t < Schema().names.size(); ++t)
    {
        if (m_data->types.at(record).test(t))
        {
            names.push_back(Schema().names[t]);
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
    const auto typeId = Schema().Find(type);
    if (!typeId || !Schema().HasField(*typeId, field))
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
    Outcome result;
    result.limits = limits;
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
    Checker checker(data, result);
    try
    {
        checker.Run();
        result.status = data->units.lengthPower10Mm ? Status::Checked : Status::Deferred;
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
