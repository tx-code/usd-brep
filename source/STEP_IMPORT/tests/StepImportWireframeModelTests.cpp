// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"
#include "StepImportWireframeLength.h"

#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <SmNativeArchive.h>
#include <SmSurface.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>

namespace
{
using namespace step_import;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Near(Point a, Point b)
{
    Require(std::hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]) < 1e-9, "independent analytic wire truth");
}
std::string Replace(std::string s, std::string_view a, std::string_view b)
{
    const auto p = s.find(a);
    Require(p != s.npos, "public mutation target");
    s.replace(p, a.size(), b);
    return s;
}
Options General()
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    o.numericalIntegration = NumericalIntegrationSettings{};
    return o;
}
AssemblyOutcome Import(const std::string& s, const Options& o)
{
    return ImportAssemblyBytes(s, o, { 8, 8, 8 });
}
void Accepted(const AssemblyOutcome& r)
{
    if (r.status != Status::Accepted)
    {
        std::cerr << StatusName(r.status) << '\n';
        for (const auto& d : r.report.diagnostics)
        {
            std::cerr << d.source.key << '\n';
        }
    }
    Require(r.status == Status::Accepted && r.assembly && !r.report.json.empty(), "complete wire assembly published");
}
void Archive(const WireframeModelSnapshot& m, const std::filesystem::path& path, bool point)
{
    m.WriteNative(path);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&breps);
    Require(
        SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS,
        "independent archive read"
    );
    Require(
        curves.GetSize() == 2 && surfaces.GetSize() == 0 && trees.GetSize() == 0 && breps.GetSize() == (point ? 1u : 0u),
        "archive contains only selected curves and explicit point adapters"
    );
    Require(
        curves[1]->GetNaturalInterval().GetMin() == 2.75 && curves[1]->GetNaturalInterval().GetMax() == 4.25,
        "archive cannot restore unused basis portions"
    );
    SmPoint3d p;
    Require(curves[1]->EvaluatePoint(3.5, p) == SM_SUCCESS, "archive interior query");
    Near({ p.x, p.y, p.z }, { 1, 1, 0 });
    if (point)
    {
        SmTArray<SmVertex*> vertices;
        SmTArray<SmFace*> faces;
        SmTArray<SmEdge*> edges;
        breps[0]->GetVertices(vertices);
        breps[0]->GetFaces(faces);
        breps[0]->GetEdges(edges);
        Require(vertices.GetSize() == 1 && faces.GetSize() == 0 && edges.GetSize() == 0, "first archived BRep is a point adapter, not material");
        p = vertices[0]->GetPoint();
        Near({ p.x, p.y, p.z }, { 7, 8, 9 });
    }
}
void TinyNativeLength(double expected)
{
    // A separate, caller-owned native curve tests numerical parameter scaling
    // without claiming that the STEP factory accepts this parameter range.
    // This quadratic Bezier is the SAME selected parabola, with endpoint chord
    // 1 mm and analytic length (sqrt(2)+asinh(1))/2 mm.
    SmContext context;
    SmTArray<SmPoint3d> controls;
    controls.Add(SmPoint3d(.5, .75, 0));
    controls.Add(SmPoint3d(1, 1.25, 0));
    controls.Add(SmPoint3d(1.5, .75, 0));
    SmTArray<ULONG> multiplicities;
    multiplicities.Add(3);
    multiplicities.Add(3);
    SmTArray<double> knots;
    knots.Add(0);
    knots.Add(1);
    SmBSplineCurve* raw = nullptr;
    const auto created =
        SmBSplineCurve::CreateCanonical(context, 3, 2, controls, SM_CF_UNSPECIFIED, multiplicities, knots, SM_KT_UNSPECIFIED, nullptr, nullptr, raw);
    std::unique_ptr<SmBSplineCurve> curve(raw);
    Require(created == SM_SUCCESS && curve && curve->GetContext() == &context && !curve->GetOwner(), "independent native quadratic ownership");
    const std::array<double, 2> interval{ 2.75e-14, 4.25e-14 };
    // EditParameterization delegates to N_BasisReparam, which assigns these
    // endpoint knots directly. This disposable control is not an imported
    // snapshot or a source normalization; the query itself must not edit it.
    Require(curve->EditParameterization(SmExtent1d(interval[0], interval[1])) == SM_SUCCESS, "native tiny parameter control");
    Require(
        curve->GetNaturalInterval().GetMin() == interval[0] && curve->GetNaturalInterval().GetMax() == interval[1],
        "native control retains the exact requested tiny domain"
    );
    auto length = detail::QueryWireframeCurveLength(*curve, interval, 1e-8);
    if (!length)
    {
        throw std::runtime_error(length.error().key);
    }
    Require(
        std::abs(*length - expected) < 1e-8 && std::abs(*length - 1) > .1,
        "tiny native parameter interval retains its nonstraight physical length, not the endpoint chord"
    );
    SmTArray<SmPoint3d> after;
    SmTArray<double> weights;
    Require(curve->GetControlPolygon(after, weights) == SM_SUCCESS && after.GetSize() == controls.GetSize(), "native control polygon survives query");
    for (ULONG i = 0; i < controls.GetSize(); ++i)
    {
        Near({ after[i].x, after[i].y, after[i].z }, { controls[i].x, controls[i].y, controls[i].z });
    }
    Require(
        curve->GetNaturalInterval().GetMin() == interval[0] && curve->GetNaturalInterval().GetMax() == interval[1] &&
            curve->GetContext() == &context && !curve->GetOwner(),
        "length integration preserves actual native parameters and ownership"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public wire fixture and output directory required");
        std::ifstream file(argv[1], std::ios::binary);
        Require(bool(file), "fixture exists");
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        const std::filesystem::path out(argv[2]);
        std::filesystem::create_directories(out);
        auto result = Import(text, General());
        Accepted(result);
        Require(
            result.assembly->DefinitionCount() == 2 && result.assembly->InstanceCount() == 2 && result.assembly->BodyCount(0) == 0 &&
                result.assembly->DefinitionRole(0) == "WireframeModel",
            "wire-only root retains its own role"
        );
        auto model = result.assembly->WireframeModel(0);
        Require(bool(model), "owning wire geometry");
        auto views = model->Wireframes();
        Require(views.size() == 1, "source set retained");
        const auto info = views[0].Info();
        Require(info.relationshipId.empty() && info.curves.size() == 2 && info.points.size() == 1, "no invented associated relationship");
        Near(views[0].EvaluateCurve(0, info.curves[0].parameterDomain[0]), { 2, 0, 0 });
        Near(views[0].EvaluateCurve(0, info.curves[0].parameterDomain[1]), { 6, 0, 0 });
        Require(std::abs(views[0].CurveLength(0, 1e-8) - 4) < 1e-8, "trimmed line length, not complete basis");
        // x=2s,y=4s(1-s), s=[1/4,3/4]. Integrate sqrt(4+(4-8s)^2)
        // analytically; orientation reverses the traversal, not its length.
        const double parabolaLength = (std::sqrt(2.) + std::asinh(1.)) / 2.;
        Require(std::abs(views[0].CurveLength(1, 1e-8) - parabolaLength) < 1e-8, "analytic trimmed parabola length");
        for (const double accuracy : { 0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
        {
            bool invalidAccuracy = false;
            try
            {
                (void)views[0].CurveLength(0, accuracy);
            }
            catch (const std::invalid_argument&)
            {
                invalidAccuracy = true;
            }
            Require(invalidAccuracy, "measurement accuracy must be explicit, finite and positive");
        }
        bool unestablished = false;
        try
        {
            (void)views[0].CurveLength(1, 1e-30);
        }
        catch (const std::runtime_error& error)
        {
            unestablished = std::string_view(error.what()) == "LinkedCurveLengthAccuracyUnestablished";
        }
        Require(unestablished, "an accuracy below numerical roundoff refuses instead of asserting or returning chord/zero");
        Require(
            model->Source()->Source().Bytes() == text && views[0].Info().curves[1].parameterDomain == info.curves[1].parameterDomain &&
                views[0].Info().curves[1].sourceForwardInNative == info.curves[1].sourceForwardInNative &&
                views[0].Info().curves[1].trimEvidence->sourceParameters == info.curves[1].trimEvidence->sourceParameters &&
                views[0].Info().curves[1].trimEvidence->definitionErrorUpperMm == info.curves[1].trimEvidence->definitionErrorUpperMm,
            "successful and failed length queries preserve source, native parameters, sense and defining evidence"
        );
        // A tiny STEP parameter range is still outside the current factory's
        // established support. Keep that refusal separate from the native
        // numerical regression; no source qualification is bypassed here.
        const auto tinyParameters = Replace(
            Replace(Replace(text, "(3,3),(2.,5.)", "(3,3),(2.E-14,5.E-14)"), "PARAMETER_VALUE(4.25)", "PARAMETER_VALUE(4.25E-14)"),
            "PARAMETER_VALUE(2.75)",
            "PARAMETER_VALUE(2.75E-14)"
        );
        auto tiny = Import(tinyParameters, General());
        Require(
            tiny.status == Status::Indeterminate && !tiny.assembly &&
                std::any_of(
                    tiny.report.diagnostics.begin(),
                    tiny.report.diagnostics.end(),
                    [](const auto& diagnostic)
                    {
                        return diagnostic.source.key == "GeneralWireSplineTrimOutsideNativeBasis";
                    }
                ),
            "tiny STEP domain retains the existing source-factory refusal without publishing geometry"
        );
        TinyNativeLength(parabolaLength);
        Require(
            info.curves[1].parameterDomain == std::array<double, 2>{ 2.75, 4.25 } && !info.curves[1].sourceForwardInNative,
            "source knot t and reverse sense"
        );
        Near(views[0].EvaluateCurve(1, 2.75), { .5, .75, 0 });
        Near(views[0].EvaluateCurve(1, 3.5), { 1, 1, 0 });
        Near(views[0].EvaluateCurve(1, 4.25), { 1.5, .75, 0 });
        Require(
            info.curves[1].trimEvidence && info.curves[1].trimEvidence->sourceParameters == std::array<std::string, 2>{ "4.25", "2.75" } &&
                info.curves[1].trimEvidence->definitionErrorUpperMm <= .001 &&
                info.curves[1].trimEvidence->sourceConsistencyUpperMm.value_or(1) <= .001,
            "owning source cut/definition/consistency evidence"
        );
        Near(views[0].Closest({ 7, 8, 9 }).pointMm, { 7, 8, 9 });
        bool outside = false;
        try
        {
            (void)views[0].EvaluateCurve(1, 2.);
        }
        catch (const std::out_of_range&)
        {
            outside = true;
        }
        Require(outside, "unused basis not exposed by queries");
        Require(
            !result.report.definitions.at(0).checks->numericalIntegration && !result.report.definitions[0].checks->nativeRole &&
                !result.report.definitions[0].checks->native,
            "no fabricated area, volume or BRep qualification"
        );
        Require(
            result.report.json.find("\"face_integration\":\"NotApplicable\"") != std::string::npos &&
                result.report.json.find("\"archive_point_brep_index\":0") != std::string::npos,
            "report geometry-only scope and archive point offset"
        );
        Require(
            result.report.json.find("WithinMaterialCollection") == std::string::npos &&
                result.report.json.find("WithinAssembly") != std::string::npos,
            "wire checks have assembly definition scope, never material collection scope"
        );
        const auto calls = result.report.nativeCalls;
        Require(calls > 0 && result.report.exactGeometryWork.has_value(), "bounded actual native and exact work");
        result = {};
        Require(model->Source()->Source().Bytes() == text, "source survives outcome release");
        Archive(*model, out / "wire.smp", true);
        model.reset();
        Near(views[0].EvaluateCurve(1, 3.5), { 1, 1, 0 });
        const auto noPoint = Replace(Replace(text, "(#30,#31,#32)", "(#30,#31)"), "#32=CARTESIAN_POINT('',(7.,8.,9.));\n", "");
        auto curves = Import(noPoint, General());
        Accepted(curves);
        Archive(*curves.assembly->WireframeModel(0), out / "curves.smp", false);
        for (const auto phase :
             { Phase::BeforeCapture, Phase::SemanticsChecked, Phase::BeforeNativeCall, Phase::NativeReady, Phase::ReportReady, Phase::BeforeCommit })
        {
            auto options = General();
            auto control = std::make_shared<ImportControl>();
            options.execution.control = control;
            options.execution.progress = [control, phase](Progress p)
            {
                if (p.phase == phase)
                {
                    control->RequestCancel();
                }
            };
            const auto cancelled = Import(text, options);
            Require(cancelled.status == Status::Cancelled && !cancelled.assembly, "cancellation never publishes partial wire geometry");
        }
        auto exact = General();
        exact.nativeLimits.nativeCalls = calls;
        Accepted(Import(text, exact));
        --exact.nativeLimits.nativeCalls;
        const auto limited = Import(text, exact);
        Require(
            limited.status == Status::ResourceLimit && !limited.assembly && limited.report.candidateDiscarded,
            "N-1 disposes unpublished geometry"
        );
        auto small = General();
        small.nativeLimits.reportBytes = 128;
        auto reportLimited = Import(text, small);
        Require(reportLimited.status == Status::ResourceLimit && !reportLimited.assembly, "mandatory report bounds complete publication");
        const auto conflict = Replace(text, "(1.5,0.75,0.)", "(1.5,4.75,0.)");
        const auto rejected = Import(conflict, General());
        Require(
            !rejected.assembly && rejected.status != Status::Accepted && rejected.report.candidateDiscarded,
            "parameter preference does not ignore source point disagreement"
        );
        const auto roundedPoint = Replace(text, "(7.,8.,9.)", "(10000000000000000.1,8.,9.)");
        const auto pointFailure = Import(roundedPoint, General());
        Require(
            pointFailure.status == Status::Indeterminate && !pointFailure.assembly && pointFailure.report.candidateDiscarded,
            "finite point coordinates cannot hide source decimal roundoff above the definition budget"
        );
        std::cout << "Wire geometry ownership, trim, archive and publication contracts passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
