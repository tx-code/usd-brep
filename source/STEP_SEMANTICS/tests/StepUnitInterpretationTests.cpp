// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemantics.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace step_semantics;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Replace(std::string& text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "public synthetic replacement exists");
    text.replace(at, from.size(), to);
}
Outcome CheckUnits(
    const std::string& text,
    UnitInterpretation policy,
    SourcePolicy sourcePolicy = SourcePolicy::PreserveNonGeometric,
    GeometryScope scope = GeometryScope::GeneralBrep
)
{
    return Check(
        step_source::InspectBytes(text, step_source::Limits::PlanarFixtures()),
        {},
        sourcePolicy,
        scope,
        SchemaInterpretation::DeclaredOnly,
        policy
    );
}
bool Has(const Outcome& outcome, std::string_view key)
{
    return std::any_of(
        outcome.diagnostics.begin(),
        outcome.diagnostics.end(),
        [&](const auto& d)
        {
            return d.source.key == key;
        }
    );
}
void Refused(const std::string& text, std::string_view key)
{
    const auto result = CheckUnits(text, UnitInterpretation::VerifiedExplicitUnitDimensions);
    Require(!result.snapshot && Has(result, key), "unproven conversion publishes no snapshot with the expected diagnostic");
}
void Contract(std::string text)
{
    Replace(
        text,
        "AP203_CONFIGURATION_CONTROLLED_3D_DESIGN_OF_MECHANICAL_PARTS_AND_ASSEMBLIES_MIM_LF {1 0 10303 403 2 1 2}",
        "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}"
    );
    Replace(text, "'ap203_configuration_controlled_3d_design_of_mechanical_parts_and_assemblies',2011", "'automotive_design',2010");
    Replace(
        text,
        "#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
        "#11=(CONVERSION_BASED_UNIT('arbitrary label',#902) NAMED_UNIT(#903) PLANE_ANGLE_UNIT());\n"
        "#901=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));\n"
        "#902=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901);\n"
        "#903=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);"
    );
    const auto strict = CheckUnits(text, UnitInterpretation::DeclaredOnly);
    Require(!strict.snapshot && Has(strict, "DerivedSlotMarkerRequired"), "default derived-slot rejection is unchanged");
    for (const auto [policy, scope] : { std::pair{ SourcePolicy::Strict, GeometryScope::GeneralBrep },
                                        std::pair{ SourcePolicy::PreserveNonGeometric, GeometryScope::ExplicitOuterBounds } })
    {
        bool refused = false;
        try
        {
            CheckUnits(text, UnitInterpretation::VerifiedExplicitUnitDimensions, policy, scope);
        }
        catch (const std::invalid_argument&)
        {
            refused = true;
        }
        Require(refused, "invalid API combinations keep their existing exception contract");
    }

    auto result = CheckUnits(text, UnitInterpretation::VerifiedExplicitUnitDimensions);
    Require(
        result.snapshot && result.status == Status::CheckedWithDiagnostics && result.unitDependencies == "CheckedWithDiagnostics",
        "explicit angle dimensions pass the complete unit dependency and ownership gates"
    );
    auto held = result.snapshot;
    const auto& records = held->Source().Records();
    const auto unit = held->UnitContext().planeAngleUnit;
    Require(records[unit].id == "11" && held->UnitDimensionAssumptions().size() == 1, "the selected physical angle owns one assumption");
    const auto assumption = held->UnitDimensionAssumptions().front();
    Require(
        assumption.unit == unit && records[assumption.dimensions].id == "903" && assumption.factor && records[*assumption.factor].id == "902",
        "the receipt binds original unit, dimensions and measure identities"
    );
    auto dimensions = held->Field(unit, "dimensions");
    auto factor = held->Field(*assumption.factor, "value_component");
    Require(
        dimensions.Declaration().derivedSlot && dimensions.Value().kind == step_source::ValueKind::Reference &&
            dimensions.ReferenceTarget() == assumption.dimensions,
        "both the AP214 DERIVE contract and actual explicit reference remain"
    );
    Require(
        held->AppliedUnitInterpretation() == UnitInterpretation::VerifiedExplicitUnitDimensions &&
            held->UnitInterpretationDiagnostics().size() == 1 &&
            held->UnitInterpretationDiagnostics().front().source.key == "DerivedSlotMarkerRequired" && held->PreservedDiagnostics().empty(),
        "physical interpretation diagnostics are separate from nongeometric exceptions"
    );
    result = {};
    held.reset();
    Require(
        factor.Value().text == "PLANE_ANGLE_MEASURE" && factor.Value().children.front().text == "+0.0174532925199433E+0" &&
            dimensions.Value().text == "903",
        "owning field views preserve source spelling after the outcome is released"
    );

    auto nested = text;
    Replace(nested, "PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901", "PLANE_ANGLE_MEASURE(2.),#904");
    Replace(
        nested,
        "#903=DIMENSIONAL_EXPONENTS",
        "#904=(CONVERSION_BASED_UNIT('not a heuristic',#905) NAMED_UNIT(#903) PLANE_ANGLE_UNIT());\n"
        "#905=MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(0.5),#901);\n#903=DIMENSIONAL_EXPONENTS"
    );
    const auto chain = CheckUnits(nested, UnitInterpretation::VerifiedExplicitUnitDimensions);
    Require(chain.snapshot && chain.snapshot->UnitDimensionAssumptions().size() == 2, "all explicit units in a nested chain require their own proofs");

    auto bad = text;
    Replace(bad, "DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.)", "DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,1.)");
    Refused(bad, "ExplicitPlaneAngleUnitDimensionsConflict");
    for (const auto scalar : { "0.", "-0.", "-0.0174532925199433" })
    {
        bad = text;
        Replace(bad, "+0.0174532925199433E+0", scalar);
        Refused(bad, "SourceUnitScaleNonPositive");
    }
    bad = text;
    Replace(bad, "PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901", "PLANE_ANGLE_MEASURE(1.),#11");
    const auto cycle = CheckUnits(bad, UnitInterpretation::VerifiedExplicitUnitDimensions);
    Require(!cycle.snapshot && cycle.unitDependencies != "Passed", "cyclic factors cannot publish a partial unit receipt");
    bad = text;
    Replace(bad, "PLANE_ANGLE_MEASURE(+0.0174532925199433E+0),#901", "PLANE_ANGLE_MEASURE(1.),#10");
    const auto length = CheckUnits(bad, UnitInterpretation::VerifiedExplicitUnitDimensions);
    Require(!length.snapshot && length.unitDependencies != "Passed", "a factor based on a length unit cannot acquire an angle interpretation");
    auto marker = text;
    Replace(marker, "NAMED_UNIT(#903)", "NAMED_UNIT(*)");
    // The explicit dimension record would be an orphan after removing its only use.
    Replace(marker, "#903=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);", "");
    const auto conforming = CheckUnits(marker, UnitInterpretation::VerifiedExplicitUnitDimensions);
    Require(
        conforming.snapshot && conforming.snapshot->UnitDimensionAssumptions().empty() &&
            conforming.snapshot->AppliedUnitInterpretation() == UnitInterpretation::DeclaredOnly,
        "a valid source marker needs no unit assumption even when a policy was requested"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public planar source fixture required");
        std::ifstream input(argv[1], std::ios::binary);
        Require(bool(input), "public fixture opens");
        Contract({ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() });
        std::cout << "explicit plane-angle dimensions, source ownership and refusal controls passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
