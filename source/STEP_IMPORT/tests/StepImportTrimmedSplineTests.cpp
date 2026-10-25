// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyInputs.h"
#include "StepImportCurvePointConstruction.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportNativeData.h"
#include "StepImportUnits.h"

#include <SmAssertArray.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
using namespace step_import::detail::construction;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Near(Point actual, Point expected)
{
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        Require(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) < 1e-8, "independent public analytic point");
    }
}
std::string Replace(std::string text, std::string_view from, std::string_view to)
{
    const auto offset = text.find(from);
    Require(offset != std::string::npos, "public mutation target");
    text.replace(offset, from.size(), to);
    return text;
}
std::size_t Record(const step_semantics::Snapshot& source, std::string_view id)
{
    const auto* found = source.Source().Find(id);
    Require(found != nullptr, "public source identity");
    return std::size_t(found - source.Source().Records().data());
}
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
class Harness : public GeometryConstruction
{
public:

    Harness(std::shared_ptr<const step_semantics::Snapshot> source, const Options& options, Report& report, Execution& execution, SmContext& context)
        : GeometryConstruction(std::move(source), options, report, execution)
    {
        m_context = &context;
    }
    Result<void> Initialize()
    {
        auto units = ResolveLengthScale(
            *m_source,
            m_source->UnitContext().lengthUnit,
            [&]
            {
                return m_execution.Check(Phase::BeforeNativeCall);
            }
        );
        if (!units)
        {
            return tl::unexpected(std::move(units.error()));
        }
        scale = Q(units->scaleMm);
        m_report.units = UnitNormalization{ Id(m_source->UnitContext().lengthUnit), units->scaleMm, {}, std::move(units->definitions) };
        return {};
    }
};
struct Observed
{
    std::optional<Failure> failure;
    WireframeInfo group;
    std::array<Point, 3> points{};
    std::size_t definitions = 0, nativeCalls = 0, exactWork = 0;
    std::vector<ParameterMapping> mappings;
};
Observed Run(const std::string& text, Options options = Config(), bool repeat = false)
{
    auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    const auto checked = step_semantics::CheckAssemblySource(raw, {}, step_semantics::SourcePolicy::Strict);
    Require(checked.document && checked.checks.status == step_semantics::Status::Checked, "complete public wire-definition source");
    const auto source = checked.document->Definition(2);
    Require(source->PartDefinition().shapeRole == "WireframeModel", "actual independent wire-only source role");
    const auto curve = Record(*source, "975");
    Require(SupportsDetachedCurve(*source, curve), "shared type scope admits one-level trimmed explicit-knot spline");
    Require(!SupportsDetachedCurve(*source, Record(*source, "202")), "bare LINE remains outside detached finite-curve scope");
    Require(!SupportsDetachedCurve(*source, source->Source().Records().size()), "invalid source record cannot become a supported type");
    Report report;
    report.generalBrep = true;
    report.source = raw.source;
    report.sourceSha256 = raw.sha256;
    Execution execution(options, report);
    SmContext context;
    Harness inputs(source, options, report, execution, context);
    Observed result;
    const auto finish = [&]
    {
        result.nativeCalls = report.nativeCalls;
        result.exactWork = report.exactGeometryWork ? report.exactGeometryWork->usedUnits : 0;
        result.mappings = report.parameterMappings;
        return result;
    };
    if (auto initialized = inputs.Initialize(); !initialized)
    {
        result.failure = std::move(initialized.error());
        return finish();
    }
    CurvePointDefinitions definitions(inputs);
    WireframeEvidence evidence;
    result.group.representationId = "967";
    result.group.setId = "968";
    auto appended = definitions.Append(SourceRecord{ curve }, result.group, evidence);
    if (!appended)
    {
        result.failure = std::move(appended.error());
        return finish();
    }
    if (repeat)
    {
        appended = definitions.Append(SourceRecord{ curve }, result.group, evidence);
        Require(bool(appended), "same source wrapper can participate in another use without a second definition");
    }
    const auto owned = std::move(definitions).Finish();
    result.definitions = owned.CurveCount();
    const auto& info = result.group.curves.at(0);
    const auto& native = owned.CurveAt(CurveIndex{ info.nativeIndex });
    Require(native.GetContext() == &context && !native.GetOwner(), "detached curve has exactly its real allocating context");
    const auto interval = native.GetNaturalInterval();
    Require(
        interval.GetMin() == info.parameterDomain[0] && interval.GetMax() == info.parameterDomain[1],
        "stored detached definition owns exactly its selected parameter domain"
    );
    SmAssertArray findings;
    Require(native.AssertValid(&findings, SM_LEVEL_2, SM_WALK) && findings.GetSize() == 0, "independent full native curve validity");
    const std::array<double, 3> parameters{ interval.GetMin(), (interval.GetMin() + interval.GetMax()) / 2, interval.GetMax() };
    for (std::size_t i = 0; i < parameters.size(); ++i)
    {
        SmPoint3d point;
        Require(native.EvaluatePoint(parameters[i], point) == SM_SUCCESS, "actual native parameter evaluation");
        result.points[i] = { point.x, point.y, point.z };
    }
    return finish(); // Every native owner is destroyed before this real context.
}
std::string Case(const std::string& fixture, bool rational = false, bool forward = false, std::string_view preference = "PARAMETER")
{
    const std::string polynomial = "#980=B_SPLINE_CURVE_WITH_KNOTS('',2,(#10010,#10011,#10012),.UNSPECIFIED.,.F.,.F.,(3,3),(2.,5.),.UNSPECIFIED.);";
    const std::string weighted =
        "#980=(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#10010,#10011,#10012),.UNSPECIFIED.,.F.,.F.) "
        "B_SPLINE_CURVE_WITH_KNOTS((3,3),(2.,5.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
        "RATIONAL_B_SPLINE_CURVE((1.,2.,1.)) REPRESENTATION_ITEM(''));";
    auto text = Replace(
        fixture,
        "#980=B_SPLINE_CURVE_WITH_KNOTS('',2,(#100,#101,#102),.UNSPECIFIED.,.F.,.F.,(3,3),(2.,5.),.UNSPECIFIED.);",
        rational ? weighted : polynomial
    );
    text = Replace(
        text,
        "#975=TRIMMED_CURVE('',#980,(PARAMETER_VALUE(4.)),(PARAMETER_VALUE(3.)),.F.,.PARAMETER.);",
        "#975=TRIMMED_CURVE('',#980,(PARAMETER_VALUE(" + std::string(forward ? "2.75" : "4.25") +
            "),#10013),"
            "(#10014,PARAMETER_VALUE(" +
            std::string(forward ? "4.25" : "2.75") + ")),." + (forward ? "T" : "F") + ".,." + std::string(preference) + ".);"
    );
    const std::string lower = rational ? "(0.63636363636363636,1.0909090909090909,0.)" : "(0.5,0.75,0.)";
    const std::string upper = rational ? "(1.3636363636363636,1.0909090909090909,0.)" : "(1.5,0.75,0.)";
    text = Replace(
        text,
        "ENDSEC;\nEND-ISO",
        "#10010=CARTESIAN_POINT('',(0.,0.,0.));\n#10011=CARTESIAN_POINT('',(1.,2.,0.));\n"
        "#10012=CARTESIAN_POINT('',(2.,0.,0.));\n#10013=CARTESIAN_POINT(''," +
            (forward ? lower : upper) +
            ");\n"
            "#10014=CARTESIAN_POINT(''," +
            (forward ? upper : lower) + ");\nENDSEC;\nEND-ISO"
    );
    return text;
}
void Analytic(const std::string& fixture)
{
    for (bool rational : { false, true })
    {
        for (bool forward : { false, true })
        {
            const auto result = Run(Case(fixture, rational, forward), Config(), true);
            if (result.failure)
            {
                throw std::runtime_error(result.failure->key + ": " + result.failure->message);
            }
            Require(result.definitions == 1 && result.group.curves.size() == 2, "repeated uses retain one source/native owner");
            const auto& info = result.group.curves[0];
            Require(
                info.sourceId == "975" && info.basisId == "980" && info.nativeIndex == result.group.curves[1].nativeIndex &&
                    info.parameterDomain == std::array<double, 2>{ 2.75, 4.25 } && info.sourceForwardInNative == forward && info.trimEvidence,
                "source identity, basis identity, original native parameters and declared direction remain distinct"
            );
            const auto& proof = *info.trimEvidence;
            Require(
                proof.sourceParameters == (forward ? std::array<std::string, 2>{ "2.75", "4.25" } : std::array<std::string, 2>{ "4.25", "2.75" }) &&
                    proof.definitionErrorUpperMm <= 1e-9 && proof.sourceConsistencyUpperMm && *proof.sourceConsistencyUpperMm < 1e-12 &&
                    proof.proofPatches > 0,
                "owning evidence retains raw trim order and separate certified physical error bounds"
            );
            if (rational)
            {
                Near(result.points[0], { 7. / 11., 12. / 11., 0 });
                Near(result.points[1], { 1, 4. / 3., 0 });
                Near(result.points[2], { 15. / 11., 12. / 11., 0 });
            }
            else
            {
                Near(result.points[0], { .5, .75, 0 });
                Near(result.points[1], { 1, 1, 0 });
                Near(result.points[2], { 1.5, .75, 0 });
            }
            Require(
                std::any_of(
                    result.mappings.begin(),
                    result.mappings.end(),
                    [](const auto& mapping)
                    {
                        return mapping.kind == "WireframeTrimmedSplineEndpointRounding" &&
                               mapping.equation.find("source_trim_1_decimal=") != std::string::npos;
                    }
                ),
                "report mapping names endpoint rounding instead of claiming source/native same-t identity"
            );
        }
    }
    const auto unspecified = Run(Case(fixture, false, false, "UNSPECIFIED"));
    Require(!unspecified.failure, "UNSPECIFIED with two consistent forms uses the explicit parameter deterministically");
}
std::string NearKnot(const std::string& fixture)
{
    auto text = Case(fixture);
    text = Replace(
        text,
        "2,(#10010,#10011,#10012),.UNSPECIFIED.,.F.,.F.,(3,3),(2.,5.)",
        "1,(#10010,#10011,#10012),.UNSPECIFIED.,.F.,.F.,(2,1,2),(0.,1.,2.)"
    );
    text = Replace(text, "(PARAMETER_VALUE(4.25),#10013)", "(PARAMETER_VALUE(1.5),#10013)");
    text = Replace(text, "(#10014,PARAMETER_VALUE(2.75))", "(#10014,PARAMETER_VALUE(1.0000000000000002))");
    text = Replace(text, "#10011=CARTESIAN_POINT('',(1.,2.,0.))", "#10011=CARTESIAN_POINT('',(1.,0.,0.))");
    text = Replace(text, "#10013=CARTESIAN_POINT('',(1.5,0.75,0.))", "#10013=CARTESIAN_POINT('',(1.5,0.,0.))");
    text = Replace(text, "#10014=CARTESIAN_POINT('',(0.5,0.75,0.))", "#10014=CARTESIAN_POINT('',(1.0000000000000002,0.,0.))");
    return text;
}
void Boundaries(const std::string& fixture)
{
    const auto nearText = NearKnot(fixture);
    const auto near = Run(nearText);
    if (near.failure)
    {
        throw std::runtime_error(near.failure->key + ": near-knot public parameter control");
    }
    const auto& info = near.group.curves.at(0);
    Require(
        info.parameterDomain[0] == std::nextafter(1., 2.) && info.parameterDomain[1] == 1.5 && info.trimEvidence &&
            info.trimEvidence->definitionErrorUpperMm > 0 && info.trimEvidence->definitionErrorUpperMm < 1e-9,
        "native endpoint stays strictly above the nearby knot and decimal conversion has explicit nonzero evidence"
    );
    const auto expect = [&](const std::string& text, std::string_view key, Status status)
    {
        const auto result = Run(text);
        Require(
            result.failure && result.failure->key == key && result.failure->status == status && result.group.curves.empty(),
            "unsupported or unestablished trim never registers a whole-basis fallback"
        );
    };
    expect(
        Replace(Case(fixture), "#10013=CARTESIAN_POINT('',(1.5,0.75,0.))", "#10013=CARTESIAN_POINT('',(1.51,0.75,0.))"),
        "GeneralWireSplineTrimSelectorUnestablished",
        Status::Indeterminate
    );
    expect(Case(fixture, false, false, "CARTESIAN"), "GeneralWireSplineTrimPreferenceUnsupported", Status::Unsupported);
    expect(Replace(Case(fixture), "(PARAMETER_VALUE(4.25),#10013)", "(#10013)"), "GeneralWireSplineParameterTrimRequired", Status::Unsupported);
    auto collapsed = Replace(nearText, "(PARAMETER_VALUE(1.5),#10013)", "(PARAMETER_VALUE(1.00000000000000002),#10013)");
    collapsed = Replace(collapsed, "(#10014,PARAMETER_VALUE(1.0000000000000002))", "(#10014,PARAMETER_VALUE(1.00000000000000001))");
    expect(collapsed, "GeneralWireSplineTrimOrderUnestablished", Status::Indeterminate);
    auto limited = Config();
    limited.nativeLimits.exactWorkUnits = 1;
    const auto resource = Run(Case(fixture), limited);
    Require(
        resource.failure && resource.failure->status == Status::ResourceLimit && resource.failure->key == "ExactGeometryWorkLimit" &&
            resource.group.curves.empty(),
        "one shared exact ledger bounds source preparation, selector comparison and definition proof"
    );
    auto cancelled = Config();
    cancelled.execution.control = std::make_shared<ImportControl>();
    std::size_t visits = 0;
    cancelled.execution.progress = [&](Progress)
    {
        if (++visits == 8)
        {
            cancelled.execution.control->RequestCancel();
        }
    };
    const auto stopped = Run(Case(fixture), cancelled);
    Require(
        stopped.failure && stopped.failure->status == Status::Cancelled && stopped.failure->key == "ImportCancelled" && visits == 8 &&
            stopped.group.curves.empty(),
        "cooperative cancellation preserves its real status without registering or retrying a definition"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public assembly_box_and_wireframe.step fixture required");
        std::ifstream file(argv[1], std::ios::binary);
        Require(bool(file), "public source fixture exists");
        const std::string source{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        std::lock_guard lock(NativeMutex());
        Analytic(source);
        Boundaries(source);
        std::cout << "Native trimmed source spline construction contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
