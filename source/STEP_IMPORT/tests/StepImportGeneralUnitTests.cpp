// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportPlan.h"
#include "StepImportUnits.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void Replace(std::string& text, std::string_view before, std::string_view after)
{
    const auto at = text.find(before);
    Require(at != std::string::npos, "synthetic source replacement exists");
    text.replace(at, before.size(), after);
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "public unit fixture opens");
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}
std::string AngleUnits(std::string text, bool explicitDimensions)
{
    Replace(
        text,
        "#11=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
        "#11=(CONVERSION_BASED_UNIT('degree label is not authority',#990000) NAMED_UNIT(*) PLANE_ANGLE_UNIT());\n"
        "#990000=PLANE_ANGLE_MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(0.5),#990002);\n"
        "#990002=(CONVERSION_BASED_UNIT('second half',#990003) NAMED_UNIT(*) PLANE_ANGLE_UNIT());\n"
        "#990003=MEASURE_WITH_UNIT(PLANE_ANGLE_MEASURE(+0.5E0),#990004);\n"
        "#990004=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));"
    );
    if (explicitDimensions)
    {
        Replace(text, "#990004=", "#990001=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);\n#990004=");
        for (const auto label : { "degree label is not authority", "second half" })
        {
            const auto start = text.find(std::string("CONVERSION_BASED_UNIT('") + label);
            const auto marker = text.find("NAMED_UNIT(*)", start);
            Require(start != std::string::npos && marker != std::string::npos, "conversion unit marker");
            text.replace(marker, std::string_view("NAMED_UNIT(*)").size(), "NAMED_UNIT(#990001)");
        }
    }
    return text;
}
void AngleContract(const std::filesystem::path& fixtures)
{
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    const auto raw = Read(fixtures / "source_pcurve_revolution_patch.step");
    std::size_t siBoundaries = 0, conversionBoundaries = 0;
    options.execution.progress = [&](step_import::Progress progress)
    {
        siBoundaries += progress.phase == step_import::Phase::BeforeNativeCall;
    };
    const auto baseline = step_import::ImportBytes(raw, options);
    Require(baseline.status == step_import::Status::Accepted && baseline.model, "independent SI-radian control is accepted");
    options.execution.progress = [&](step_import::Progress progress)
    {
        conversionBoundaries += progress.phase == step_import::Phase::BeforeNativeCall;
    };
    // Exact source scale is 1/4 radian despite the misleading unit name. Source
    // angular parameters are multiplied by four; lengths, generator parameters,
    // pcurve knots, direction ratios, topology and physical truth are unchanged.
    auto text = AngleUnits(raw, false);
    for (const auto [from, to] : { std::pair{ "PARAMETER_VALUE(1.8707963267948966)", "PARAMETER_VALUE(7.4831853071795864)" },
                                   std::pair{ "PARAMETER_VALUE(2.7707963267948966)", "PARAMETER_VALUE(11.0831853071795864)" } })
    {
        while (text.find(from) != std::string::npos)
        {
            Replace(text, from, to);
        }
    }
    for (const auto id : { "964", "965", "966" })
    {
        Replace(text, std::string("#") + id + "=CARTESIAN_POINT('',(1.2,", std::string("#") + id + "=CARTESIAN_POINT('',(4.8,");
    }
    auto ordinary = step_import::ImportBytes(text, options);
    Require(ordinary.status == step_import::Status::Accepted && ordinary.model, "conforming conversion-angle source imports without interpretation");
    Near(ordinary.model->Info().areaMm2, 3 * 7 * 0.9, 1e-3, "independent cylinder-sector area with source curve, trim and UV unit conversion");
    Require(!ordinary.model->MaterialVolumeMm3(), "angle conversion does not upgrade an open surface to material");
    bool uvChecked = false, trimChecked = false;
    for (const auto& mapping : ordinary.report.parameterMappings)
    {
        for (const auto& [name, values] : mapping.parameters)
        {
            if (name == "source_uv_to_native_step_scale")
            {
                Near(values[0], 45 / std::numbers::pi, 1e-13, "source revolution U uses the declared quarter-radian scale");
                Near(values[1], 1, 0, "source generator V parameters do not acquire physical angle or length scaling");
                uvChecked = true;
            }
            if (name == "angle_parameter_to_rad")
            {
                Near(values[0], .25, 0, "conic trim parameters use the same exact conversion chain");
                trimChecked = true;
            }
        }
    }
    Require(uvChecked && trimChecked, "both actual native parameter consumers retain their mappings");
    const auto owner = ordinary.model->Source();
    std::size_t visits = 0;
    const auto angle = step_import::detail::ResolvePlaneAngleScale(
        *owner,
        owner->UnitContext().planeAngleUnit,
        [&]() -> step_import::detail::Result<void>
        {
            ++visits;
            return {};
        }
    );
    Require(angle && *angle == step_import::Fraction(1, 4) && visits > 4, "nested exact angle factors resolve once in base-to-root order");
    Require(
        ordinary.report.nativeCalls == baseline.report.nativeCalls && conversionBoundaries == siBoundaries + visits,
        "conic and pcurve factories share one owning-model unit resolution, without rescanning for each angular consumer"
    );
    options.execution.progress = {};
    for (std::size_t stop = 1; stop <= visits; ++stop)
    {
        std::size_t current = 0;
        const auto cancelled = step_import::detail::ResolvePlaneAngleScale(
            *owner,
            owner->UnitContext().planeAngleUnit,
            [&]() -> step_import::detail::Result<void>
            {
                if (++current == stop)
                {
                    return tl::unexpected(step_import::detail::Failure{ step_import::Status::Cancelled, "AngleStop", "stop", 23, "Execution" });
                }
                return {};
            }
        );
        Require(
            !cancelled && current == stop && cancelled.error().key == "AngleStop" && cancelled.error().record == 23,
            "angle arithmetic propagates every cancellation boundary without converting it to missing-unit fallback"
        );
    }
    const auto wrongQuantity = step_import::detail::ResolvePlaneAngleScale(
        *owner,
        owner->UnitContext().lengthUnit,
        []() -> step_import::detail::Result<void>
        {
            return {};
        }
    );
    Require(!wrongQuantity && wrongQuantity.error().key == "GeneralConversionQuantityUnsupported", "length units cannot substitute for angle roots");

    auto interpretedText = text;
    for (const auto label : { "degree label is not authority", "second half" })
    {
        const auto start = interpretedText.find(std::string("CONVERSION_BASED_UNIT('") + label);
        const auto marker = interpretedText.find("NAMED_UNIT(*)", start);
        interpretedText.replace(marker, std::string_view("NAMED_UNIT(*)").size(), "NAMED_UNIT(#990001)");
    }
    Replace(interpretedText, "#990004=", "#990001=DIMENSIONAL_EXPONENTS(0.,0.,0.,0.,0.,0.,0.);\n#990004=");
    const auto strict = step_import::ImportBytes(interpretedText, options);
    Require(strict.status == step_import::Status::Rejected && !strict.model, "unselected explicit angle-slot interpretation stays refused");
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.unitInterpretation = step_semantics::UnitInterpretation::VerifiedExplicitUnitDimensions;
    auto interpreted = step_import::ImportBytes(interpretedText, options);
    Require(
        interpreted.status == step_import::Status::AcceptedUnderInterpretation && interpreted.model &&
            interpreted.model->Source()->UnitDimensionAssumptions().size() == 2 && interpreted.model->Source()->Source().Bytes() == interpretedText,
        "physical angle interpretation retains complete source and both original marker diagnostics"
    );
    Near(interpreted.model->Info().areaMm2, 18.9, 1e-3, "explicit dimensions do not change native geometry");
    const auto held = interpreted.model->Face();
    const auto copy = interpreted.model->CopyBrepForModeling();
    Require(copy.status == step_import::Status::Accepted && copy.model, "converted pcurve carriers copy independently");
    interpreted = {};
    Require(
        held.AppliedUnitInterpretation() == step_semantics::UnitInterpretation::VerifiedExplicitUnitDimensions,
        "owning face retains its angle interpretation after outcome disposal"
    );

    // A second owning context has a nonbinary scale: 400 * .5 * milliradian
    // is exactly 1/5 radian. This checks prefix application/cancellation and
    // catches reuse of the preceding model's 1/4 cache. Decimal source cuts
    // and U controls encode the same independent physical sector fivefold.
    auto fifth = text;
    Replace(fifth, "PLANE_ANGLE_MEASURE(0.5)", "PLANE_ANGLE_MEASURE(400.)");
    Replace(fifth, "SI_UNIT($,.RADIAN.)", "SI_UNIT(.MILLI.,.RADIAN.)");
    for (const auto [from, to] : { std::pair{ "PARAMETER_VALUE(7.4831853071795864)", "PARAMETER_VALUE(9.353981633974483)" },
                                   std::pair{ "PARAMETER_VALUE(11.0831853071795864)", "PARAMETER_VALUE(13.853981633974483)" } })
    {
        while (fifth.find(from) != std::string::npos)
        {
            Replace(fifth, from, to);
        }
    }
    for (const auto id : { "964", "965", "966" })
    {
        Replace(fifth, std::string("#") + id + "=CARTESIAN_POINT('',(4.8,", std::string("#") + id + "=CARTESIAN_POINT('',(6.0,");
    }
    auto fifthOptions = options;
    fifthOptions.sourcePolicy = step_semantics::SourcePolicy::Strict;
    fifthOptions.unitInterpretation = step_semantics::UnitInterpretation::DeclaredOnly;
    const auto fifthResult = step_import::ImportBytes(fifth, fifthOptions);
    Require(
        fifthResult.status == step_import::Status::Accepted && fifthResult.model,
        "nonbinary prefixed angle chain remains a separate owning context"
    );
    Near(fifthResult.model->Info().areaMm2, 18.9, 1e-3, "independent sector area with exact rational 1/5 angle scale");
    const auto fifthOwner = fifthResult.model->Source();
    const auto fifthScale = step_import::detail::ResolvePlaneAngleScale(
        *fifthOwner,
        fifthOwner->UnitContext().planeAngleUnit,
        []() -> step_import::detail::Result<void>
        {
            return {};
        }
    );
    Require(
        fifthScale && *fifthScale == step_import::Fraction(1, 5),
        "prefix is multiplied once and exact intermediate factors cancel before narrowing"
    );

    auto coneText = AngleUnits(Read(fixtures / "analytic_split_spline_cone.step"), true);
    Replace(coneText, "3.,0.6435011087932843868", "3.,2.5740044351731375472");
    const auto cone = step_import::ImportBytes(coneText, options);
    Require(cone.status == step_import::Status::AcceptedUnderInterpretation && cone.model, "conical semi-angle uses the same physical scale");
    Near(*cone.model->MaterialVolumeMm3(), 84 * std::numbers::pi, .01, "independent conical-frustum volume");

    auto wide = interpretedText;
    Replace(wide, "PLANE_ANGLE_MEASURE(0.5)", "PLANE_ANGLE_MEASURE(0.123456789012345678901)");
    const auto capacity = step_import::ImportBytes(wide, options);
    Require(
        capacity.status == step_import::Status::Indeterminate && !capacity.model &&
            std::any_of(
                capacity.report.diagnostics.begin(),
                capacity.report.diagnostics.end(),
                [](const auto& diagnostic)
                {
                    return diagnostic.source.key == "GeneralUnitScaleRange";
                }
            ),
        "native ratio capacity is unestablished evidence; factors are never rounded or relabeled source-invalid"
    );
    auto solidAngle = interpretedText;
    Replace(
        solidAngle,
        "#990004=(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.));",
        "#990004=(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT());"
    );
    const auto wrongBasis = step_import::ImportBytes(solidAngle, options);
    Require(
        wrongBasis.status == step_import::Status::Unsupported && !wrongBasis.model &&
            std::any_of(
                wrongBasis.report.diagnostics.begin(),
                wrongBasis.report.diagnostics.end(),
                [](const auto& d)
                {
                    return d.source.key == "GeneralConversionQuantityUnsupported";
                }
            ),
        "a solid-angle root with the same zero dimension vector cannot supply a plane-angle scale"
    );
    options.execution.control = std::make_shared<step_import::ImportControl>();
    options.execution.progress = [&](step_import::Progress p)
    {
        if (p.phase == step_import::Phase::NativeReady)
        {
            options.execution.control->RequestCancel();
        }
    };
    const auto cancelled = step_import::ImportBytes(interpretedText, options);
    Require(
        cancelled.status == step_import::Status::Cancelled && !cancelled.model && cancelled.report.candidateDiscarded,
        "angle interpretation cannot bypass final candidate cleanup"
    );
}
void ConversionUnitContract(const std::filesystem::path& source)
{
    std::ifstream input(source, std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const std::string original = "#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));";
    const auto at = text.find(original);
    Require(at != std::string::npos, "public fixture length declaration");
    text.replace(at, original.size(), "#10=(CONVERSION_BASED_UNIT('arbitrary',#1000) LENGTH_UNIT() NAMED_UNIT(#1001));");
    const auto end = text.find("ENDSEC;\nEND-ISO");
    Require(end != std::string::npos, "public fixture DATA end");
    text.insert(
        end,
        "#1000=MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0254),#1002);\n"
        "#1001=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n"
        "#1002=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));\n"
    );
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    auto outcome = step_import::ImportBytes(text, options);
    Require(outcome.status == step_import::Status::Accepted && outcome.model, "checked conversion source builds natively");
    auto model = outcome.model;
    outcome = {};
    const auto units = model->LengthUnits();
    Require(
        units.lengthToMm == step_import::Fraction(127, 5) && units.generalDefinitions.size() == 1,
        "owning model retains exact conversion after report disposal"
    );
    auto copy = units;
    copy.generalDefinitions.clear();
    Require(model->LengthUnits().generalDefinitions.size() == 1, "length normalization is an independent returned value");
    const auto sourceOwner = model->Source();
    std::size_t checkpoints = 0;
    const auto resolved = step_import::detail::ResolveLengthScale(
        *sourceOwner,
        sourceOwner->UnitContext().lengthUnit,
        [&]() -> step_import::detail::Result<void>
        {
            ++checkpoints;
            return {};
        }
    );
    Require(resolved && checkpoints > 4, "conversion control reaches exact-arithmetic checkpoints");
    for (std::size_t stop = 1; stop <= checkpoints; ++stop)
    {
        std::size_t visits = 0;
        const auto stopped = step_import::detail::ResolveLengthScale(
            *sourceOwner,
            sourceOwner->UnitContext().lengthUnit,
            [&]() -> step_import::detail::Result<void>
            {
                if (++visits == stop)
                {
                    return tl::unexpected(step_import::detail::Failure{ step_import::Status::Cancelled, "UnitStop", "stop", 23, "Execution" });
                }
                return {};
            }
        );
        Require(
            !stopped && visits == stop && stopped.error().key == "UnitStop" && stopped.error().record == 23,
            "exact conversion arithmetic returns cancellation without completing or visiting a later checkpoint"
        );
    }
    const auto& definition = units.generalDefinitions.front();
    Require(
        sourceOwner->Source().Records()[definition.factor].id == "1000" &&
            sourceOwner->Field(definition.factor, "unit_component").ReferenceTarget() == definition.baseUnit,
        "normalization retains actual typed factor/base identities"
    );
    Near(*model->MaterialVolumeMm3(), 6000 * 25.4 * 25.4 * 25.4, 1e-4, "independent converted box volume");
    Require(
        model->ClassifyMaterial({ 7 * 25.4, 7 * 25.4, 20 * 25.4 }) == step_import::MaterialPointClass::Inside,
        "material query uses canonical millimetres"
    );
    const auto wrongQuantity = step_import::detail::ResolveLengthScale(
        *sourceOwner,
        sourceOwner->UnitContext().planeAngleUnit,
        []() -> step_import::detail::Result<void>
        {
            return {};
        }
    );
    Require(
        !wrongQuantity && wrongQuantity.error().status == step_import::Status::Unsupported &&
            wrongQuantity.error().key == "GeneralConversionQuantityUnsupported" &&
            wrongQuantity.error().record == sourceOwner->UnitContext().planeAngleUnit,
        "angle units cannot substitute for a nominal length root; the refusal is a result"
    );
    auto wideText = text;
    const std::string factor = "LENGTH_MEASURE(0.0254)";
    const auto factorAt = wideText.find(factor);
    Require(factorAt != std::string::npos, "public conversion factor");
    wideText.replace(factorAt, factor.size(), "LENGTH_MEASURE(0.001234567890123456789)");
    auto wideOutcome = step_import::ImportBytes(wideText, options);
    Require(wideOutcome.status == step_import::Status::Accepted && wideOutcome.model, "wide factor builds after exact SI cancellation");
    auto wideModel = wideOutcome.model;
    wideOutcome = {};
    const auto wideUnits = wideModel->LengthUnits();
    Require(
        wideUnits.lengthToMm == step_import::Fraction(1234567890123456789LL, 1000000000000000000LL) &&
            wideUnits.generalDefinitions.front().factorValue.Numerator() == "1234567890123456789" &&
            wideUnits.generalDefinitions.front().factorValue.Denominator() == "1000000000000000000000" &&
            wideUnits.generalDefinitions.front().scaleMm.Denominator() == "1000000000000000000" && wideModel->Source()->Source().Bytes() == wideText,
        "owning exact unit evidence survives report disposal without narrowing source factors"
    );
    const double scale = 1.234567890123456789;
    Near(*wideModel->MaterialVolumeMm3(), 6000 * scale * scale * scale, 1e-6, "independent wide-factor box volume");
    auto lateCancel = options;
    lateCancel.execution.control = std::make_shared<step_import::ImportControl>();
    lateCancel.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::NativeReady)
        {
            lateCancel.execution.control->RequestCancel();
        }
    };
    const auto wideStopped = step_import::ImportBytes(wideText, lateCancel);
    Require(wideStopped.status == step_import::Status::Cancelled && !wideStopped.model, "wide arithmetic never bypasses final cancellation");
    auto cancel = options;
    cancel.execution.control = std::make_shared<step_import::ImportControl>();
    std::size_t boundaries = 0;
    cancel.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::SemanticsChecked && ++boundaries == 3)
        {
            cancel.execution.control->RequestCancel();
        }
    };
    const auto stopped = step_import::ImportBytes(text, cancel);
    Require(
        stopped.status == step_import::Status::Cancelled && !stopped.model && stopped.report.nativeCalls == 0,
        "conversion preflight remains cancellable before native allocation"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public box fixture and geometry fixture directory required");
        const auto planar = step_import::ImportFile(argv[1], step_import::Options::PlanarFixture());
        Require(
            planar.status == step_import::Status::Accepted && planar.model && planar.model->LengthUnits().lengthToMm == step_import::Fraction(1),
            "proved planar snapshots also own length normalization"
        );
        ConversionUnitContract(argv[1]);
        AngleContract(argv[2]);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
