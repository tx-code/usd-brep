// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemanticReport.h"

#include <algorithm>

namespace step_semantics
{
namespace
{
using step_source::report::Writer;

void Scale(Writer& out, int power)
{
    out.Add("{\"kind\":\"rational\",\"num\":");
    out.String("1" + std::string(static_cast<std::size_t>(std::max(power, 0)), '0'));
    out.Add(",\"den\":");
    out.String("1" + std::string(static_cast<std::size_t>(std::max(-power, 0)), '0'));
    out.Add("}");
}
void RecordIds(Writer& out, const Snapshot& data, const std::vector<std::size_t>& indices)
{
    out.Add("[");
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        if (i)
        {
            out.Add(",");
        }
        out.String(data.Source().Records()[indices[i]].id);
    }
    out.Add("]");
}
void Diagnostics(Writer& out, const step_source::InspectOutcome& source, const std::vector<Diagnostic>& diagnostics)
{
    out.Add("[");
    for (std::size_t i = 0; i < diagnostics.size(); ++i)
    {
        const auto& diagnostic = diagnostics[i];
        const auto& error = diagnostic.source;
        if (i)
        {
            out.Add(",");
        }
        out.Add("{\"key\":");
        out.String(error.key);
        out.Add(",\"stage\":");
        out.String(error.stage);
        out.Add(",\"classification\":");
        out.String(diagnostic.classification);
        out.Add(",\"message\":");
        out.String(error.message);
        out.Add(",\"span\":");
        out.Range(error.primary);
        out.Add(",\"location\":");
        out.Position(source, error.primary.begin);
        out.Add(",\"record_id\":");
        out.String(error.recordId);
        out.Add(",\"component\":");
        out.String(error.component);
        out.Add(",\"parameter_path\":");
        out.Path(error.parameterPath);
        out.Add(",\"target_id\":");
        out.String(error.targetId);
        out.Add(",\"field\":");
        out.String(diagnostic.field);
        out.Add(",\"declared_in\":");
        out.String(diagnostic.declaredIn);
        out.Add(",\"declared_type\":");
        out.String(diagnostic.declaredType);
        out.Add(",\"expected\":");
        out.String(diagnostic.expected);
        out.Add(",\"actual\":");
        out.String(diagnostic.actual);
        out.Add(",\"responsibility\":");
        out.String(diagnostic.responsibility);
        out.Add(",\"related\":[");
        for (std::size_t r = 0; r < error.related.size(); ++r)
        {
            if (r)
            {
                out.Add(",");
            }
            out.Add("{\"span\":");
            out.Range(error.related[r]);
            out.Add(",\"location\":");
            out.Position(source, error.related[r].begin);
            out.Add("}");
        }
        out.Add("]}");
    }
    out.Add("]");
}
} // namespace

void report::WriteSchemaSelection(Writer& out, const std::optional<SchemaSelection>& selection)
{
    if (!selection)
    {
        out.Add("null");
        return;
    }
    out.Add("{\"identifiers\":[");
    bool comma = false;
    for (const auto& identity : selection->identifiers)
    {
        if (comma)
        {
            out.Add(",");
        }
        comma = true;
        out.Add("{\"literal\":");
        out.Utf8String(identity.literal);
        out.Add(",\"name\":");
        out.Utf8String(identity.name);
        out.Add(",\"span\":");
        out.Range(identity.span);
        out.Add(",\"oid\":[");
        for (std::size_t i = 0; i < identity.oid.size(); ++i)
        {
            if (i)
            {
                out.Add(",");
            }
            out.Utf8String(identity.oid[i]);
        }
        out.Add("],\"protocol\":");
        switch (identity.protocol)
        {
            case Protocol::Unknown:
                out.String("Unknown");
                break;
            case Protocol::Ap203e1:
                out.String("AP203e1");
                break;
            case Protocol::Ap203e2:
                out.String("AP203e2");
                break;
            case Protocol::Ap214:
                out.String("AP214");
                break;
            case Protocol::Ap242:
                out.String("AP242");
                break;
        }
        out.Add(",\"match\":");
        switch (identity.match)
        {
            case SchemaMatch::Malformed:
                out.String("Malformed");
                break;
            case SchemaMatch::UnknownName:
                out.String("UnknownName");
                break;
            case SchemaMatch::NameOnly:
                out.String("NameOnly");
                break;
            case SchemaMatch::LegacyAlias:
                out.String("LegacyAlias");
                break;
            case SchemaMatch::UnregisteredVersion:
                out.String("UnregisteredVersion");
                break;
            case SchemaMatch::RegisteredVersion:
                out.String("RegisteredVersion");
                break;
        }
        out.Add(",\"registry_id\":");
        if (identity.registryId.empty())
        {
            out.Add("null");
        }
        else
        {
            out.String(identity.registryId);
        }
        out.Add("}");
    }
    out.Add("],\"qualified_profile\":");
    if (selection->qualified)
    {
        out.Add("{\"registry_id\":");
        out.String(selection->qualified->registryId);
        out.Add(",\"profile\":");
        out.String(selection->qualified->profile);
        out.Add(",\"express_sha256\":");
        out.String(selection->qualified->expressSha256);
        if (selection->appliedInterpretation != SchemaInterpretation::DeclaredOnly)
        {
            out.Add(",\"binding_basis\":\"ExplicitApplicationInterpretation\",\"header_identity_verified\":false");
        }
        if (selection->qualified->kind == ProfileKind::Ap203N916Source)
        {
            out.Add(",\"binding_basis\":\"NameOnlyGeneralSourceSubset\",\"header_edition_verified\":false");
        }
        if (selection->qualified->kind != ProfileKind::Ap203e2Planar)
        {
            out.Add(",\"scope\":\"SourceDeclarationsPartUnitsOnly\"");
        }
        out.Add("}");
    }
    else
    {
        out.Add("null");
    }
    out.Add("}");
}

void report::WriteJson(Writer& out, const step_source::InspectOutcome& source, const Outcome& result)
{
    out.Add("{\"format_version\":\"step-semantics.v1\",\"scope\":\"SinglePartTypesSI\",\"status\":");
    out.String(StatusName(result.status));
    out.Add(",\"deferred_reason\":");
    if (result.status == Status::Deferred)
    {
        if (result.snapshot && !result.snapshot->PartDefinition().deferredRegionFaces.empty())
        {
            out.String("GeneralBrepRegionClassificationNotRun");
        }
        else
        {
            out.String(
                result.snapshot && (result.snapshot->SelectedProfile().kind == ProfileKind::Ap214e3Source ||
                                    result.snapshot->SelectedProfile().kind == ProfileKind::Ap214MbxSource) ?
                    "Ap214ConversionUnitEvaluationNotRun" :
                    "ConversionLengthInterpretationPendingTicket09"
            );
        }
    }
    else
    {
        out.Add("null");
    }
    out.Add(",\"source\":");
    step_source::report::WriteJson(out, source);
    out.Add(",\"schema_selection\":");
    WriteSchemaSelection(out, result.schemaSelection);
    if (result.requestedInterpretation != SchemaInterpretation::DeclaredOnly)
    {
        const bool applied = result.schemaSelection && result.schemaSelection->appliedInterpretation != SchemaInterpretation::DeclaredOnly;
        out.Add(",\"schema_interpretation\":{\"requested\":\"UnregisteredAp214As2010\",\"applied\":");
        out.Add(applied ? "true" : "false");
        if (applied)
        {
            out.Add(",\"source_binding_verified\":false");
        }
        out.Add(",\"original_binding_diagnostics\":");
        Diagnostics(out, source, result.interpretationDiagnostics);
        out.Add("}");
    }
    out.Add(",\"checks\":{\"schema_binding\":");
    out.String(result.binding);
    out.Add(",\"type_fields\":");
    out.String(result.types);
    const bool extendedSource = result.schemaSelection && result.schemaSelection->qualified &&
                                result.schemaSelection->qualified->kind != ProfileKind::Ap203e2Planar;
    if (extendedSource)
    {
        out.Add(",\"unit_dependencies\":");
        out.String(result.unitDependencies);
        out.Add(",\"geometry_dependencies\":");
        out.String(result.geometryDependencies);
    }
    out.Add(",\"single_part_profile\":");
    out.String(result.part);
    out.Add(",\"si_units\":");
    out.String(result.siUnits);
    if (result.policy == SourcePolicy::PreserveNonGeometric)
    {
        out.Add(",\"interoperability_approval\":");
        out.String(result.interoperability);
    }
    // Report partial checks only when those source predicates were attempted;
    // neither count grants complete source entity/type qualification.
    out.Add(",\"global_rules_47\":\"NotRun\",\"other_entity_where\":");
    out.String(extendedSource && (result.geometryDependencyChecks.entityPredicates || result.unitDependencyChecks.total) ? "Partial" : "NotRun");
    out.Add(",\"named_measure_predicates\":");
    out.String(
        extendedSource && (result.geometryDependencyChecks.typePredicates || result.unitDependencyChecks.typePredicates) ? "Partial" : "NotRun"
    );
    out.Add(
        ","
        "\"geometry_consistency\":\"NotRun\",\"conversion_units\":\"NotRun\","
        "\"uncertainty_evaluation\":\"NotRun\",\"native_build\":\"NotRun\",\"full_import\":\"NotRun\"}"
    );
    out.Add(",\"limits\":{\"dependency_visits\":");
    out.Number(result.limits.dependencyVisits);
    if (result.policy == SourcePolicy::PreserveNonGeometric)
    {
        out.Add(",\"preserved_diagnostics\":");
        out.Number(result.limits.preservedDiagnostics);
    }
    out.Add("},\"statistics\":{\"dependency_visits\":");
    out.Number(result.dependencyVisits);
    if (extendedSource)
    {
        out.Add(",\"geometry_dependency_checks\":");
        out.Number(result.geometryDependencyChecks.total);
        out.Add(",\"geometry_type_predicate_checks\":");
        out.Number(result.geometryDependencyChecks.typePredicates);
        out.Add(",\"geometry_entity_predicate_checks\":");
        out.Number(result.geometryDependencyChecks.entityPredicates);
        out.Add(",\"unit_dependency_checks\":");
        out.Number(result.unitDependencyChecks.total);
        out.Add(",\"unit_dimension_definitions\":");
        out.Number(result.unitDependencyChecks.unitDefinitions);
        out.Add(",\"unit_type_predicate_checks\":");
        out.Number(result.unitDependencyChecks.typePredicates);
        out.Add(",\"property_attachment_checks\":");
        out.Number(result.propertyAttachmentChecks);
    }
    out.Add("}");
    out.Add(",\"snapshot\":");
    if (!result.snapshot)
    {
        out.Add("null");
    }
    else
    {
        const auto& data = *result.snapshot;
        out.Add("{\"schema\":{\"name\":");
        out.String(data.SchemaIdentity().name);
        out.Add(",\"oid\":[");
        for (std::size_t i = 0; i < data.SchemaIdentity().oid.size(); ++i)
        {
            if (i)
            {
                out.Add(",");
            }
            // A qualified profile has matched an exact compiled numeric OID, never an arbitrary raw token.
            out.Add(data.SchemaIdentity().oid[i]);
        }
        out.Add("],\"express_sha256\":");
        out.String(data.SelectedProfile().expressSha256);
        out.Add(",\"profile\":");
        out.String(data.SelectedProfile().profile);
        out.Add("}");
        out.Add(",\"type_index_complete\":true,\"type_catalog_scope\":\"ProfileAncestorClosureAndRegisteredGuards\",\"type_populations\":{");
        bool first = true;
        for (const auto& type : data.RegisteredTypes())
        {
            if (!first)
            {
                out.Add(",");
            }
            first = false;
            out.String(type);
            out.Add(":");
            RecordIds(out, data, *data.Instances(type));
        }
        out.Add("},\"records\":[");
        const auto& records = data.Source().Records();
        for (std::size_t r = 0; r < records.size(); ++r)
        {
            if (r)
            {
                out.Add(",");
            }
            out.Add("{\"source_record_index\":");
            out.Number(r);
            out.Add(",\"id\":");
            out.String(records[r].id);
            out.Add(",\"types\":[");
            const auto types = data.TypesOf(r);
            for (std::size_t i = 0; i < types.size(); ++i)
            {
                if (i)
                {
                    out.Add(",");
                }
                out.String(types[i]);
            }
            out.Add("],\"used_by_representation\":");
            out.Add(data.UsedByRepresentation(r) ? "true" : "false");
            out.Add(",\"attributes\":[");
            bool firstAttribute = true;
            for (std::size_t c = 0; c < records[r].components.size(); ++c)
            {
                for (std::size_t p = 0; p < records[r].components[c].parameters.size(); ++p)
                {
                    const auto view = data.Attribute(r, c, p);
                    if (!firstAttribute)
                    {
                        out.Add(",");
                    }
                    firstAttribute = false;
                    out.Add("{\"component_index\":");
                    out.Number(c);
                    out.Add(",\"parameter_index\":");
                    out.Number(p);
                    out.Add(",\"name\":");
                    out.String(view.name);
                    out.Add(",\"declared_in\":");
                    out.String(view.declaredIn);
                    out.Add(",\"declared_type\":");
                    out.String(view.declaredType);
                    out.Add(",\"redeclared_by\":");
                    out.String(view.redeclaredBy);
                    out.Add(",\"optional\":");
                    out.Add(view.optional ? "true" : "false");
                    out.Add(",\"derived_slot\":");
                    out.Add(view.derivedSlot ? "true" : "false");
                    out.Add(",\"aggregate\":");
                    out.String(view.aggregate);
                    out.Add(",\"minimum\":");
                    out.Number(view.minimum);
                    out.Add(",\"maximum\":");
                    if (view.maximum)
                    {
                        out.Number(*view.maximum);
                    }
                    else
                    {
                        out.Add("null");
                    }
                    out.Add(",\"unique_elements\":");
                    out.Add(view.uniqueElements ? "true" : "false");
                    if (!view.nestedAggregates.empty())
                    {
                        out.Add(",\"nested_aggregates\":[");
                        for (std::size_t d = 0; d < view.nestedAggregates.size(); ++d)
                        {
                            const auto& dimension = view.nestedAggregates[d];
                            if (d)
                            {
                                out.Add(",");
                            }
                            out.Add("{\"aggregate\":");
                            out.String(dimension.aggregate);
                            out.Add(",\"minimum\":");
                            out.Number(dimension.minimum);
                            out.Add(",\"maximum\":");
                            if (dimension.maximum)
                            {
                                out.Number(*dimension.maximum);
                            }
                            else
                            {
                                out.Add("null");
                            }
                            out.Add(",\"unique_elements\":");
                            out.Add(dimension.uniqueElements ? "true" : "false");
                            out.Add("}");
                        }
                        out.Add("]");
                    }
                    out.Add(",\"span\":");
                    out.Range(view.value->span);
                    out.Add("}");
                }
            }
            out.Add("]}");
        }
        out.Add("]");
        if (result.policy == SourcePolicy::PreserveNonGeometric)
        {
            out.Add(",\"unattached_non_geometric_declarations\":");
            RecordIds(out, data, data.UnattachedNonGeometricDeclarations());
        }
        if (result.geometryScope == GeometryScope::GeneralBrep)
        {
            out.Add(",\"deferred_region_faces\":");
            RecordIds(out, data, data.PartDefinition().deferredRegionFaces);
        }
        out.Add(",\"part\":{\"roles\":{");
        first = true;
        for (const auto& role : data.PartDefinition().roles)
        {
            if (!first)
            {
                out.Add(",");
            }
            first = false;
            out.String(role.name);
            out.Add(":");
            out.String(records[role.recordIndex].id);
        }
        out.Add("},\"source_shape_role\":");
        out.String(data.PartDefinition().shapeRole);
        if (extendedSource)
        {
            out.Add(",\"property_attachments\":[");
            bool firstAttachment = true;
            for (const auto& attachment : data.PartDefinition().propertyAttachments)
            {
                if (!firstAttachment)
                {
                    out.Add(",");
                }
                firstAttachment = false;
                out.Add("{\"link\":");
                out.String(records[attachment.link].id);
                out.Add(",\"property_definition\":");
                out.String(records[attachment.propertyDefinition].id);
                out.Add(",\"characterized_definition\":");
                out.String(records[attachment.characterizedDefinition].id);
                out.Add(",\"product_definition\":");
                out.String(records[attachment.productDefinition].id);
                out.Add(",\"representation\":");
                out.String(records[attachment.representation].id);
                out.Add(",\"context\":");
                out.String(records[attachment.context].id);
                out.Add("}");
            }
            out.Add("]");
        }
        out.Add(",\"shape_geometry_verified\":false,\"apd_status\":");
        if (data.PartDefinition().applicationProtocolPresent)
        {
            out.String(data.PartDefinition().apdStatus);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"apd_schema_claim\":");
        if (data.PartDefinition().applicationProtocolPresent)
        {
            out.String(data.PartDefinition().apdSchemaClaim);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"apd_year_lexeme\":");
        if (data.PartDefinition().applicationProtocolPresent)
        {
            out.String(data.PartDefinition().apdYear);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"apd_global_rule\":\"NotRun\",\"inverse_roles\":{\"ac_context_elements\":");
        std::vector<std::size_t> owners;
        const auto findRole = [&](std::string_view name)
        {
            const auto& roles = data.PartDefinition().roles;
            return std::find_if(
                       roles.begin(),
                       roles.end(),
                       [&](const Role& role)
                       {
                           return role.name == name;
                       }
            )->recordIndex;
        };
        const auto ac = records[findRole("application_context")].id;
        for (auto ref : data.UsedIn(ac, "APPLICATION_CONTEXT_ELEMENT", "frame_of_reference"))
        {
            owners.push_back(data.Source().References()[ref].recordIndex);
        }
        RecordIds(out, data, owners);
        owners.clear();
        out.Add(",\"context_representations\":");
        const auto context = records[findRole("unit_context")].id;
        for (auto ref : data.UsedIn(context, "REPRESENTATION", "context_of_items"))
        {
            owners.push_back(data.Source().References()[ref].recordIndex);
        }
        RecordIds(out, data, owners);
        out.Add("}},\"units\":{\"length_unit\":");
        out.String(records[data.UnitContext().lengthUnit].id);
        out.Add(",\"plane_angle_unit\":");
        out.String(records[data.UnitContext().planeAngleUnit].id);
        out.Add(",\"solid_angle_unit\":");
        if (data.UnitContext().solidAngleUnit)
        {
            out.String(records[*data.UnitContext().solidAngleUnit].id);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"length_scale_mm\":");
        if (data.UnitContext().lengthPower10Mm)
        {
            Scale(out, *data.UnitContext().lengthPower10Mm);
        }
        else
        {
            out.Add("null");
        }
        out.Add(",\"conversion_status\":\"NotRun\",\"si_definitions\":[");
        for (std::size_t i = 0; i < data.UnitContext().siDefinitions.size(); ++i)
        {
            const auto& unit = data.UnitContext().siDefinitions[i];
            if (i)
            {
                out.Add(",");
            }
            out.Add("{\"source_id\":");
            out.String(records[unit.recordIndex].id);
            out.Add(",\"quantity\":");
            out.String(unit.quantity);
            out.Add(",\"name\":");
            out.String(unit.siName);
            out.Add(",\"prefix\":");
            if (unit.prefix.empty())
            {
                out.Add("null");
            }
            else
            {
                out.String(unit.prefix);
            }
            out.Add(",\"canonical_unit\":");
            out.String(unit.quantity == "Length" ? "mm" : unit.quantity == "PlaneAngle" ? "rad" : unit.quantity == "Mass" ? "kg" : "sr");
            out.Add(",\"scale\":");
            Scale(out, unit.canonicalPower10);
            out.Add(",\"dimensions\":[");
            for (std::size_t d = 0; d < unit.dimensions.size(); ++d)
            {
                if (d)
                {
                    out.Add(",");
                }
                out.Number(static_cast<std::size_t>(unit.dimensions[d]));
            }
            out.Add("]}");
        }
        out.Add("]");
        if (extendedSource)
        {
            out.Add(",\"dimension_definitions\":[");
            const auto& dimensions = data.UnitContext().dimensionDefinitions;
            for (std::size_t i = 0; i < dimensions.size(); ++i)
            {
                if (i)
                {
                    out.Add(",");
                }
                out.Add("{\"source_id\":");
                out.String(records[dimensions[i].recordIndex].id);
                out.Add(",\"nominal\":");
                out.String(dimensions[i].nominal);
                out.Add(",\"dimensions\":[");
                for (std::size_t d = 0; d < 7; ++d)
                {
                    if (d)
                    {
                        out.Add(",");
                    }
                    out.Add("{\"kind\":\"rational\",\"num\":");
                    out.String(dimensions[i].dimensions[d].numerator);
                    out.Add(",\"den\":");
                    out.String(dimensions[i].dimensions[d].denominator);
                    out.Add("}");
                }
                out.Add("]}");
            }
            out.Add("]");
        }
        out.Add(
            "},\"parameter_contract\":{\"coordinates\":\"Length\",\"direction_ratios\":\"Dimensionless\","
            "\"vector_magnitude\":\"Length\",\"line_parameter\":\"Dimensionless\",\"plane_parameters\":\"Length\","
            "\"target_mapping\":\"NotRun\",\"geometry_conversion\":\"NotRun\"}}"
        );
    }
    out.Add(",\"diagnostics\":");
    Diagnostics(out, source, result.diagnostics);
    if (result.policy == SourcePolicy::PreserveNonGeometric)
    {
        out.Add(",\"source_policy\":\"PreserveNonGeometric\",\"preserved_diagnostics\":");
        Diagnostics(out, source, result.preservedDiagnostics);
        out.Add(",\"source_conformance\":");
        out.String(result.snapshot ? (result.preservedDiagnostics.empty() ? "SelectedSubsetChecked" : "NonGeometricIssuesPreserved") : "NotQualified");
    }
    if (result.geometryScope == GeometryScope::GeneralBrep)
    {
        out.Add(",\"geometry_scope\":\"GeneralBrep\"");
    }
    out.Add("}\n");
}

bool InspectJson(const step_source::InspectOutcome& source, const Outcome& result, std::string& output)
{
    return Writer::Publish(
        source.limits.reportBytes,
        output,
        [&](Writer& out)
        {
            report::WriteJson(out, source, result);
        }
    );
}
} // namespace step_semantics
