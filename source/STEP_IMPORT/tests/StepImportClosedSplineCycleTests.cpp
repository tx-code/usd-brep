// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportCylindricalBandRepair.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmCrvOnSurf.h>
#include <SmNativeArchive.h>
#include <SmSTEPSurface.h>
#include <fstream>
#include <iostream>
#include <set>

namespace
{
using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
Options Settings()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
void Replace(std::string& text, const std::string& from, const std::string& to)
{
    const auto position = text.find(from);
    Require(position != std::string::npos, "public closed cubic mutation token");
    text.replace(position, from.size(), to);
}
void Native(SmBrep& native, double phase = .5, bool reversed = false, bool rational = false)
{
    SmAssertArray pointers, deep;
    Require(native.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "closed spline pointers");
    Require(native.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "closed spline LEVEL2");
    step_import_test::CheckNativeWorkBudget(native, .001);
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    native.GetEdges(edges);
    native.GetVertices(vertices);
    Require(edges.GetSize() == 2 && vertices.GetSize() == 2, "one source edge uses two children and one derived boundary vertex");
    std::set<std::pair<double, double>> intervals;
    for (const auto edge : edges)
    {
        const auto* composed = dynamic_cast<const SmCrvOnSurf*>(edge->GetCurve());
        const auto* curve = dynamic_cast<const SmBSplineCurve*>(composed ? composed->GetUVCurve() : edge->GetCurve());
        Require(curve && curve->GetDegree() == 3 && curve->GetDim() == (composed ? 2U : 3U), "full original cubic basis retained");
        ULONG count = 0;
        double* cp = nullptr;
        Require(curve->GetControlPointsPointer(count, cp) == SM_SUCCESS && count == 4 && cp, "cubic full net");
        Require(bool(curve->IsRational()) == rational, "retained rational source role");
        const double expected[4][3] = { { 0, 0, 0 }, { 4, 0, 0 }, { 4, 4, 0 }, { 0, 0, 0 } };
        for (unsigned i = 0; i < 4; ++i)
        {
            const double weight = rational && (i == 1 || i == 2) ? 2. : 1.;
            if (rational)
            {
                Require(cp[4 * i + 3] == weight, "full rational weights preserved");
            }
            // Native 2D storage may use the NL_NOZ sentinel. The source UV
            // has exactly two coordinates; sentinel Z is not source geometry.
            for (unsigned k = 0; k < curve->GetDim(); ++k)
            {
                Require(cp[4 * i + k] == weight * expected[reversed ? 3 - i : i][k], "full original coefficients unchanged in each child");
            }
        }
        SmTArray<double> knots;
        Require(curve->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 8, "full original knot count");
        for (unsigned i = 0; i < 8; ++i)
        {
            Require(knots[i] == (i < 4 ? 0 : 1), "full original parameter knots unchanged");
        }
        const auto interval = edge->GetInterval();
        intervals.emplace(interval.GetMin(), interval.GetMax());
    }
    Require(intervals.size() == 2, "two distinct child intervals");
    const auto first = *intervals.begin(), last = *intervals.rbegin();
    Require(
        first.first == 0 && last.second == 1 && first.second == last.first && first.second > 0 && first.second < 1,
        "children cover the whole natural domain with exactly one shared cut"
    );
    Near(first.second, phase, 1e-8, "numerically inverted phase agrees with the independent analytic parameter without snapping");
    bool original = false, derived = false;
    for (const auto vertex : vertices)
    {
        const auto p = vertex->GetPoint();
        const double x = 12 * phase * (1 - phase) * (rational ? 2. : 1.) / (rational ? 1 + 3 * phase * (1 - phase) : 1.);
        original = original || (p.x == x && p.y == x * (reversed ? 1 - phase : phase) && p.z == 0);
        derived = derived || (p.x == 0 && p.y == 0 && p.z == 0);
    }
    Require(original && derived, "source phase vertex unchanged and derived closure point explicit");
}
Outcome Accepted(const std::string& text)
{
    auto result = ImportBytes(text, Settings());
    if (!result.model)
    {
        for (const auto& d : result.report.diagnostics)
        {
            std::cerr << d.source.key << '\n';
        }
    }
    Require(result.status == Status::Accepted && result.model, "whole closed nonperiodic cubic import");
    Require(result.model->Source()->Source().Bytes() == text, "original STEP bytes retained");
    const auto& info = result.model->Info();
    Require(info.shapeRole == "OpenSurfaceModel" && info.faces == 1 && info.edges == 2 && info.vertices == 2, "source surface role retained");
    Require(!result.model->MaterialVolumeMm3(), "closure of a boundary does not grant material semantics");
    // Independent Green integral: C(t)=(12t(1-t),12t²(1-t),0),
    // area=72 integral_0^1 t²(1-t)² dt=12/5 square millimetres.
    Near(info.areaMm2, 12. / 5, 1e-5, "independent analytic enclosed area");
    // Independent vertical projections onto an interior point and an exact
    // nonvertex boundary point C(7/8), after the complete target is published.
    // These qualify representative native queries, not a global solver bound.
    for (const auto xy : { std::pair{ 1.5, .5 }, std::pair{ 1.3125, 1.1484375 } })
    {
        const Point query{ xy.first, xy.second, 2. };
        for (const auto closest : { result.model->Closest(query), result.model->Face().Closest(query) })
        {
            Near(closest.distanceMm, 2., 1e-6, "independent closest distance after cycle normalization");
            Near(closest.pointMm[0], xy.first, 1e-6, "independent closest X");
            Near(closest.pointMm[1], xy.second, 1e-6, "independent closest Y");
            Near(closest.pointMm[2], 0., 1e-6, "independent closest Z");
        }
    }
    const auto& topology = result.model->TopologyNormalization();
    Require(
        topology && topology->completed && topology->sourceCurveIntervalsCovered && topology->sourceVerticesPreserved,
        "owning completed normalization"
    );
    Require(
        topology->vertices.size() == 2 && topology->vertices[0].sourceVertexId == "60" && !topology->vertices[1].sourceVertexId,
        "derived vertex never masquerades as a source vertex"
    );
    const auto& uses = topology->sourceUseSegments;
    Require(uses.size() == 2, "one source oriented use retains two target incidences");
    for (const auto& use : uses)
    {
        Require(
            use.sourceFaceId == "23" && use.sourceBoundId == "24" && use.sourceLoopId == "25" && use.sourceOrientedEdgeId == "80" &&
                use.sourceEdgeId == "74",
            "all source use identities retained"
        );
    }
    Require(uses.front().target.startVertex == 0 && uses.back().target.endVertex == 0, "source use traversal begins and ends at its original vertex");
    Require(uses.front().target.endVertex == uses.back().target.startVertex, "child traversal joins at the derived closure");
    return result;
}
void CyclicQuintic(const std::filesystem::path& path, const std::filesystem::path& directory)
{
    std::ifstream in(path, std::ios::binary);
    Require(bool(in), "public cyclic quintic fixture");
    const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    auto result = ImportBytes(text, Settings());
    Require(result.status == Status::Accepted && result.model, "whole unclamped cyclic quintic import");
    const auto model = result.model;
    Require(model->Source()->Source().Bytes() == text, "original unclamped knot/control definition retained");
    // Independent exact Green integral of the authored degree-five polynomial
    // B-spline pieces on [5,13]: 2596859/113400. Schema closed_curve alone does
    // not authorize changing that basis or treating a shifted interval as its
    // selected natural domain. The area is a numerical observation, not a
    // replacement for complete source-use, UV, role and native validation.
    Near(model->Info().areaMm2, 2596859. / 113400., 1e-5, "independent exact cyclic-quintic area");
    Require(model->Info().shapeRole == "OpenSurfaceModel" && !model->MaterialVolumeMm3(), "cyclic boundary retains surface role");
    const auto closest = model->Closest({ 3, 3, 2 });
    Near(closest.distanceMm, 2., 1e-6, "independent cyclic-face query");
    const auto normalization = model->TopologyNormalization();
    Require(
        normalization && normalization->completed && normalization->sourceCurveIntervalsCovered && normalization->sourceVerticesPreserved,
        "cyclic quintic retains complete owning source coverage"
    );
    const auto native = [](SmBrep& brep)
    {
        SmAssertArray pointers, deep;
        Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize(), "cyclic quintic native pointers");
        Require(brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && !deep.GetSize(), "cyclic quintic native LEVEL2");
        SmTArray<SmEdge*> edges;
        brep.GetEdges(edges);
        Require(edges.GetSize() == 2, "two full-basis cyclic children");
        std::set<std::pair<double, double>> intervals;
        for (const auto edge : edges)
        {
            Require(edge->GetCurve()->GetDegree() == 5, "original quintic degree");
            const auto interval = edge->GetInterval();
            intervals.emplace(interval.GetMin(), interval.GetMax());
        }
        Require(
            intervals.size() == 2 && intervals.begin()->first == 5 && intervals.rbegin()->second == 13 &&
                intervals.begin()->second == intervals.rbegin()->first,
            "original full natural domain covered exactly once"
        );
        step_import_test::CheckNativeWorkBudget(brep, .001);
    };
    auto copy = model->CopyBrepForModeling();
    Require(copy.model != nullptr, "independent cyclic quintic modeling copy");
    copy.model->Edit(native);
    const auto archive = directory / "cyclic-quintic.smb";
    model->WriteNative(archive);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS &&
            bodies.GetSize() == 1,
        "complete cyclic quintic archive replay"
    );
    native(*bodies[0]);
    copy.model->Edit(
        [&](SmBrep& original)
        {
            step_import_test::CompareNativeDefinitions(original, *bodies[0]);
        }
    );
    result = {};
    Require(model->TopologyNormalization()->completed, "cyclic quintic evidence outlives mutable report");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public fixture and local output required");
        std::ifstream in(argv[1], std::ios::binary);
        Require(bool(in), "public closed cubic fixture");
        const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        auto result = Accepted(text);
        auto copy = result.model->CopyBrepForModeling();
        Require(copy.status == Status::Accepted && copy.model, "independent modeling copy");
        copy.model->Edit(
            [](SmBrep& native)
            {
                Native(native);
            }
        );
        const auto directory = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(directory);
        const auto archive = directory / "closed-cubic.smb";
        result.model->WriteNative(archive);
        SmContext context;
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> bodies;
        SmObjsDelete<SmCurve*> dc(&curves);
        SmObjsDelete<SmSurface*> ds(&surfaces);
        SmObjsDelete<SmBrep*> db(&bodies);
        Require(
            SmNativeArchive::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS &&
                bodies.GetSize() == 1,
            "complete native archive readback"
        );
        Native(*bodies[0]);
        copy.model->Edit(
            [&](SmBrep& original)
            {
                step_import_test::CompareNativeDefinitions(original, *bodies[0]);
            }
        );
        // Topological/curve/face senses compose independently. All variants
        // describe the same positive planar region; a reversed basis requires
        // the opposite native traversal, not a fitted reversal of coefficients.
        for (unsigned variant = 0; variant < 4; ++variant)
        {
            auto changed = text;
            if (variant == 0)
            {
                Replace(changed, "#60,#60,#40,.T.)", "#60,#60,#40,.F.)");
                Replace(changed, "#74,.T.)", "#74,.F.)");
            }
            if (variant == 1)
            {
                Replace(changed, "#24=FACE_OUTER_BOUND('',#25,.T.)", "#24=FACE_OUTER_BOUND('',#25,.F.)");
                Replace(changed, "#74,.T.)", "#74,.F.)");
            }
            if (variant == 2)
            {
                Replace(changed, "#23=ADVANCED_FACE('',(#24),#30,.T.)", "#23=ADVANCED_FACE('',(#24),#30,.F.)");
                Replace(changed, "#74,.T.)", "#74,.F.)");
            }
            if (variant == 3)
            {
                Replace(changed, "(#41,#42,#43,#41)", "(#41,#43,#42,#41)");
                Replace(changed, "#74,.T.)", "#74,.F.)");
            }
            auto accepted = Accepted(changed);
            auto independent = accepted.model->CopyBrepForModeling();
            Require(independent.model != nullptr, "composed-sense modeling copy");
            independent.model->Edit(
                [&](SmBrep& native)
                {
                    Native(native, .5, variant == 3);
                }
            );
        }
        auto quarter = text;
        Replace(quarter, "#50=CARTESIAN_POINT('',(3.,1.5,0.))", "#50=CARTESIAN_POINT('',(2.25,0.5625,0.))");
        auto shifted = Accepted(quarter);
        auto quarterCopy = shifted.model->CopyBrepForModeling();
        Require(quarterCopy.model != nullptr, "noncentral phase modeling copy");
        quarterCopy.model->Edit(
            [](SmBrep& native)
            {
                Native(native, .25);
            }
        );
        for (const auto& replacement : { std::string(".F.,.F."), std::string(".U.,.F."), std::string(".T.,.U."), std::string(".T.,.T.") })
        {
            auto unknown = text;
            Replace(unknown, ".UNSPECIFIED.,.T.,.F.,(4,4)", ".UNSPECIFIED.," + replacement + ",(4,4)");
            const auto refused = ImportBytes(unknown, Settings());
            Require(!refused.model && refused.status != Status::Accepted, "source closure and non-self-intersection declarations are never guessed");
        }
        auto open = text;
        Replace(open, "(#41,#42,#43,#41)", "(#41,#42,#43,#44)");
        open.insert(open.rfind("ENDSEC;"), "#44=CARTESIAN_POINT('',(0.00001,0.,0.));\n");
        const auto notClosed = ImportBytes(open, Settings());
        Require(
            !notClosed.model && std::any_of(
                                    notClosed.report.diagnostics.begin(),
                                    notClosed.report.diagnostics.end(),
                                    [](const auto& d)
                                    {
                                        return d.source.key == "ClosedSplineClosureUnestablished";
                                    }
                                ),
            "native endpoint closure must be established, not inferred from the source flag"
        );
        auto boundary = text;
        Replace(boundary, "#50=CARTESIAN_POINT('',(3.,1.5,0.))", "#50=CARTESIAN_POINT('',(0.,0.,0.))");
        const auto atBoundary = ImportBytes(boundary, Settings());
        Require(
            atBoundary.status == Status::Accepted && atBoundary.model && atBoundary.model->Info().edges == 1 &&
                atBoundary.model->Info().vertices == 1 && !atBoundary.model->TopologyNormalization(),
            "already valid natural-boundary traversal retains its existing one-edge representation"
        );
        auto distinct = text;
        Replace(distinct, "#60,#60,#40,.T.)", "#60,#61,#40,.T.)");
        distinct.insert(distinct.rfind("ENDSEC;"), "#61=VERTEX_POINT('',#50);\n");
        const auto separate = ImportBytes(distinct, Settings());
        Require(!separate.model, "coincident coordinates never merge distinct source endpoint identities");
        // A source 3D-master surface_curve retains its original pcurve data;
        // it must undergo the existing representation checks after segmentation.
        auto pcurve = text;
        Replace(pcurve, "#60,#60,#40,.T.)", "#60,#60,#140,.T.)");
        pcurve.insert(
            pcurve.rfind("ENDSEC;"),
            "#140=SURFACE_CURVE('',#40,(#141),.CURVE_3D.);\n"
            "#141=PCURVE('',#30,#142);\n"
            "#142=DEFINITIONAL_REPRESENTATION('',(#143),#149);\n"
            "#143=B_SPLINE_CURVE_WITH_KNOTS('',3,(#144,#145,#146,#144),.UNSPECIFIED.,.T.,.F.,(4,4),(0.,1.),.UNSPECIFIED.);\n"
            "#144=CARTESIAN_POINT('',(0.,0.));\n#145=CARTESIAN_POINT('',(4.,0.));\n#146=CARTESIAN_POINT('',(4.,4.));\n"
            "#149=(GEOMETRIC_REPRESENTATION_CONTEXT(2) PARAMETRIC_REPRESENTATION_CONTEXT() REPRESENTATION_CONTEXT('source UV','2D'));\n"
        );
        const auto sourceUv = ImportBytes(pcurve, Settings());
        if (!sourceUv.model)
        {
            for (const auto& d : sourceUv.report.diagnostics)
            {
                std::cerr << "PCURVE control: " << d.source.key << '\n';
            }
        }
        Require(sourceUv.status == Status::Accepted && sourceUv.model, "source 3D-master pcurve remains checked through both children");
        auto sourceUvCopy = sourceUv.model->CopyBrepForModeling();
        Require(sourceUvCopy.model != nullptr, "3D-master source UV modeling copy");
        sourceUvCopy.model->Edit(
            [](SmBrep& native)
            {
                Native(native);
            }
        );
        auto preferred = pcurve;
        Replace(preferred, ".CURVE_3D.", ".PCURVE_S1.");
        auto preferredUv = Accepted(preferred);
        auto preferredCopy = preferredUv.model->CopyBrepForModeling();
        Require(preferredCopy.model != nullptr, "source pcurve-priority modeling copy");
        preferredCopy.model->Edit(
            [](SmBrep& native)
            {
                Native(native);
            }
        );
        preferredUv.model->WriteNative(directory / "preferred-pcurve.smb");
        {
            SmContext readContext;
            SmTArray<SmCurve*> readCurves;
            SmTArray<SmSurface*> readSurfaces;
            SmTArray<long> readTrees;
            SmTArray<SmBrep*> readBodies;
            SmObjsDelete<SmCurve*> rc(&readCurves);
            SmObjsDelete<SmSurface*> rs(&readSurfaces);
            SmObjsDelete<SmBrep*> rb(&readBodies);
            const auto path = directory / "preferred-pcurve.smb";
            Require(
                SmNativeArchive::ReadPartFromFile(readContext, path.c_str(), readCurves, readSurfaces, readTrees, readBodies, SM_BINARY) ==
                        SM_SUCCESS &&
                    readBodies.GetSize() == 1,
                "source-pcurve composed archive readback"
            );
            Native(*readBodies[0]);
            preferredCopy.model->Edit(
                [&](SmBrep& original)
                {
                    step_import_test::CompareNativeDefinitions(original, *readBodies[0]);
                }
            );
        }
        preferredCopy.model->Edit(
            [](SmBrep& native)
            {
                SmTArray<SmEdge*> edges;
                native.GetEdges(edges);
                auto* actual = dynamic_cast<SmCrvOnSurf*>(edges[0]->GetCurve());
                Require(actual != nullptr, "preferred pcurve is retained as its exact composition");
                SmCurve* raw = nullptr;
                Require(actual->Copy(*native.GetContext(), raw) == SM_SUCCESS && raw, "independent composed identity control");
                std::unique_ptr<SmCurve> reference(raw);
                reference->SetOwner(nullptr);
                const auto identical = [&]()
                {
                    return bool(step_import::detail::RequireSameNativeCurveDefinition(*reference, *actual));
                };
                Require(identical(), "complete copied UV/carrier definitions match");
                const auto* uv = dynamic_cast<const SmBSplineCurve*>(actual->GetUVCurve());
                ULONG n = 0;
                double* uvNet = nullptr;
                Require(uv && uv->GetControlPointsPointer(n, uvNet) == SM_SUCCESS && n > 0 && uvNet, "mutable modeling copy UV control net");
                const auto savedUv = uvNet[0];
                uvNet[0] += .25;
                Require(!identical(), "a changed defining UV pole cannot pass the shared identity check");
                uvNet[0] = savedUv;
                Require(identical(), "restored UV identity");
                const auto* wrapper = dynamic_cast<const SmSTEPSurface*>(actual->GetBaseSurface());
                const auto* carrier = wrapper ? dynamic_cast<const SmBSplineSurface*>(wrapper->GetBaseSurface()) : nullptr;
                ULONG nu = 0, nv = 0;
                double* carrierNet = nullptr;
                Require(
                    carrier && carrier->GetControlPointsPointer(nu, nv, carrierNet) == SM_SUCCESS && nu && nv && carrierNet,
                    "owned canonical carrier net"
                );
                const auto savedCarrier = carrierNet[0];
                carrierNet[0] += .25;
                Require(!identical(), "a changed wrapped carrier pole cannot pass UV-only comparison");
                carrierNet[0] = savedCarrier;
                Require(identical(), "restored complete composition identity");
            }
        );

        auto badPcurve = pcurve;
        Replace(badPcurve, "#145=CARTESIAN_POINT('',(4.,0.))", "#145=CARTESIAN_POINT('',(4.,0.1))");
        Replace(badPcurve, "#146=CARTESIAN_POINT('',(4.,4.))", "#146=CARTESIAN_POINT('',(4.,3.9))");
        const auto conflict = ImportBytes(badPcurve, Settings());
        Require(
            !conflict.model && conflict.status != Status::Accepted,
            "extra evaluated witnesses cannot bypass an actual source representation conflict"
        );
        auto rational = text;
        Replace(
            rational,
            "#40=B_SPLINE_CURVE_WITH_KNOTS('closed nonperiodic cubic',3,(#41,#42,#43,#41),.UNSPECIFIED.,.T.,.F.,(4,4),(0.,1.),.UNSPECIFIED.);",
            "#40=(BOUNDED_CURVE() B_SPLINE_CURVE(3,(#41,#42,#43,#41),.UNSPECIFIED.,.T.,.F.) "
            "B_SPLINE_CURVE_WITH_KNOTS((4,4),(0.,1.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
            "RATIONAL_B_SPLINE_CURVE((1.,2.,2.,1.)) REPRESENTATION_ITEM('closed rational cubic'));"
        );
        Replace(rational, "#50=CARTESIAN_POINT('',(3.,1.5,0.))", "#50=CARTESIAN_POINT('',(3.4285714285714284,1.7142857142857142,0.))");
        const auto rationalResult = ImportBytes(rational, Settings());
        if (!rationalResult.model)
        {
            for (const auto& d : rationalResult.report.diagnostics)
            {
                std::cerr << d.source.key << '\n';
            }
        }
        Require(rationalResult.status == Status::Accepted && rationalResult.model, "positive rational closed source cycle");
        // Independent Green integral: x=24t(1-t)/(1+3t(1-t)), y=t*x.
        // A=1/2 integral x² dt = (288-1536*atanh(sqrt(3/7))/sqrt(21))/7.
        const double rationalArea = (288 - 1536 * std::atanh(std::sqrt(3. / 7)) / std::sqrt(21.)) / 7;
        Near(rationalResult.model->Info().areaMm2, rationalArea, 1e-5, "independent analytic rational enclosed area");
        auto rationalCopy = rationalResult.model->CopyBrepForModeling();
        Require(rationalCopy.model != nullptr, "rational closed cycle modeling copy");
        rationalCopy.model->Edit(
            [](SmBrep& native)
            {
                Native(native, .5, false, true);
            }
        );
        rationalResult.model->WriteNative(directory / "rational-cycle.smb");
        const auto calls = result.report.nativeCalls;
        const auto bytes = result.report.json.size();
        auto limits = Settings();
        limits.nativeLimits.nativeCalls = calls;
        Require(ImportBytes(text, limits).status == Status::Accepted, "exact native-call boundary");
        --limits.nativeLimits.nativeCalls;
        auto refused = ImportBytes(text, limits);
        Require(refused.status == Status::ResourceLimit && !refused.model, "native N-1 withholds every child and source ledger");
        limits = Settings();
        limits.nativeLimits.reportBytes = bytes;
        Require(ImportBytes(text, limits).status == Status::Accepted, "exact required-report boundary");
        --limits.nativeLimits.reportBytes;
        refused = ImportBytes(text, limits);
        Require(refused.status == Status::ResourceLimit && !refused.model, "report N-1 withholds the entire candidate");
        for (auto at : { std::size_t(1), calls / 3, calls / 2, calls - 1 })
        {
            limits = Settings();
            limits.execution.control = std::make_shared<ImportControl>();
            std::size_t seen = 0;
            limits.execution.progress = [&](Progress p)
            {
                if (p.phase == Phase::BeforeNativeCall && ++seen == at)
                {
                    limits.execution.control->RequestCancel();
                }
            };
            refused = ImportBytes(text, limits);
            Require(
                refused.status == Status::Cancelled && !refused.model,
                "cancellation during preparation, adoption and final checks publishes no partial model"
            );
        }
        for (auto phase : { Phase::NativeReady, Phase::BeforeReport, Phase::BeforeCommit })
        {
            limits = Settings();
            limits.execution.control = std::make_shared<ImportControl>();
            limits.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    limits.execution.control->RequestCancel();
                }
            };
            refused = ImportBytes(text, limits);
            Require(refused.status == Status::Cancelled && !refused.model, "late cancellation withholds snapshot and normalization evidence");
        }
        const auto owned = result.model->TopologyNormalization();
        result.report = {};
        Require(owned && owned->completed && owned->sourceUseSegments.size() == 2, "snapshot evidence remains owning after report release");
        copy.model->Edit(
            [](SmBrep& native)
            {
                Native(native);
            }
        );
        CyclicQuintic(std::filesystem::path(argv[1]).parent_path() / "closed_quintic_natural_domain.step", directory);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
