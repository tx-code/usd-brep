// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"

#include <algorithm>
#include <sstream>
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
const std::unordered_map<std::string, int> prefixes = { { "", 0 },        { "EXA", 18 },   { "PETA", 15 },  { "TERA", 12 }, { "GIGA", 9 },
                                                        { "MEGA", 6 },    { "KILO", 3 },   { "HECTO", 2 },  { "DECA", 1 },  { "DECI", -1 },
                                                        { "CENTI", -2 },  { "MILLI", -3 }, { "MICRO", -6 }, { "NANO", -9 }, { "PICO", -12 },
                                                        { "FEMTO", -15 }, { "ATTO", -18 } };
const std::unordered_set<std::string> siNames = { "METRE",   "GRAM",           "SECOND", "AMPERE", "KELVIN",    "MOLE",  "CANDELA",
                                                  "RADIAN",  "STERADIAN",      "HERTZ",  "NEWTON", "PASCAL",    "JOULE", "WATT",
                                                  "COULOMB", "VOLT",           "FARAD",  "OHM",    "SIEMENS",   "WEBER", "TESLA",
                                                  "HENRY",   "DEGREE_CELSIUS", "LUMEN",  "LUX",    "BECQUEREL", "GRAY",  "SIEVERT" };

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
bool Matches(const SemanticData& data, std::size_t record, std::string_view expected)
{
    // Under the closed profile guard these are the only possible branches of the full SELECTs.
    if (expected == "UNIT")
    {
        return IsA(data, record, "NAMED_UNIT") || IsA(data, record, "DERIVED_UNIT");
    }
    if (expected == "SHELL")
    {
        return IsA(data, record, "OPEN_SHELL") || IsA(data, record, "CLOSED_SHELL");
    }
    if (expected == "CHARACTERIZED_DEFINITION")
    {
        return IsA(data, record, "PRODUCT_DEFINITION") || IsA(data, record, "PRODUCT_DEFINITION_SHAPE");
    }
    if (expected == "REPRESENTED_DEFINITION")
    {
        return IsA(data, record, "PROPERTY_DEFINITION");
    }
    return IsA(data, record, expected);
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
        m_phase = &m_outcome.binding;
        Binding();
        *m_phase = "Passed";
        m_phase = &m_outcome.types;
        Mapping();
        Fields();
        *m_phase = "Passed";
        m_phase = &m_outcome.part;
        Product();
        Closure();
        *m_phase = "Passed";
        m_phase = &m_outcome.siUnits;
        UnitContext();
        *m_phase = m_data->units.lengthPower10Mm ? "Passed" : "Deferred";
    }

private:

    std::shared_ptr<SemanticData> m_data;
    Outcome& m_outcome;
    std::string* m_phase = nullptr;
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
            *m_phase = StatusName(status);
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
    const Value& ValueAt(std::size_t r, std::size_t slot) const
    {
        return Record(r).components[0].parameters.at(slot);
    }
    std::size_t Target(const Value& value) const
    {
        return IndexOf(*m_data, value.text);
    }
    const step_source::Component& Component(std::size_t r, std::string_view name) const
    {
        const auto& parts = Record(r).components;
        const auto found = std::find_if(
            parts.begin(),
            parts.end(),
            [&](const auto& part)
            {
                return part.name == name;
            }
        );
        if (found == parts.end())
        {
            throw std::logic_error("Validated facet is missing");
        }
        return *found;
    }
    std::size_t ComponentIndex(std::size_t r, std::string_view name) const
    {
        return static_cast<std::size_t>(&Component(r, name) - Record(r).components.data());
    }

    void Binding()
    {
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
        // Recognize whitespace between the exact name and numeric OID components; do not normalize name/edition aliases.
        const auto& literal = identifiers[0].text;
        const auto opening = literal.find('{'), closing = literal.find('}');
        std::vector<std::string> names, oid, tail;
        std::string token;
        if (opening != std::string::npos && closing != std::string::npos && opening < closing)
        {
            std::istringstream before(literal.substr(0, opening)), inside(literal.substr(opening + 1, closing - opening - 1)),
                after(literal.substr(closing + 1));
            while (before >> token)
            {
                names.push_back(token);
            }
            while (inside >> token)
            {
                oid.push_back(token);
            }
            while (after >> token)
            {
                tail.push_back(token);
            }
        }
        const std::vector<std::string> required{ "1", "0", "10303", "403", "2", "1", "2" };
        if (names != std::vector<std::string>{ SchemaName } || oid != required || !tail.empty() ||
            std::count(literal.begin(), literal.end(), '{') != 1 || std::count(literal.begin(), literal.end(), '}') != 1)
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
        const std::vector<std::vector<std::string>> signatures = {
            { "GEOMETRIC_REPRESENTATION_CONTEXT", "GLOBAL_UNIT_ASSIGNED_CONTEXT", "REPRESENTATION_CONTEXT" },
            { "GEOMETRIC_REPRESENTATION_CONTEXT", "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT", "GLOBAL_UNIT_ASSIGNED_CONTEXT", "REPRESENTATION_CONTEXT" },
            { "LENGTH_UNIT", "NAMED_UNIT", "SI_UNIT" },
            { "NAMED_UNIT", "PLANE_ANGLE_UNIT", "SI_UNIT" },
            { "NAMED_UNIT", "SI_UNIT", "SOLID_ANGLE_UNIT" },
            { "CONVERSION_BASED_UNIT", "LENGTH_UNIT", "NAMED_UNIT" }
        };
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
                const auto countGroup = [&](std::initializer_list<const char*> group)
                {
                    std::size_t count = 0;
                    for (const auto* name : group)
                    {
                        count += IsA(*m_data, r, name) ? 1 : 0;
                    }
                    return count;
                };
                if (countGroup({ "SI_UNIT", "CONVERSION_BASED_UNIT" }) > 1 ||
                    countGroup({ "LENGTH_UNIT", "PLANE_ANGLE_UNIT", "SOLID_ANGLE_UNIT", "MASS_UNIT" }) > 1 ||
                    countGroup({ "PRODUCT_CONTEXT", "PRODUCT_DEFINITION_CONTEXT" }) > 1)
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
                if (std::find(signatures.begin(), signatures.end(), explicitTypes) == signatures.end())
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
                const bool valid = field.type == "SI_PREFIX"    ? prefixes.count(value.text) != 0 :
                                   field.type == "SI_UNIT_NAME" ? siNames.count(value.text) != 0 :
                                                                  field.type == "SOURCE" &&
                                                                      (value.text == "MADE" || value.text == "BOUGHT" || value.text == "NOT_KNOWN");
                if (!valid)
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
                if (!Matches(*m_data, Target(value), field.type))
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
                            Record(Target(value)).components[0].name,
                            "N2635 explicit reference type"
                        );
                    }
                    catch (Failure& failure)
                    {
                        failure.diagnostic.source.targetId = value.text;
                        failure.diagnostic.source.related.push_back(Record(Target(value)).idSpan);
                        throw;
                    }
                }
                break;
            case FieldKind::Measure:
                if (value.kind != ValueKind::Typed || value.children.size() != 1)
                {
                    mismatch();
                }
                if (value.text != "LENGTH_MEASURE" && value.text != "NON_NEGATIVE_LENGTH_MEASURE" && value.text != "POSITIVE_LENGTH_MEASURE")
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
    void Link(std::size_t owner, const Value& field, std::size_t target)
    {
        if (Target(field) != target)
        {
            Fail(
                "UnsupportedSinglePartGraph",
                "D2",
                "Product chain roles do not refer to the same source definition",
                field.span,
                Status::Unsupported,
                owner,
                0,
                {},
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
        Link(apd, ValueAt(apd, 3), ac);
        Link(pc, ValueAt(pc, 1), ac);
        Link(pdc, ValueAt(pdc, 1), ac);
        if (ValueAt(product, 3).children.size() != 1)
        {
            Fail("UnsupportedSinglePartGraph", "D2", "Only one product context is supported", ValueAt(product, 3).span, Status::Unsupported, product);
        }
        Link(product, ValueAt(product, 3).children[0], pc);
        Link(formation, ValueAt(formation, 2), product);
        Link(pd, ValueAt(pd, 2), formation);
        Link(pd, ValueAt(pd, 3), pdc);
        Link(pds, ValueAt(pds, 2), pd);
        Link(sdr, ValueAt(sdr, 0), pds);
        Link(sdr, ValueAt(sdr, 1), m_representation);
        Link(m_representation, ValueAt(m_representation, 2), m_context);
        m_data->part.apdStatus = ValueAt(apd, 0).text;
        m_data->part.apdSchemaClaim = ValueAt(apd, 1).text;
        m_data->part.apdYear = ValueAt(apd, 2).text; // APD global RULE is ticket 04; never substitute HEADER text here.
        if (IsA(*m_data, pc, "MECHANICAL_CONTEXT") && ValueAt(pc, 2).text != "mechanical")
        {
            Fail(
                "SourceRuleViolation:MECHANICAL_CONTEXT.WR1",
                "D3",
                "Legacy mechanical context discipline is invalid",
                ValueAt(pc, 2).span,
                Status::Rejected,
                pc,
                0,
                { 3 },
                "mechanical",
                ValueAt(pc, 2).text,
                "MECHANICAL_CONTEXT.WR1"
            );
        }
        if (IsA(*m_data, pdc, "DESIGN_CONTEXT") && ValueAt(pdc, 2).text != "design")
        {
            Fail(
                "SourceRuleViolation:DESIGN_CONTEXT.WR1",
                "D3",
                "Legacy design context lifecycle is invalid",
                ValueAt(pdc, 2).span,
                Status::Rejected,
                pdc,
                0,
                { 3 },
                "design",
                ValueAt(pdc, 2).text,
                "DESIGN_CONTEXT.WR1"
            );
        }
        if (ValueAt(pc, 2).text != "mechanical")
        {
            Fail(
                "UnsupportedProductContextDiscipline",
                "D2",
                "Product discipline is outside this profile",
                ValueAt(pc, 2).span,
                Status::Unsupported,
                pc,
                0,
                { 3 },
                "mechanical",
                ValueAt(pc, 2).text,
                "profile role guard"
            );
        }
        if (ValueAt(pdc, 0).text != "part definition")
        {
            Fail(
                "UnsupportedDefinitionContextRole",
                "D2",
                "Definition context is outside the single-part profile",
                ValueAt(pdc, 0).span,
                Status::Unsupported,
                pdc,
                0,
                { 1 },
                "part definition",
                ValueAt(pdc, 0).text,
                "profile role guard"
            );
        }
        if (ValueAt(pdc, 2).text != "design")
        {
            Fail(
                "UnsupportedDefinitionLifecycleStage",
                "D2",
                "Definition lifecycle is outside this profile",
                ValueAt(pdc, 2).span,
                Status::Unsupported,
                pdc,
                0,
                { 3 },
                "design",
                ValueAt(pdc, 2).text,
                "profile role guard"
            );
        }
        const std::unordered_set<std::string> specialNames{ "visual appearance", "tactile appearance", "contact ratio",
                                                            "hardness",          "treatment result",   "surface texture" };
        if (specialNames.count(ValueAt(pds, 0).text))
        {
            Fail(
                "UnsupportedShapePurpose",
                "D2",
                "Special surface-condition purpose is not supported",
                ValueAt(pds, 0).span,
                Status::Unsupported,
                pds,
                0,
                { 1 }
            );
        }
        if (ValueAt(m_representation, 0).text == "centre of mass" || ValueAt(m_representation, 0).text == "treatment result")
        {
            Fail(
                "UnsupportedRepresentationPurpose",
                "D2",
                "Special representation purpose is not supported",
                ValueAt(m_representation, 0).span,
                Status::Unsupported,
                m_representation,
                0,
                { 1 }
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
            const auto& products = ValueAt(category, 2);
            if (ValueAt(category, 0).text != "part" || products.children.size() != 1 || Target(products.children[0]) != product)
            {
                Fail(
                    "UnsupportedProductCategory",
                    "D2",
                    "Category name/products are outside the single-part guard",
                    ValueAt(category, 0).span,
                    Status::Unsupported,
                    category,
                    0,
                    { 1 },
                    "part for this product",
                    ValueAt(category, 0).text,
                    "profile category guard"
                );
            }
            m_data->part.roles.push_back({ "category", category });
        }
        std::vector<std::size_t> shapes;
        for (const auto& item : ValueAt(m_representation, 1).children)
        {
            const auto target = Target(item);
            if (IsA(*m_data, target, "AXIS2_PLACEMENT_3D"))
            {
                continue;
            }
            const bool solid = IsA(*m_data, m_representation, "ADVANCED_BREP_SHAPE_REPRESENTATION");
            if (!IsA(*m_data, target, solid ? "MANIFOLD_SOLID_BREP" : "SHELL_BASED_SURFACE_MODEL"))
            {
                Fail(
                    "UnsupportedRepresentationItem",
                    "D2",
                    "Only the declared shape and auxiliary placements are supported",
                    item.span,
                    Status::Unsupported,
                    m_representation,
                    0,
                    { 2 },
                    "one MSB/SBSM",
                    Record(target).components[0].name,
                    "profile shape guard"
                );
            }
            shapes.push_back(target);
        }
        if (shapes.size() != 1)
        {
            Fail(
                "UnsupportedShapeMultiplicity",
                "D2",
                "Exactly one shape item is required",
                ValueAt(m_representation, 1).span,
                Status::Unsupported,
                m_representation
            );
        }
        const auto shape = shapes[0];
        m_data->part.roles.push_back({ "shape", shape });
        std::size_t shell = 0;
        if (IsA(*m_data, shape, "MANIFOLD_SOLID_BREP"))
        {
            shell = Target(ValueAt(shape, 1));
            m_data->part.shapeRole = "MaterialSolid";
        }
        else
        {
            if (ValueAt(shape, 1).children.size() != 1)
            {
                Fail("UnsupportedShapeMultiplicity", "D2", "One source shell is required", ValueAt(shape, 1).span, Status::Unsupported, shape);
            }
            shell = Target(ValueAt(shape, 1).children[0]);
            m_data->part.shapeRole = IsA(*m_data, shell, "CLOSED_SHELL") ? "ClosedSurfaceModel" : "OpenSurfaceModel";
        }
        m_data->part.roles.push_back({ "shell", shell });
        for (const auto face : Population("FACE"))
        {
            std::size_t outer = 0;
            for (const auto& bound : ValueAt(face, 1).children)
            {
                outer += IsA(*m_data, Target(bound), "FACE_OUTER_BOUND") ? 1 : 0;
            }
            if (outer != 1)
            {
                Fail(
                    "UnsupportedExplicitOuterBoundRequired",
                    "D2",
                    "This profile requires one explicit FACE_OUTER_BOUND",
                    ValueAt(face, 1).span,
                    Status::Unsupported,
                    face,
                    0,
                    { 2 },
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
        for (const auto& value : ValueAt(m_representation, 1).children)
        {
            items.push_back(Target(value));
        }
        m_data->representationUse = Reach(std::move(items), true);
    }

    void UnitContext()
    {
        const auto& dimension = Component(m_context, "GEOMETRIC_REPRESENTATION_CONTEXT").parameters[0];
        if (!PositiveInteger(dimension.text))
        {
            Fail(
                "SourceRuleViolation:DIMENSION_COUNT.WR1",
                "D3",
                "dimension_count must be positive",
                dimension.span,
                Status::Rejected,
                m_context,
                ComponentIndex(m_context, "GEOMETRIC_REPRESENTATION_CONTEXT"),
                { 1 },
                ">0",
                dimension.text,
                "DIMENSION_COUNT.WR1"
            );
        }
        if (!Three(dimension.text))
        {
            Fail(
                "UnsupportedCoordinateDimension",
                "D2",
                "This profile requires a 3D representation context",
                dimension.span,
                Status::Unsupported,
                m_context,
                ComponentIndex(m_context, "GEOMETRIC_REPRESENTATION_CONTEXT"),
                { 1 },
                "3",
                dimension.text,
                "profile dimension guard"
            );
        }
        std::vector<std::size_t> lengths, angles, solids;
        const auto& assignments = Component(m_context, "GLOBAL_UNIT_ASSIGNED_CONTEXT").parameters[0];
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
            Fail(
                "UnsupportedUnitAssignment",
                "D2",
                "Exactly one length and plane angle, at most one solid angle are required",
                assignments.span,
                Status::Unsupported,
                m_context,
                ComponentIndex(m_context, "GLOBAL_UNIT_ASSIGNED_CONTEXT"),
                { 1 },
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
            const auto& parameters = Component(r, "SI_UNIT").parameters;
            SiUnit definition;
            definition.recordIndex = r;
            definition.siName = parameters[1].text;
            definition.prefix = parameters[0].kind == ValueKind::Unset ? "" : parameters[0].text;
            const bool length = IsA(*m_data, r, "LENGTH_UNIT");
            const bool angle = IsA(*m_data, r, "PLANE_ANGLE_UNIT");
            definition.quantity = length ? "Length" : angle ? "PlaneAngle" : "SolidAngle";
            const std::string expected = length ? "METRE" : angle ? "RADIAN" : "STERADIAN";
            if (definition.siName != expected)
            {
                Fail(
                    "UnsupportedSiUnitKind",
                    "D2",
                    "SI name/nominal quantity pairing is outside this profile",
                    parameters[1].span,
                    Status::Unsupported,
                    r,
                    ComponentIndex(r, "SI_UNIT"),
                    { 2 },
                    expected,
                    definition.siName,
                    "profile nominal SI-unit guard"
                );
            }
            definition.canonicalPower10 = prefixes.at(definition.prefix) + (length ? 3 : 0);
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
    if (!m_data)
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
    try
    {
        Checker(data, result).Run();
        result.status = data->units.lengthPower10Mm ? Status::Checked : Status::Deferred;
        result.snapshot = std::make_shared<const Snapshot>(std::move(data));
    }
    catch (const Failure& failure)
    {
        result.status = failure.status;
        result.diagnostics.push_back(failure.diagnostic);
    }
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
