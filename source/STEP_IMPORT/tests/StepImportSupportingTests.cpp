// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace step_import;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= 1e-9, "independent supporting value");
}
void Near(Point actual, Point expected)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        Near(actual[i], expected[i]);
    }
}
Options General()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(bool(file), "supporting public fixture");
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
void Replace(std::string& text, std::string_view before, std::string_view after)
{
    const auto at = text.find(before);
    Require(at != std::string::npos, "shared definition fixture edit");
    text.replace(at, before.size(), after);
}
void Add(std::string& text, std::string_view records)
{
    const auto at = text.find("ENDSEC;", text.find("DATA;"));
    Require(at != std::string::npos, "shared definition fixture data end");
    text.insert(at, records);
}
std::string SharedSource(std::string source)
{
    Add(source,
        "#910=GEOMETRIC_CURVE_SET('',(#911,#912,#940,#918));\n"
        "#918=CARTESIAN_POINT('',(7.,8.,9.));\n"
        "#920=GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION('',(#910,#904),#12);\n"
        "#931=SHAPE_REPRESENTATION_RELATIONSHIP('','',#900,#920);\n");
    // A nonzero, admitted source residual must survive definition reuse in CGR.
    Replace(source, "#914=CARTESIAN_POINT('',(0.,0.,0.))", "#914=CARTESIAN_POINT('',(0.,0.0000001,0.))");
    // Keep curves and points interspersed with the unchanged plane/frame path.
    Replace(source, "(#904,#970,#972,#911,#912,#940)", "(#912,#904,#911,#972,#940,#970)");
    return source;
}
void SharedDefinitions(const std::string& fixture, const std::filesystem::path& directory)
{
    const auto source = SharedSource(fixture);
    const auto first = ImportBytes(source, General());
    Require(first.status == Status::Accepted && first.model, "shared two-role candidate");
    auto duplicate = source;
    Replace(duplicate, "(#910,#904)", "(#910,#904,#919)");
    Add(duplicate, "#919=GEOMETRIC_CURVE_SET('',(#912,#911,#940));\n");
    std::vector<std::pair<Phase, std::size_t>> checkpoints;
    auto options = General();
    options.execution.progress = [&](Progress p)
    {
        checkpoints.emplace_back(p.phase, p.nativeCalls);
    };
    auto result = ImportBytes(duplicate, options);
    Require(result.status == Status::Accepted && result.model, "repeated cross-role definitions");
    // Only the two repeated wireframe curve uses add native bbox calls.
    Require(result.report.nativeCalls == first.report.nativeCalls + 2, "reused wireframe uses retain native charges");
    const auto wires = result.model->Wireframes();
    const auto support = result.model->SupportingGeometry().at(0);
    const auto a = wires.at(0).Info(), b = wires.at(1).Info();
    const auto s = support.Info();
    Require(a.points.size() == 2 && a.points[0].nativeIndex != a.points[1].nativeIndex, "equal points keep different source identities");
    Require(
        a.points[0].nativeIndex == b.points[0].nativeIndex && b.points[0].nativeIndex == s.points[0].nativeIndex,
        "same source point shared by both roles"
    );
    for (std::size_t i = 0; i < a.curves.size(); ++i)
    {
        Require(
            a.curves[i].nativeIndex == b.curves[i].nativeIndex && b.curves[i].nativeIndex == s.curves[i].nativeIndex,
            "same source curve shared by sets and roles"
        );
        Require(
            a.curves[i].parameterDomain == s.curves[i].parameterDomain && a.curves[i].sourceForwardInNative == s.curves[i].sourceForwardInNative,
            "shared native parameters and direction"
        );
        for (const auto parameter : a.curves[i].parameterDomain)
        {
            Near(wires[0].EvaluateCurve(i, parameter), support.EvaluateCurve(i, parameter));
        }
        const auto mapping = std::find_if(
            result.report.parameterMappings.begin(),
            result.report.parameterMappings.end(),
            [&](const auto& m)
            {
                return m.sourceGeometryId == a.curves[i].sourceId;
            }
        );
        Require(
            mapping != result.report.parameterMappings.end() && mapping->kind.starts_with("Wireframe") &&
                mapping->targetCurveIndex == a.curves[i].nativeIndex,
            "first role retains mapping kind and native index"
        );
    }
    const auto measured = [&](std::string_view kind)
    {
        const auto found = std::find_if(
            result.report.tolerances.begin(),
            result.report.tolerances.end(),
            [&](const auto& item)
            {
                return item.kind == kind;
            }
        );
        Require(found != result.report.tolerances.end(), "applicable role measurement present");
        return found->valueMm;
    };
    const auto residual = measured("MeasuredWireframeSourceTrimResidual");
    Require(residual > 0, "shared residual must not become zero");
    Near(residual, measured("MeasuredSupportingSourceTrimResidual"));
    Near(residual, 1e-7);
    const auto trace = checkpoints;
    checkpoints.clear();
    const auto replay = ImportBytes(duplicate, options);
    Require(
        replay.status == Status::Accepted && trace == checkpoints && replay.report.json == result.report.json,
        "repeat has identical complete report and execution sequence"
    );
    result.model->WriteNative(directory / "shared-two-role-definitions.smb");
    result = {};
    Near(support.EvaluatePlane(0, 2, 3), { 30, 42, 53 });
    Near(wires[0].EvaluateCurve(0, a.curves[0].parameterDomain[1]), support.EvaluateCurve(0, a.curves[0].parameterDomain[1]));

    auto pointOnly = duplicate;
    Replace(pointOnly, "(#912,#904,#911,#972,#940,#970)", "(#904,#911,#972,#970)");
    const auto noCurve = ImportBytes(pointOnly, General());
    Require(
        noCurve.model && std::none_of(
                             noCurve.report.tolerances.begin(),
                             noCurve.report.tolerances.end(),
                             [](const auto& item)
                             {
                                 return item.kind.starts_with("MeasuredSupporting");
                             }
                         ),
        "unexecuted supporting measurements have no zero rows"
    );

    const std::array families{ std::string("#950=CIRCLE('',#904,2.);\n"),
                               std::string("#950=ELLIPSE('',#904,3.,2.);\n"),
                               std::string(
                                   "#950=B_SPLINE_CURVE_WITH_KNOTS('',2,(#914,#951,#952),.UNSPECIFIED.,.F.,.F.,(3,3),(0.,1.),.UNSPECIFIED.);\n"
                                   "#951=CARTESIAN_POINT('',(1.,1.,0.));\n#952=CARTESIAN_POINT('',(2.,0.,0.));\n"
                               ),
                               std::string(
                                   "#950=(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#914,#951,#952),.UNSPECIFIED.,.F.,.F.) "
                                   "B_SPLINE_CURVE_WITH_KNOTS((3,3),(0.,1.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
                                   "RATIONAL_B_SPLINE_CURVE((1.,2.,1.)) REPRESENTATION_ITEM(''));\n"
                                   "#951=CARTESIAN_POINT('',(1.,1.,0.));\n#952=CARTESIAN_POINT('',(2.,0.,0.));\n"
                               ) };
    for (const auto& family : families)
    {
        auto data = source;
        Replace(data, "(#911,#912,#940,#918)", "(#911,#912,#940,#918,#950)");
        Replace(data, "(#912,#904,#911,#972,#940,#970)", "(#912,#904,#911,#972,#940,#970,#950)");
        Add(data, family);
        const auto imported = ImportBytes(data, General());
        Require(imported.status == Status::Accepted && imported.model, "shared supported curve family");
        const auto wire = imported.model->Wireframes()[0];
        const auto supporting = imported.model->SupportingGeometry()[0];
        const auto curve = wire.Info().curves.back();
        Require(curve.nativeIndex == supporting.Info().curves.back().nativeIndex, "curve family keeps one shared definition");
        const auto t = (curve.parameterDomain[0] + curve.parameterDomain[1]) / 2;
        Near(wire.EvaluateCurve(2, t), supporting.EvaluateCurve(2, t));
    }
}
void Basic(const std::string& source, const std::filesystem::path& directory)
{
    auto result = ImportBytes(source, General());
    Require(result.status == Status::Accepted && result.model, "complete supporting native candidate");
    Require(result.model->Wireframes().empty(), "CGR is not a wireframe shape representation");
    Require(
        result.model->Info().faces == 6 && result.model->Info().edges == 12 && result.model->Info().vertices == 8,
        "supporting members do not create material topology"
    );
    Near(*result.model->MaterialVolumeMm3(), 6000);
    Near(result.model->Closest({ 7, 8, 9 }).distanceMm, 4);
    Require(result.model->ClassifyMaterial({ 7, 8, 9 }) == MaterialPointClass::Inside, "source point not material boundary");
    const auto views = result.model->SupportingGeometry();
    Require(views.size() == 1, "owning supporting view");
    const auto info = views[0].Info();
    Require(info.representationId == "960" && info.relationshipId == "961", "source representation/link identities");
    Require(
        info.frames.size() == 2 && info.planes.size() == 1 && info.curves.size() == 2 && info.points.size() == 1,
        "all supporting members retained"
    );
    Near(info.points[0].pointMm, { 7, 8, 9 });
    Near(info.frames[1].frame.originMm, { 30, 40, 50 });
    Near(info.frames[1].frame.x, { 0, 1, 0 });
    Near(info.frames[1].frame.y, { 0, 0, 1 });
    Near(info.frames[1].frame.z, { 1, 0, 0 });
    Near(views[0].EvaluatePlane(0, 2, 3), { 30, 42, 53 });
    Near(views[0].ClosestPlane(0, { 35, 42, 53 }).pointMm, { 30, 42, 53 });
    Near(views[0].ClosestPlane(0, { 35, 42, 53 }).distanceMm, 5);
    const auto line = info.curves[0].parameterDomain, arc = info.curves[1].parameterDomain;
    Near(views[0].EvaluateCurve(0, line[0]), { 0, 0, 0 });
    Near(views[0].EvaluateCurve(0, line[1]), { 5, 0, 0 });
    Near(views[0].EvaluateCurve(1, (arc[0] + arc[1]) / 2), { std::sqrt(2.), std::sqrt(2.), 0 });
    Require(result.model->CopyForModeling().status == Status::Unsupported, "complete copy cannot omit supporting geometry");
    Require(bool(result.model->CopyBrepForModeling().model), "explicit independent body copy");
    // The production complete-part writer verifies all detached curve nets,
    // points and canonical axes/scales/domains on actual readback. Frames use
    // explicit adapters; no source plane or topological face is fabricated.
    result.model->WriteNative(directory / "complete-supporting.smb");
    for (const auto uv : { std::array{ std::numeric_limits<double>::quiet_NaN(), 0. }, std::array{ info.planes[0].parameterDomain[2] * 2, 0. } })
    {
        bool rejected = false;
        try
        {
            views[0].EvaluatePlane(0, uv[0], uv[1]);
        }
        catch (const std::out_of_range&)
        {
            rejected = true;
        }
        Require(rejected, "no native plane clamping or nonfinite query");
    }
    result = {};
    Near(views[0].EvaluatePlane(0, 2, 3), { 30, 42, 53 });
    Near(views[0].EvaluateCurve(0, line[1]), { 5, 0, 0 });
    Require(views[0].AppliedSourceInterpretation() == step_semantics::SchemaInterpretation::DeclaredOnly, "owning view retains source qualification");
}
void Execution(const std::string& source, const std::filesystem::path& bodyFixture)
{
    const auto body = ImportFile(bodyFixture, General());
    const auto complete = ImportBytes(source, General());
    Require(body.model && complete.model && complete.report.nativeCalls > body.report.nativeCalls, "extra native supporting calls");
    for (auto limit = body.report.nativeCalls; limit < complete.report.nativeCalls; ++limit)
    {
        auto options = General();
        options.nativeLimits.nativeCalls = limit;
        const auto failed = ImportBytes(source, options);
        Require(
            failed.status == Status::ResourceLimit && !failed.model && failed.report.candidateDiscarded,
            "late supporting limit discards complete candidate"
        );
        options = General();
        options.execution.control = std::make_shared<ImportControl>();
        options.execution.progress = [&](Progress progress)
        {
            if (progress.phase == Phase::BeforeNativeCall && progress.nativeCalls >= limit)
            {
                options.execution.control->RequestCancel();
            }
        };
        const auto cancelled = ImportBytes(source, options);
        Require(
            cancelled.status == Status::Cancelled && !cancelled.model && cancelled.report.candidateDiscarded,
            "late supporting cancellation releases geometry before context"
        );
    }
    for (const auto phase : { Phase::NativeReady, Phase::BeforeReport, Phase::ReportReady, Phase::BeforeCommit })
    {
        auto options = General();
        options.execution.control = std::make_shared<ImportControl>();
        options.execution.progress = [&](Progress progress)
        {
            if (progress.phase == phase)
            {
                options.execution.control->RequestCancel();
            }
        };
        const auto cancelled = ImportBytes(source, options);
        Require(
            cancelled.status == Status::Cancelled && !cancelled.model && cancelled.report.candidateDiscarded,
            "supporting publication shares final execution decision"
        );
    }
    auto options = General();
    options.nativeLimits.reportBytes = 64;
    const auto failed = ImportBytes(source, options);
    Require(failed.status == Status::ResourceLimit && !failed.model, "required supporting report cannot be omitted");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "supporting fixture, body fixture, output directory");
        const auto source = Read(argv[1]);
        const std::filesystem::path directory(argv[3]);
        std::filesystem::create_directories(directory);
        Basic(source, directory);
        SharedDefinitions(source, directory);
        auto rotated = source;
        const std::string oldNormal = "#973=DIRECTION('',(1.,0.,0.))";
        const auto at = rotated.find(oldNormal);
        Require(at != std::string::npos, "public rotated normal edit");
        rotated.replace(at, oldNormal.size(), "#973=DIRECTION('',(1.,1.,1.))");
        auto result = ImportBytes(rotated, General());
        Require(bool(result.model), "irrational rotated supporting frame");
        const auto projected = result.model->SupportingGeometry()[0].ClosestPlane(0, { 31, 42, 53 });
        // Independent plane x+y+z=120; subtract the excess equally per axis.
        Near(projected.pointMm, { 29, 40, 51 });
        Near(projected.distanceMm, std::sqrt(12.));
        auto interpreted = source;
        const std::string oid = "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}";
        interpreted.replace(interpreted.find(oid), oid.size(), "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}");
        auto options = General();
        options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
        options.schemaInterpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
        result = ImportBytes(interpreted, options);
        Require(result.status == Status::AcceptedUnderInterpretation && result.model, "explicit unverified supporting interpretation");
        const auto held = result.model->SupportingGeometry()[0];
        result = {};
        Require(held.AppliedSourceInterpretation() == options.schemaInterpretation, "owning supporting view retains unverified identity");
        Near(held.EvaluatePlane(0, 2, 3), { 30, 42, 53 });
        Execution(source, argv[2]);
        Execution(SharedSource(source), argv[2]);
        std::cout << "supporting geometry ownership/query/archive/publication controls passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
