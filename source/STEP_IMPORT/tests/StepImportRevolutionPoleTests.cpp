// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportRevolutionPole.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <SmSurfOfRevolution.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>
#include <string_view>

using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
namespace
{
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    return options;
}
void Verify(SmBrep& brep, bool lune, bool minimum, bool cone)
{
    SmAssertArray pointers, deep;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "complete exact-pole pointers");
    Require(brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "complete exact-pole LEVEL2");
    step_import_test::CheckNativeWorkBudget(brep, .001);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    brep.GetFaces(faces);
    brep.GetEdges(edges);
    brep.GetVertices(vertices);
    Require(
        faces.GetSize() == 1 && edges.GetSize() == (lune ? 2u : 3u) && vertices.GetSize() == (lune ? 2u : 3u),
        "original source face/edge/vertex inventory"
    );
    auto* surface = dynamic_cast<SmSurfOfRevolution*>(faces[0]->GetSurface());
    Require(surface, "spherical image retains its source revolution class");
    ULONG approximate = 0;
    const ULONG expectedPoles = lune ? SM_SS_VMIN | SM_SS_VMAX : minimum ? SM_SS_VMIN : SM_SS_VMAX;
    Require(
        surface->GetSingularities(nullptr, nullptr, &approximate) == expectedPoles && approximate == expectedPoles,
        "only the generator end is an exact native pole, with no additional approximate poles"
    );
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    SmBoolean swapped = TRUE;
    Require(
        surface->GetCanonical(generator, origin, axis, &swapped) == SM_SUCCESS && generator && !swapped,
        "unswapped independent original generator"
    );
    Require(generator->GetDegree() == (cone ? 1u : 2u) && generator->GetSTEPInterval() == SmExtent1d(-2, 8), "source degree and generator parameters");
    SmTArray<double> knots, weights;
    SmTArray<SmPoint3d> controls;
    Require(generator->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == (cone ? 4u : lune ? 8u : 6u), "complete source knot inventory");
    for (ULONG i = 0; i < knots.GetSize(); ++i)
    {
        Require(knots[i] == (i < (cone ? 2u : 3u) ? -2 : lune && i < 5 ? 3 : 8), "every original knot and multiplicity");
    }
    Require(
        generator->GetControlPolygon(controls, weights) == SM_SUCCESS &&
            controls.GetSize() == (cone ? 2u :
                                   lune ? 5u :
                                          3u) &&
            (weights.GetSize() == 0 || weights.GetSize() == controls.GetSize()),
        "complete rational generator coefficients"
    );
    for (ULONG i = 0; i < controls.GetSize(); ++i)
    {
        const ULONG control = minimum ? 2 - i : i;
        const double expectedY = cone ? (i == 0 ? 3 : 0) : lune ? (i == 0 || i == 4 ? 0 : 3) : (control == 2 ? 0 : 3);
        const double expectedZ = cone ? (i == 0 ? 0 : 3) : lune ? (i < 2 ? -3 : i == 2 ? 0 : 3) : (control == 0 ? 0 : 3);
        Require(controls[i].x == 0 && controls[i].y == expectedY && controls[i].z == expectedZ, "original generator controls never snapped or moved");
        if (weights.GetSize())
        {
            Require(weights[i] == (cone ? 1 : i % 2 ? std::sqrt(.5) : 1), "original rational generator weights");
        }
    }
    const auto q = std::sqrt(.5);
    for (const double t : { 0., .2, .7, 1. })
    {
        const bool lower = lune && t < .5;
        const double local = lune ? (lower ? 2 * t : 2 * t - 1) : minimum ? 1 - t : t;
        const double a = (1 - local) * (1 - local) + 2 * q * local * (1 - local);
        const double b = 2 * q * local * (1 - local) + local * local;
        const double denominator = (1 - local) * (1 - local) + 2 * q * local * (1 - local) + local * local;
        const double radius = cone ? 3 * (1 - t) : 3 * (lower ? b : a) / denominator;
        const double height = cone ? 3 * t : 3 * (lower ? -a : b) / denominator;
        for (const double degrees : { 0., 45., 90., 180., 360. })
        {
            SmPoint3d point;
            Require(surface->EvaluateSTEPPoint(SmPoint2d(degrees, -2 + 10 * t), point) == SM_SUCCESS, "source-parameter pole evaluation");
            const double radians = degrees * std::numbers::pi / 180;
            Near(point.x, -radius * std::sin(radians), 1e-9, "independent rational revolution x");
            Near(point.y, radius * std::cos(radians), 1e-9, "independent rational revolution y");
            Near(point.z, height, 1e-9, "independent rational generator z");
        }
    }
    unsigned circles = 0, lines = 0, poleVertices = 0;
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        const auto type = edges[i]->GetCurve()->GetType();
        Require(type == SmCircle_TYPE || (cone && type == SmLine_TYPE), "original source 3D definitions, no fitted cache or priority override");
        if (type == SmLine_TYPE)
        {
            ++lines;
        }
        else
        {
            ++circles;
        }
    }
    for (ULONG i = 0; i < vertices.GetSize(); ++i)
    {
        if (vertices[i]->GetPoint() == SmPoint3d(0, 0, 3) || (lune && vertices[i]->GetPoint() == SmPoint3d(0, 0, -3)))
        {
            ++poleVertices;
        }
    }
    Require(
        circles == (cone ? 1u :
                    lune ? 2u :
                           3u) &&
            lines == (cone ? 2u : 0u) && poleVertices == (lune ? 2u : 1u),
        "complete original curves and pole-vertex inventory"
    );
}
void RoundTrips(const std::shared_ptr<const ModelSnapshot>& model, const std::filesystem::path& directory, bool lune, bool minimum, bool cone)
{
    auto editable = model->CopyForModeling();
    Require(editable.status == Status::Accepted && editable.model, "independent exact-pole modeling copy");
    editable.model->Edit(
        [&](SmBrep& brep)
        {
            Verify(brep, lune, minimum, cone);
        }
    );
    for (const auto format : { SM_BINARY, SM_ASCII })
    {
        const auto path = directory / (format == SM_BINARY ? "revolution-pole.smb" : "revolution-pole.smt");
        editable.model->Edit(
            [&](SmBrep& brep)
            {
                SmTArray<SmCurve*> curves;
                SmTArray<SmSurface*> surfaces;
                SmTArray<long> trees;
                SmTArray<SmBrep*> bodies;
                bodies.Add(&brep); // Borrowed for this synchronous serialization only.
                Require(
                    SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, bodies, format, TRUE, FALSE) == SM_SUCCESS,
                    "exact-pole archive write"
                );
            }
        );
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> dc(&curves);
        SmObjsDelete<SmSurface*> ds(&surfaces);
        SmObjsDelete<SmBrep*> db(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, bodies, format) == SM_SUCCESS &&
                curves.GetSize() == 0 && surfaces.GetSize() == 0 && bodies.GetSize() == 1,
            "complete exact-pole archive read"
        );
        Verify(*bodies[0], lune, minimum, cone);
    }
}
void InteriorVisitRefused(const std::shared_ptr<const ModelSnapshot>& model)
{
    auto copy = model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "disposable native crossing negative control");
    copy.model->Edit(
        [](SmBrep& brep)
        {
            SmTArray<SmFace*> faces;
            SmTArray<SmEdge*> edges;
            SmTArray<SmVertex*> vertices;
            brep.GetFaces(faces);
            brep.GetEdges(edges);
            brep.GetVertices(vertices);
            SmEdge* equator = nullptr;
            for (const auto edge : edges)
            {
                SmVertex* first = nullptr;
                SmVertex* last = nullptr;
                edge->GetVertices(first, last);
                if (first->GetPoint().z == 0 && last->GetPoint().z == 0)
                {
                    equator = edge;
                }
            }
            Require(equator, "equatorial edge, separate from the existing pole vertex");
            SmVertex* first = nullptr;
            SmVertex* last = nullptr;
            equator->GetVertices(first, last);
            const auto a = first->GetPoint(), b = last->GetPoint();
            SmTArray<SmPoint3d> controls;
            controls.Add(a);
            controls.Add(a + SmVector3d(0, 0, 3));
            controls.Add(SmPoint3d(0, 0, 3));
            controls.Add(b + SmVector3d(0, 0, 3));
            controls.Add(b);
            SmTArray<double> weights, knots;
            for (const double weight : { 1., std::sqrt(.5), 1., std::sqrt(.5), 1. })
            {
                weights.Add(weight);
            }
            const auto interval = equator->GetInterval();
            knots.Add(interval.GetMin());
            knots.Add(interval.Evaluate(.5));
            knots.Add(interval.GetMax());
            SmTArray<ULONG> multiplicities;
            multiplicities.Add(3);
            multiplicities.Add(2);
            multiplicities.Add(3);
            SmBSplineCurve* raw = nullptr;
            Require(
                SmBSplineCurve::CreateCanonical(
                    *brep.GetContext(),
                    3,
                    2,
                    controls,
                    SM_CF_UNSPECIFIED,
                    multiplicities,
                    knots,
                    SM_KT_UNSPECIFIED,
                    &weights,
                    nullptr,
                    raw
                ) == SM_SUCCESS &&
                    raw,
                "exact two-meridian interior-pole detour"
            );
            std::unique_ptr<SmCurve> curve(raw);
            // This disposable copy intentionally invalidates its old import
            // qualification. Use the complete owner-updating mutator; the pole
            // checker reads actual defining geometry, independent of stale trims.
            const auto status = equator->Replace3DCurve(raw);
            if (raw->GetOwner() == equator)
            {
                curve.release();
            }
            Require(status == SM_SUCCESS && !curve, "native negative-control curve ownership");
            std::vector<SmVertex*> original(vertices.GetDataArray(), vertices.GetDataArray() + vertices.GetSize());
            detail::terminal_spline::Limits limits;
            limits.controlSlots = 100000;
            std::size_t work = 0;
            auto proof = detail::CheckRevolutionPoleVisits(
                *faces[0],
                original,
                limits,
                [&](std::size_t units) -> detail::Result<void>
                {
                    Require(units <= 2000000 - work, "bounded negative-control exact work");
                    work += units;
                    return {};
                },
                []() -> detail::Result<void>
                {
                    return {};
                }
            );
            Require(
                !proof && proof.error().key == "GeneralRevolutionPoleCrossingUnestablished",
                "an existing pole marker cannot hide another edge's interior visit"
            );
            Require(raw->ConvertTo2D() == SM_SUCCESS, "disposable dimensional-tag negative control");
            proof = detail::CheckRevolutionPoleVisits(
                *faces[0],
                original,
                limits,
                [](std::size_t) -> detail::Result<void>
                {
                    return {};
                },
                []() -> detail::Result<void>
                {
                    return {};
                }
            );
            Require(
                !proof && proof.error().key == "GeneralRevolutionPoleCurveProofUnsupported",
                "a 2D NOZ tag cannot be interpreted as a physical 3D coefficient"
            );
        }
    );
}
void Check(const std::string& source, const std::filesystem::path& directory, bool lune, bool minimum, bool cone)
{
    const auto options = Config();
    auto result = ImportBytes(source, options);
    Require(result.status == Status::Accepted && result.model, "complete unmarked source revolution pole region");
    auto model = result.model;
    const auto& info = model->Info();
    Require(
        info.shapeRole == "OpenSurfaceModel" && info.faces == 1 && info.edges == (lune ? 2u : 3u) && info.vertices == (lune ? 2u : 3u) &&
            info.freeBoundaryEdges == (lune ? 2u : 3u),
        "pole does not add edges/vertices or grant material semantics"
    );
    Require(!model->MaterialVolumeMm3() && !model->ClassifyMaterial({ 0, 0, 1 }), "surface stays outside material classification");
    // Integrate R^2*cos(phi) dphi du, phi=[0,pi/2], u=[.3,1.2].
    // The oracle is independent of native mass properties and source tessellation.
    Near(info.areaMm2, cone ? 4.05 * std::sqrt(2.) : lune ? 16.2 : 8.1, 1e-5, "independent spherical quarter-patch area");
    Near(
        info.boundaryMm,
        cone ? 6 * std::sqrt(2.) + 2.7 :
        lune ? 6 * std::numbers::pi :
               3 * std::numbers::pi + 2.7,
        1e-6,
        "independent two meridians and equatorial arc"
    );
    Near(info.centroidMm[0], (cone ? 2. : 3 * std::numbers::pi / 4) * (std::cos(1.2) - std::cos(.3)) / .9, 1e-5, "independent area centroid x");
    Near(info.centroidMm[1], (cone ? 2. : 3 * std::numbers::pi / 4) * (std::sin(1.2) - std::sin(.3)) / .9, 1e-5, "independent area centroid y");
    Near(info.centroidMm[2], cone ? 1. : lune ? 0 : 1.5, 1e-5, "independent area centroid z");
    const double root = std::sqrt(.5);
    Near(
        model
            ->Closest(
                cone ? Point{ -(1.5 + root) * std::sin(.7), (1.5 + root) * std::cos(.7), 1.5 + root } :
                       Point{ -4 * std::sin(.7) * root, 4 * std::cos(.7) * root, 4 * root }
            )
            .distanceMm,
        1,
        1e-6,
        "independent closest point inside the pole region"
    );
    Require(
        model->Source()->Source().Bytes() == source && !model->Repairs() && !model->TopologyNormalization() && !model->SurfaceCurve3dInterpretation(),
        "no source rewriting, repair, new pole topology or priority interpretation"
    );
    Require(result.report.pointerReports == 0 && result.report.deepReports == 0, "both original native validators pass");
    Require(
        result.report.json.find("NativeRevolutionPoleCurveIntersectionContainment") != std::string::npos,
        "report distinguishes established exact-pole containment"
    );
    const auto evidence = model->RevolutionPoleVisits();
    Require(
        !evidence.empty() && result.report.exactGeometryWork && result.report.exactGeometryWork->usedUnits > 0,
        "owning native pole evidence and request-wide exact-work receipt"
    );
    for (const auto& proof : evidence)
    {
        Require(
            proof.nativePoleMask == (lune    ? SM_SS_VMIN | SM_SS_VMAX :
                                     minimum ? SM_SS_VMIN :
                                               SM_SS_VMAX) &&
                proof.checkedEdges == (lune ? 4u : 3u) && proof.patches > 0 && proof.exactWorkUnits > 0 && proof.nativePoleRadiusMm > 0 &&
                proof.nativePoleRadiusMm <= .001,
            "every actual edge/pole pair uses the original native zone"
        );
    }
    auto bounded = options;
    bounded.nativeLimits.exactWorkUnits = result.report.exactGeometryWork->usedUnits;
    const auto exactWork = ImportBytes(source, bounded);
    Require(exactWork.status == Status::Accepted && exactWork.model, "exact cumulative work allowance");
    --bounded.nativeLimits.exactWorkUnits;
    const auto insufficientWork = ImportBytes(source, bounded);
    Require(insufficientWork.status == Status::ResourceLimit && !insufficientWork.model, "one unit short cannot publish");
    bounded = options;
    bounded.nativeLimits.reportBytes = result.report.json.size();
    const auto exactReport = ImportBytes(source, bounded);
    Require(
        exactReport.status == Status::Accepted && exactReport.model && exactReport.report.json == result.report.json,
        "complete deterministic pole evidence at the exact report limit"
    );
    --bounded.nativeLimits.reportBytes;
    const auto insufficientReport = ImportBytes(source, bounded);
    Require(insufficientReport.status == Status::ResourceLimit && !insufficientReport.model, "one report byte short cannot publish");
    if (!lune && !cone)
    {
        InteriorVisitRefused(model);
    }
    const auto original = model->Source();
    RoundTrips(model, directory, lune, minimum, cone);
    result = {};
    Require(model->Source() == original, "original defining geometry outlives reports and copies");
    Require(model->RevolutionPoleVisits().size() == evidence.size(), "clearing mutable reports preserves accepted pole evidence");
    for (const auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
    {
        auto stopped = options;
        stopped.execution.control = std::make_shared<ImportControl>();
        stopped.execution.progress = [&, phase](Progress p)
        {
            if (p.phase == phase)
            {
                stopped.execution.control->RequestCancel();
            }
        };
        const auto cancelled = ImportBytes(source, stopped);
        Require(cancelled.status == Status::Cancelled && !cancelled.model, "cancelled pole construction never publishes");
    }
}
void UnsupportedDefinitionRemainsExplicit(std::string source)
{
    const auto at = source.find("#960=SURFACE_CURVE('',#304,(#961),.CURVE_3D.);");
    Require(at != std::string::npos, "one public priority declaration");
    source.replace(at, std::strlen("#960=SURFACE_CURVE('',#304,(#961),.CURVE_3D.);"), "#960=SURFACE_CURVE('',#304,(#961),.PCURVE_S1.);");
    const auto result = ImportBytes(source, Config());
    Require(result.status == Status::Deferred && !result.model, "unproved composed definition is never replaced with a fitted NURBS");
    Require(
        std::any_of(
            result.report.diagnostics.begin(),
            result.report.diagnostics.end(),
            [](const auto& diagnostic)
            {
                return diagnostic.source.key == "GeneralRevolutionPoleCurveProofUnsupported";
            }
        ),
        "unsupported pole proof is explicit"
    );
    Require(
        !result.report.surfaceCurve3dInterpretation && result.report.source && result.report.source->Bytes() == source,
        "no priority override or source rewriting while declining a composed proof"
    );
}
void SharedDocumentBudget(const std::string& source)
{
    // Clone only this public synthetic DATA section. This intentionally simple
    // fixture authoring operation is not a STEP parser: its DATA strings and
    // comments contain no '#' characters. Both independent roots are requested.
    const auto marker = source.find("\nDATA;");
    Require(marker != std::string::npos, "known public DATA marker");
    const auto begin = source.find('\n', marker + 1) + 1; // Accept the fixture's LF or CRLF without rewriting its bytes.
    const auto end = source.rfind("ENDSEC;");
    Require(begin < end, "known public DATA section");
    const auto data = source.substr(begin, end - begin);
    std::string second;
    for (std::size_t i = 0; i < data.size();)
    {
        if (data[i] != '#')
        {
            second.push_back(data[i++]);
            continue;
        }
        const auto first = ++i;
        while (i < data.size() && data[i] >= '0' && data[i] <= '9')
        {
            ++i;
        }
        Require(i != first, "only canonical public entity labels");
        second += "#" + std::to_string(std::stoul(data.substr(first, i - first)) + 10000);
    }
    const auto product = second.find("'P0-PART'");
    Require(product != std::string::npos, "second public independent product");
    second.replace(product, std::strlen("'P0-PART'"), "'P0-PART-B'");
    const auto documentSource = source.substr(0, end) + second + source.substr(end);
    const auto options = Config();
    const auto result = ImportDocumentBytes(documentSource, options);
    if (result.status != Status::Accepted || !result.document)
    {
        for (const auto& diagnostic : result.report.diagnostics)
        {
            std::cerr << diagnostic.source.key << '\n';
        }
    }
    Require(
        result.status == Status::Accepted && result.document && result.document->PartCount() == 2 && result.document->BodyCount() == 2 &&
            result.report.definitions.size() == 2 && result.report.exactGeometryWork,
        "complete independent pole definitions share one request"
    );
    std::size_t sum = 0, first = 0;
    for (std::size_t i = 0; i < 2; ++i)
    {
        const auto& leaf = result.report.definitions[i];
        Require(
            leaf.checksPassed && leaf.checks && leaf.checks->exactGeometryWork && leaf.checks->exactGeometryWork->usedUnits > 0 &&
                !result.document->Body(i)->RevolutionPoleVisits().empty(),
            "each definition owns its actual pole evidence"
        );
        const auto units = leaf.checks->exactGeometryWork->usedUnits;
        sum += units;
        if (i == 0)
        {
            first = units;
        }
    }
    Require(sum == result.report.exactGeometryWork->usedUnits, "root ledger counts both definitions exactly once");
    auto limited = options;
    limited.nativeLimits.exactWorkUnits = first;
    const auto failed = ImportDocumentBytes(documentSource, limited);
    Require(
        failed.status == Status::ResourceLimit && !failed.document && failed.report.candidateDiscarded && failed.report.definitions[0].checksPassed,
        "second-definition exact-work failure discards the entire unpublished document"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3 || argc == 4, "public exact-pole STEP input and disposable archive directory");
        std::ifstream input(argv[1], std::ios::binary);
        const std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        std::filesystem::create_directories(argv[2]);
        Check(
            source,
            argv[2],
            argc == 4 && std::string_view(argv[3]) == "lune",
            argc == 4 && std::string_view(argv[3]) == "minimum",
            argc == 4 && std::string_view(argv[3]) == "cone"
        );
        UnsupportedDefinitionRemainsExplicit(source);
        if (argc == 3)
        {
            SharedDocumentBudget(source);
            auto marked = source;
            const auto at = marked.find("#170=FACE_BOUND");
            Require(at != std::string::npos, "public unmarked bound");
            marked.replace(at, std::strlen("#170=FACE_BOUND"), "#170=FACE_OUTER_BOUND");
            const auto markedOutput = std::filesystem::path(argv[2]) / "marked";
            std::filesystem::create_directories(markedOutput);
            Check(marked, markedOutput, false, false, false);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
