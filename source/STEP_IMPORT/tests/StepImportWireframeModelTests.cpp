// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"

#include <SmBrep.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <SmNativeArchive.h>
#include <SmSurface.h>
#include <SmVertex.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
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
