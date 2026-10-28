// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"
#include "StepSchemaSelection.h"

#include <iostream>
#include <stdexcept>
using namespace step_semantics;
namespace
{
void Require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
ResolvedField ResolveAttribute(const Catalog& catalog, const char* owner, const char* field)
{
    const auto type = catalog.RequireType(owner);
    for (auto binding : catalog.Flattened(type))
    {
        if (catalog.FieldOf(binding).name == field)
        {
            return catalog.ResolveField(catalog.ancestors[type], binding);
        }
    }
    throw std::runtime_error("missing catalog field");
}
step_source::InspectOutcome Inspect(std::string identifier)
{
    return step_source::InspectBytes(
        "ISO-10303-21;HEADER;FILE_DESCRIPTION(('public catalog control'),'2;1');"
        "FILE_NAME('public.step','2026-10-28',('test'),('local'),'test','test','');FILE_SCHEMA(('" +
            identifier + "'));ENDSEC;DATA;#1=UNKNOWN($);ENDSEC;END-ISO-10303-21;",
        step_source::Limits::PlanarFixtures()
    );
}
} // namespace
int main()
{
    try
    {
        const auto& ap242 = Schema(ProfileKind::Ap242e2Source);
        const auto& ap214 = Schema(ProfileKind::Ap214MbxSource);
        Require(&ap242 != &ap214 && ap242.names.size() == 166, "independent AP242 catalog");
        const auto cbu = ResolveAttribute(ap242, "CONVERSION_BASED_UNIT", "dimensions");
        Require(
            !cbu.derivedSlot && cbu.redeclaredBy.empty() && cbu.field.type == "DIMENSIONAL_EXPONENTS",
            "AP242 conversion dimensions stay explicit"
        );
        Require(ResolveAttribute(ap214, "CONVERSION_BASED_UNIT", "dimensions").derivedSlot, "AP214 keeps its own derived dimensions");
        Require(ResolveAttribute(ap242, "SI_UNIT", "dimensions").derivedSlot, "SI dimensions still derived");
        const auto used = ResolveAttribute(ap242, "SHAPE_DEFINITION_REPRESENTATION", "used_representation");
        Require(
            !used.derivedSlot && used.field.type == "SHAPE_REPRESENTATION" && used.redeclaredBy == "SHAPE_DEFINITION_REPRESENTATION",
            "explicit inherited reference narrowing"
        );
        Require(ap242.Flattened(ap242.RequireType("SHAPE_DEFINITION_REPRESENTATION")).size() == 2, "narrowing adds no source slot");
        const auto items = ResolveAttribute(ap242, "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION", "items");
        Require(
            items.field.kind == FieldKind::Aggregate && items.field.minimum == 1 && items.field.unique == Unique::Set &&
                items.field.type == "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION_ITEMS",
            "aggregate narrowing retains source cardinality"
        );
        Require(
            ResolveAttribute(ap242, "STYLED_ITEM", "styles").field.minimum == 0 &&
                ResolveAttribute(ap214, "STYLED_ITEM", "styles").field.minimum == 1,
            "protocol-specific empty style set"
        );
        for (const char* field : { "curve_font", "curve_width", "curve_colour" })
        {
            Require(
                ResolveAttribute(ap242, "CURVE_STYLE", field).field.optional && !ResolveAttribute(ap214, "CURVE_STYLE", field).field.optional,
                "AP242 optional curve style fields"
            );
        }
        for (const char* field : { "marker", "marker_size", "marker_colour" })
        {
            Require(
                ResolveAttribute(ap242, "POINT_STYLE", field).field.optional && !ResolveAttribute(ap214, "POINT_STYLE", field).field.optional,
                "AP242 optional point style fields"
            );
        }
        const std::string name = "AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF";
        for (const char* oid : { "1 0 10303 442 1 1 4", "1 0 10303 442 3 1 4", "1 0 10303 442 4 1 4", "1 0 10303 442 7 1 4" })
        {
            auto source = Inspect(name + " {" + oid + "}");
            Require(source.source != nullptr, "public source control inspects");
            auto selected = SelectSchema(*source.source, GeometryScope::GeneralBrep);
            Require(
                selected.identifiers.size() == 1 && selected.identifiers[0].match == SchemaMatch::RegisteredVersion &&
                    selected.identifiers[0].protocol == Protocol::Ap242 && !selected.qualified,
                "identity recognition cannot qualify source checks"
            );
            for (const auto policy : { SchemaInterpretation::UnregisteredAp214As2010,
                                       SchemaInterpretation::Ap214NameOnlyAs2010,
                                       SchemaInterpretation::SourceBoundAp214As2010 })
            {
                SchemaInterpretationRequest request{ policy, policy == SchemaInterpretation::SourceBoundAp214As2010 ? source.sha256 : "" };
                Require(!SelectExplicitInterpretation(selected, request), "AP214 interpretation cannot acquire AP242");
            }
            auto checked = Check(source, {}, SourcePolicy::Strict, GeometryScope::GeneralBrep);
            Require(
                checked.status == Status::Unsupported && !checked.snapshot && checked.diagnostics[0].source.key == "UnsupportedSchemaBinding",
                "catalog alone cannot publish a source or native candidate"
            );
        }
        for (auto identifier : { name, name + " {1 0 10303 442 03 1 4}", name + " {1 0 10303 442 99 1 4}" })
        {
            auto source = Inspect(identifier);
            auto selected = SelectSchema(*source.source, GeometryScope::GeneralBrep);
            Require(
                selected.identifiers[0].match != SchemaMatch::RegisteredVersion && !selected.qualified,
                "no OID normalization or edition inference"
            );
        }
        Require(!ap242.Find("CAMERA_MODEL_D3") && !ap242.Find("MAPPED_ITEM"), "unimplemented branches remain outside closed catalog");
        Require(!ResolveAttribute(ap242, "CONVERSION_BASED_UNIT", "dimensions").derivedSlot, "interleaving catalogs cannot switch protocol state");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
