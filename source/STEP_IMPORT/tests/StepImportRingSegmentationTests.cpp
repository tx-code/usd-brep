// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmCircle.h>
#include <SmCylinder.h>
#include <SmLine.h>
#include <SmNativeArchive.h>
#include <SmPlane.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <vector>

namespace
{
using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
using Interval = std::array<double, 2>;

void Replace(std::string& text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "public ring fixture mutation token");
    text.replace(at, from.size(), to);
}

Options Config(const std::string& text, bool repair)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    if (repair)
    {
        const auto raw = step_source::InspectBytes(text, options.sourceLimits);
        Require(bool(raw.source), "public ring source captures");
        options.cylindricalBandRepair = CylindricalBandRepairRequest{ raw.sha256, 1, 2, 100000, { { "7", "21", "182", { "172", "173" } } } };
    }
    return options;
}

void SamePoint(const SmPoint3d& actual, const Point& expected)
{
    // Source vertices are definitions, not fitting targets. A small displacement
    // must not pass merely because it lies inside a modeling tolerance.
    Require(actual.x == expected[0] && actual.y == expected[1] && actual.z == expected[2], "source vertex coordinates are unchanged");
}

void SameInterval(const SmExtent1d& a, const SmExtent1d& b)
{
    Require(a.GetMin() == b.GetMin() && a.GetMax() == b.GetMax(), "retained parameter domain identity");
}

void SameDomain(const SmExtent2d& a, const SmExtent2d& b)
{
    Require(
        a.GetMin().x == b.GetMin().x && a.GetMin().y == b.GetMin().y && a.GetMax().x == b.GetMax().x && a.GetMax().y == b.GetMax().y,
        "retained surface parameter domain identity"
    );
}

void SameFrame(const SmAxis2Placement& a, const SmAxis2Placement& b)
{
    const auto same = [](const auto& x, const auto& y)
    {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    Require(
        same(a.GetOriginRef(), b.GetOriginRef()) && same(a.GetXAxisRef(), b.GetXAxisRef()) && same(a.GetYAxisRef(), b.GetYAxisRef()),
        "retained analytic frame identity"
    );
}

void SameCompleteCurve(const SmCurve& a, const SmCurve& b)
{
    Require(a.GetType() == b.GetType() && a.GetDim() == b.GetDim() && a.GetDegree() == b.GetDegree(), "complete curve class identity");
    const auto x = dynamic_cast<const SmBSplineCurve*>(&a), y = dynamic_cast<const SmBSplineCurve*>(&b);
    Require(x && y, "complete native curve basis");
    ULONG nx = 0, ny = 0;
    double *px = nullptr, *py = nullptr;
    Require(
        x->GetControlPointsPointer(nx, px) == SM_SUCCESS && y->GetControlPointsPointer(ny, py) == SM_SUCCESS && nx == ny && px && py,
        "complete curve control net"
    );
    for (ULONG i = 0; i < nx * 4; ++i)
    {
        Require(px[i] == py[i], "child curves retain the entire original homogeneous net");
    }
    SmTArray<double> kx, ky;
    Require(x->GetKnotsAll(kx) == SM_SUCCESS && y->GetKnotsAll(ky) == SM_SUCCESS && kx.GetSize() == ky.GetSize(), "complete curve knot net");
    for (ULONG i = 0; i < kx.GetSize(); ++i)
    {
        Require(kx[i] == ky[i], "child curves retain original knots");
    }
    SameInterval(a.GetNaturalInterval(), b.GetNaturalInterval());
    SameInterval(a.GetSTEPInterval(), b.GetSTEPInterval());
    const auto ca = dynamic_cast<const SmCircle*>(&a), cb = dynamic_cast<const SmCircle*>(&b);
    if (ca || cb)
    {
        Require(ca && cb && ca->GetInsideOut() == cb->GetInsideOut(), "circle analytic sense identity");
        SameFrame(ca->GetPosition(), cb->GetPosition());
        Require(ca->GetXRadius() == cb->GetXRadius() && ca->GetYRadius() == cb->GetYRadius(), "circle analytic radii identity");
    }
}

void FullCircle(const SmCircle& circle, const Point& originalVertex)
{
    Require(circle.GetXRadius() == 3. && circle.GetYRadius() == 3. && !circle.GetInsideOut(), "original analytic circle radius and sense");
    const auto& frame = circle.GetPosition();
    SamePoint(frame.GetOriginRef(), { 0., 0., originalVertex[2] });
    SamePoint(frame.GetXAxisRef(), { 1., 0., 0. });
    SamePoint(frame.GetYAxisRef(), { 0., 1., 0. });
    const auto natural = circle.GetNaturalInterval();
    Require(natural.GetMax() > natural.GetMin(), "full circle native interval");
    const auto step = circle.GetSTEPInterval();
    Near(step.GetMax() - step.GetMin(), 360., 1e-10, "a child retains a complete analytic circle, not a trimmed arc definition");
    for (const auto t : { natural.GetMin(), natural.GetMax() })
    {
        SmPoint3d p;
        Require(circle.EvaluatePoint(t, p) == SM_SUCCESS, "original full circle endpoint evaluation");
        Near(
            p.DistanceBetween(SmPoint3d(originalVertex[0], originalVertex[1], originalVertex[2])),
            0.,
            1e-12,
            "complete circle basis retains its original phase vertex"
        );
    }
}

void NativeValid(SmBrep& native)
{
    SmAssertArray pointers, deep;
    Require(native.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "ring target pointer and geometry checks");
    Require(native.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "ring target level-2 validity");
    Require(native.IsManifoldSolid(), "ring target remains a manifold material solid");
}

void Coverage(std::vector<Interval> intervals, const SmExtent1d& complete)
{
    std::sort(intervals.begin(), intervals.end());
    double end = complete.GetMin();
    Require(!intervals.empty(), "every original circle retains a target use");
    for (const auto& interval : intervals)
    {
        Require(interval[0] == end && interval[1] > interval[0], "source interval coverage has no gap, overlap or empty child");
        end = interval[1];
    }
    Require(end == complete.GetMax(), "target children cover the complete original circle interval");
}

void CheckNativeTarget(SmBrep& native, const Point& upper, bool segmented, const std::optional<TopologyNormalizationInfo>& mapping)
{
    NativeValid(native);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    native.GetFaces(faces);
    native.GetEdges(edges);
    native.GetVertices(vertices);
    Require(
        faces.GetSize() == 3 && edges.GetSize() == (segmented ? 4U : 3U) && vertices.GetSize() == (segmented ? 3U : 2U),
        "minimal ring target face/edge/vertex inventory"
    );
    const std::array<Point, 2> originalPoints{ Point{ 0., 3., 0. }, upper };
    for (const auto& expected : originalPoints)
    {
        std::size_t found = 0;
        for (const auto vertex : vertices)
        {
            const auto p = vertex->GetPoint();
            found += p.x == expected[0] && p.y == expected[1] && p.z == expected[2] ? 1U : 0U;
        }
        Require(found == 1, "each original phase vertex is retained exactly once without movement or duplication");
    }
    SmFace* cylinder = nullptr;
    for (const auto face : faces)
    {
        if (dynamic_cast<const SmCylinder*>(face->GetSurface()))
        {
            Require(cylinder == nullptr, "one original cylindrical face");
            cylinder = face;
        }
        else
        {
            Require(dynamic_cast<const SmPlane*>(face->GetSurface()) != nullptr, "original planar cap support");
        }
    }
    Require(cylinder != nullptr, "original cylindrical support remains analytic");
    SmTArray<SmLoop*> loops;
    cylinder->GetLoops(loops);
    SmTArray<SmEdgeuse*> cylinderUses;
    cylinder->GetUpwardEdgeuses(cylinderUses);
    Require(loops.GetSize() == 1 && cylinderUses.GetSize() == (segmented ? 5U : 4U), "one cut cylindrical loop retains both rings and two seam uses");
    std::map<std::string, std::vector<SmEdge*>> rings;
    SmEdge* seam = nullptr;
    for (const auto edge : edges)
    {
        SmTArray<SmEdgeuse*> uses;
        Require(edge->GetEdgeuses(uses) == SM_SUCCESS && uses.GetSize() == 4, "every child is shared through complete manifold incidence");
        if (const auto circle = dynamic_cast<const SmCircle*>(edge->GetCurve()))
        {
            const double z = circle->GetPosition().GetOriginRef().z;
            Require(z == 0. || z == 7., "source circle center identity");
            const auto id = z == 0. ? "130" : "131";
            rings[id].push_back(edge);
            FullCircle(*circle, z == 0. ? originalPoints[0] : originalPoints[1]);
            SmTArray<SmFace*> adjacent;
            edge->GetFaces(adjacent);
            bool onCylinder = false;
            for (const auto face : adjacent)
            {
                onCylinder = onCylinder || face == cylinder;
            }
            Require(adjacent.GetSize() == 2 && onCylinder, "every ring child is shared by the cylinder and its original cap");
        }
        else
        {
            Require(seam == nullptr && dynamic_cast<const SmLine*>(edge->GetCurve()), "one derived exact meridian");
            seam = edge;
        }
    }
    Require(seam && rings.size() == 2, "both source circles and exactly one meridian survive");
    SmPoint3d seamStart, seamEnd;
    Require(
        seam->GetCurve()->EvaluatePoint(seam->GetInterval().GetMin(), seamStart) == SM_SUCCESS &&
            seam->GetCurve()->EvaluatePoint(seam->GetInterval().GetMax(), seamEnd) == SM_SUCCESS,
        "derived meridian endpoint evaluation"
    );
    Near(seamStart.x, seamEnd.x, 1e-12, "derived meridian has constant cylinder X");
    Near(seamStart.y, seamEnd.y, 1e-12, "derived meridian has constant cylinder Y");
    Near(seamStart.x * seamStart.x + seamStart.y * seamStart.y, 9., 1e-12, "derived meridian remains on the original cylinder");
    Near(std::abs(seamStart.z - seamEnd.z), 7., 1e-12, "derived meridian covers precisely the original height");
    std::size_t forward = 0, opposite = 0;
    for (const auto use : cylinderUses)
    {
        if (use->GetEdge() == seam)
        {
            forward += use->GetOrientation() == SM_OT_SAME ? 1U : 0U;
            opposite += use->GetOrientation() == SM_OT_OPPOSITE ? 1U : 0U;
        }
    }
    Require(forward == 1 && opposite == 1, "the chart cut contributes two opposite uses, not a new material boundary");
    if (segmented)
    {
        Require(
            (rings["130"].size() == 1 && rings["131"].size() == 2) || (rings["130"].size() == 2 && rings["131"].size() == 1),
            "one original full ring is represented by two target children"
        );
    }
    for (const auto& [id, ring] : rings)
    {
        (void)id;
        std::vector<Interval> intervals;
        for (const auto edge : ring)
        {
            SameCompleteCurve(*ring.front()->GetCurve(), *edge->GetCurve());
            intervals.push_back({ edge->GetInterval().GetMin(), edge->GetInterval().GetMax() });
        }
        Coverage(std::move(intervals), ring.front()->GetCurve()->GetNaturalInterval());
    }
    if (!mapping)
    {
        Require(!segmented, "segmented target requires owning source correspondence");
        return;
    }
    Require(
        mapping->completed && mapping->sourceVerticesPreserved && mapping->sourceCurveIntervalsCovered,
        "complete ring normalization preservation evidence"
    );
    Require(
        mapping->faces.size() == faces.GetSize() && mapping->edges.size() == edges.GetSize() && mapping->vertices.size() == vertices.GetSize(),
        "all target objects have origins"
    );
    std::map<std::string, std::string> sourceFaces{ { "180", "140" }, { "181", "141" }, { "182", "142" } };
    for (const auto& face : mapping->faces)
    {
        const auto found = sourceFaces.find(face.sourceFaceId);
        Require(found != sourceFaces.end() && found->second == face.sourceSurfaceId, "target face retains its original source support identity");
        sourceFaces.erase(found);
    }
    Require(sourceFaces.empty(), "no source face is dropped or invented during ring segmentation");
    std::map<std::string, std::vector<Interval>> declaredCoverage;
    std::set<std::size_t> targets;
    std::size_t derivedEdges = 0;
    for (const auto& origin : mapping->edges)
    {
        Require(origin.target < mapping->edges.size() && targets.insert(origin.target).second, "unique target edge index");
        if (origin.sourceEdgeId)
        {
            Require(rings.contains(*origin.sourceEdgeId) && !origin.derivedFromFaceId, "source child has one original edge identity");
            declaredCoverage[*origin.sourceEdgeId].push_back(origin.basisParameterInterval);
        }
        else
        {
            Require(origin.derivedFromFaceId == std::optional<std::string>("182"), "meridian has a face origin, never a fabricated source edge");
            ++derivedEdges;
        }
    }
    Require(derivedEdges == 1 && declaredCoverage.size() == 2, "origins distinguish original ring children and the chart cut");
    for (const auto& [id, ring] : rings)
    {
        Coverage(declaredCoverage.at(id), ring.front()->GetCurve()->GetNaturalInterval());
        std::vector<Interval> actual;
        for (const auto edge : ring)
        {
            actual.push_back({ edge->GetInterval().GetMin(), edge->GetInterval().GetMax() });
        }
        auto declared = declaredCoverage.at(id);
        std::sort(actual.begin(), actual.end());
        std::sort(declared.begin(), declared.end());
        Require(actual == declared, "reported source children are the actual native intervals");
    }
    std::set<std::string> originals;
    std::size_t derivedVertices = 0;
    for (const auto& origin : mapping->vertices)
    {
        if (origin.sourceVertexId)
        {
            Require(
                (*origin.sourceVertexId == "120" || *origin.sourceVertexId == "121") && originals.insert(*origin.sourceVertexId).second,
                "original vertex identities are not reassigned to derived vertices"
            );
        }
        else
        {
            Require(!origin.surfacePole && !origin.basisEdgeParameters.empty(), "ring split vertex has original circle parameter evidence");
            for (const auto& [id, parameter] : origin.basisEdgeParameters)
            {
                Require(rings.contains(id), "derived vertex uses an original circle basis");
                const auto& interval = rings.at(id).front()->GetCurve()->GetNaturalInterval();
                Require(
                    parameter > interval.GetMin() && parameter < interval.GetMax(),
                    "split vertex is interior to the original full circle interval"
                );
                SmPoint3d onBasis;
                Require(rings.at(id).front()->GetCurve()->EvaluatePoint(parameter, onBasis) == SM_SUCCESS, "derived vertex original basis evaluation");
                std::size_t matches = 0;
                for (const auto vertex : vertices)
                {
                    const auto p = vertex->GetPoint();
                    const bool original = std::any_of(
                        originalPoints.begin(),
                        originalPoints.end(),
                        [&](const auto& point)
                        {
                            return p.x == point[0] && p.y == point[1] && p.z == point[2];
                        }
                    );
                    matches += !original && p.DistanceBetween(onBasis) <= 1e-12 ? 1U : 0U;
                }
                Require(matches == 1, "derived parameter evidence identifies the actual new target vertex");
            }
            ++derivedVertices;
        }
    }
    Require(originals.size() == 2 && derivedVertices == (segmented ? 1U : 0U), "complete original and derived vertex inventory");
}

void CompareCompleteNative(SmBrep& a, SmBrep& b)
{
    step_import_test::CompareNativeDefinitions(a, b); // Includes all installed UV curve classes and coefficients.
    SmTArray<SmEdge*> ea, eb;
    a.GetEdges(ea);
    b.GetEdges(eb);
    for (ULONG i = 0; i < ea.GetSize(); ++i)
    {
        SameInterval(ea[i]->GetCurve()->GetNaturalInterval(), eb[i]->GetCurve()->GetNaturalInterval());
        SameInterval(ea[i]->GetCurve()->GetSTEPInterval(), eb[i]->GetCurve()->GetSTEPInterval());
        Require(SmTol::GetZoneTol3d(ea[i]) == SmTol::GetZoneTol3d(eb[i]), "copy/archive edge tolerance identity");
    }
    SmTArray<SmFace*> fa, fb;
    a.GetFaces(fa);
    b.GetFaces(fb);
    for (ULONG i = 0; i < fa.GetSize(); ++i)
    {
        SameDomain(fa[i]->GetUVDomain(), fb[i]->GetUVDomain());
        SameDomain(fa[i]->GetSurface()->GetNaturalUVDomain(), fb[i]->GetSurface()->GetNaturalUVDomain());
        SameDomain(fa[i]->GetSurface()->GetSTEPUVDomain(), fb[i]->GetSurface()->GetSTEPUVDomain());
    }
}

void CopyArchive(const ModelSnapshot& model, const Point& upper, bool segmented, const std::filesystem::path& file)
{
    model.WriteNative(file);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, file.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS && bodies.GetSize() == 1,
        "complete ring archive readback"
    );
    const auto mapping = model.TopologyNormalization();
    CheckNativeTarget(*bodies[0], upper, segmented, mapping);
    auto first = model.CopyBrepForModeling();
    auto second = model.CopyBrepForModeling();
    Require(
        first.status == Status::Accepted && first.model && second.status == Status::Accepted && second.model,
        "independent complete target copies"
    );
    first.model->Edit(
        [&](SmBrep& native)
        {
            CheckNativeTarget(native, upper, segmented, mapping);
            CompareCompleteNative(native, *bodies[0]);
            second.model->Edit(
                [&](SmBrep& other)
                {
                    CompareCompleteNative(native, other);
                }
            );
        }
    );
}

std::size_t AcceptedCase(const std::string& source, const Point& upper, bool repair, bool segmented, const std::filesystem::path& archive)
{
    const auto options = Config(source, repair);
    auto result = ImportBytes(source, options);
    Require(
        result.status == (repair ? Status::AcceptedWithRepairs : Status::Accepted) && result.model,
        "public ring import qualifies complete target"
    );
    const auto reportBytes = result.report.json.size();
    auto model = result.model;
    result = {};
    Require(model->Source()->Source().Bytes() == source, "owning snapshot retains the exact original source after report disposal");
    Require(
        model->Source()
                ->Field(static_cast<std::size_t>(model->Source()->Source().Find("182") - model->Source()->Source().Records().data()), "same_sense")
                .Value()
                .text == "T",
        "source face orientation remains original"
    );
    for (const auto id : { "130", "131" })
    {
        const auto* record = model->Source()->Source().Find(id);
        const auto index = static_cast<std::size_t>(record - model->Source()->Source().Records().data());
        Require(
            model->Source()->Field(index, "edge_start").ReferenceTarget() == model->Source()->Field(index, "edge_end").ReferenceTarget(),
            "source full circles remain single closed source edges"
        );
    }
    const auto history = model->CylindricalBandRepairs();
    Require(bool(history) == repair, "ring segmentation does not invent a source repair");
    if (repair)
    {
        Require(
            history->definitionsIdentical && history->workingTolerancesIdentical && history->originalSourceDiagnostics.size() == 1 &&
                history->originalSourceDiagnostics[0].source.key == "SourceRuleViolation:FACE.WR2",
            "source bound-role repair retains its original failed clause and unchanged-definition evidence"
        );
    }
    if (const auto origins = model->TopologyNormalization())
    {
        if (segmented)
        {
            Require(origins->sourceUseSegments.size() == 4, "owning one-to-many source-use correspondence survives report disposal");
            const auto& owningSource = *model->Source();
            for (const auto& use : origins->sourceUseSegments)
            {
                for (const auto& id : { use.sourceFaceId, use.sourceBoundId, use.sourceLoopId, use.sourceOrientedEdgeId, use.sourceEdgeId })
                {
                    Require(owningSource.Source().Find(id) != nullptr, "every incidence source identity refers to the original immutable source");
                }
                Require(
                    origins->edges.at(use.target.edge).sourceEdgeId == use.sourceEdgeId &&
                        origins->edges.at(use.target.edge).basisParameterInterval == use.target.basisParameterInterval,
                    "owning source-use and target edge origins share the original basis interval"
                );
                Require(
                    use.target.startVertex < origins->vertices.size() && use.target.endVertex < origins->vertices.size(),
                    "owning use endpoints name target vertices without inventing source vertices"
                );
            }
        }
        for (const auto& face : origins->faces)
        {
            Require(
                model->Face(face.target).SourceId() == face.sourceFaceId && model->Face(face.target).ParameterDomain() == face.nativeDomain,
                "owning face queries address the normalized target inventory"
            );
        }
        auto callerCopy = *origins;
        callerCopy.edges.clear();
        Require(model->TopologyNormalization()->edges.size() == origins->edges.size(), "caller cannot clear owning normalization evidence");
    }
    const auto info = model->Info();
    Require(info.faces == 3 && info.edges == (segmented ? 4U : 3U) && info.vertices == (segmented ? 3U : 2U), "owning target inventory");
    Require(model->MaterialVolumeMm3().has_value(), "public cylinder material volume is applicable");
    Near(*model->MaterialVolumeMm3(), 63 * std::numbers::pi, 63 * std::numbers::pi * 2e-4, "independent analytic cylinder volume");
    Near(info.areaMm2, 60 * std::numbers::pi, 60 * std::numbers::pi * 2e-4, "independent analytic cylinder area");
    Require(
        model->ClassifyMaterial({ 0., 0., 3. }) == MaterialPointClass::Inside &&
            model->ClassifyMaterial({ 5., 0., 3. }) == MaterialPointClass::Outside,
        "segmentation preserves material classification"
    );
    CopyArchive(*model, upper, segmented, archive);
    return reportBytes;
}
void AlignedConeOwnsCut(const std::string& original, const std::filesystem::path& file)
{
    auto text = original;
    Replace(text, "#173=FACE_OUTER_BOUND", "#173=FACE_BOUND");
    Replace(text, "#103=CARTESIAN_POINT('',(0.,3.,7.))", "#103=CARTESIAN_POINT('',(0.,10.,7.))");
    Replace(text, "#111=CIRCLE('',#107,3.)", "#111=CIRCLE('',#107,10.)");
    Replace(text, "#142=CYLINDRICAL_SURFACE('',#106,3.)", "#142=CONICAL_SURFACE('',#106,3.,0.7853981633974483)");
    auto result = ImportBytes(text, Config(text, false));
    Require(result.status == Status::Accepted && result.model, "legal aligned conical rings qualify without source repair");
    auto model = result.model;
    result = {};
    const auto origins = model->TopologyNormalization();
    Require(
        origins && origins->completed && origins->sourceCurveIntervalsCovered && origins->sourceVerticesPreserved,
        "conical cut correspondence is owned after report disposal even without ring subdivision"
    );
    Require(
        origins->faces.size() == 3 && origins->edges.size() == 3 && origins->vertices.size() == 2 && origins->sourceUseSegments.empty(),
        "aligned cone retains complete target origins without inventing a split source use"
    );
    Require(
        std::count_if(
            origins->edges.begin(),
            origins->edges.end(),
            [](const auto& edge)
            {
                return edge.derivedFromFaceId == "182" && !edge.sourceEdgeId;
            }
        ) == 1,
        "derived conical generator is attributed to its source face"
    );
    Require(
        model->Source()->Source().Bytes() == text && !model->CylindricalBandRepairs(),
        "aligned cone retains its exact legal source and None repair"
    );
    Near(*model->MaterialVolumeMm3(), 973 * std::numbers::pi / 3, 973 * std::numbers::pi / 3 * 2e-4, "independent conical frustum volume");
    Near(
        model->Info().areaMm2,
        (109 + 91 * std::sqrt(2.)) * std::numbers::pi,
        (109 + 91 * std::sqrt(2.)) * std::numbers::pi * 2e-4,
        "independent conical frustum area"
    );
    model->WriteNative(file);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, file.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS && bodies.GetSize() == 1,
        "complete conical native readback"
    );
    auto copy = model->CopyBrepForModeling();
    Require(copy.status == Status::Accepted && copy.model, "conical modeling copy is independently owned");
    copy.model->Edit(
        [&](SmBrep& native)
        {
            SmAssertArray pointers, deep;
            Require(
                native.ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && native.AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
                    deep.GetSize() == 0,
                "aligned cone copy native validity"
            );
            CompareCompleteNative(native, *bodies[0]);
        }
    );
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public cylindrical-band fixture and output directory required");
        std::ifstream input(argv[1], std::ios::binary);
        Require(bool(input), "public cylindrical-band fixture opens");
        const std::string original{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        const std::filesystem::path output(argv[2]);
        std::filesystem::create_directories(output);
        AlignedConeOwnsCut(original, output / "aligned-cone.smb");
        (void)AcceptedCase(original, { 0., 3., 7. }, true, false, output / "aligned.smb");
        auto phased = original;
        Replace(phased, "#103=CARTESIAN_POINT('',(0.,3.,7.))", "#103=CARTESIAN_POINT('',(3.,0.,7.))");
        const auto reportBytes = AcceptedCase(phased, { 3., 0., 7. }, true, true, output / "phase.smb");
        auto legal = phased;
        Replace(legal, "#173=FACE_OUTER_BOUND", "#173=FACE_BOUND");
        (void)AcceptedCase(legal, { 3., 0., 7. }, false, true, output / "legal.smb");
        auto nonKnot = original;
        Replace(nonKnot, "#103=CARTESIAN_POINT('',(0.,3.,7.))", "#103=CARTESIAN_POINT('',(1.8,2.4,7.))");
        (void)AcceptedCase(nonKnot, { 1.8, 2.4, 7. }, true, true, output / "non-knot.smb");
        auto reversed = phased;
        Replace(reversed, "ADVANCED_FACE('',(#172,#173),#142", "ADVANCED_FACE('',(#173,#172),#142");
        (void)AcceptedCase(reversed, { 3., 0., 7. }, true, true, output / "reversed-bounds.smb");
        auto wrongRadius = phased;
        Replace(wrongRadius, "CYLINDRICAL_SURFACE('',#106,3.)", "CYLINDRICAL_SURFACE('',#106,3.001)");
        const auto rejected = ImportBytes(wrongRadius, Config(wrongRadius, true));
        Require(!rejected.model && rejected.report.nativeCalls == 0, "segmentation cannot bypass the exact source-region proof");
        auto nativeBudget = Config(phased, true);
        nativeBudget.nativeLimits.nativeCalls = 1;
        const auto nativeLimited = ImportBytes(phased, nativeBudget);
        Require(nativeLimited.status == Status::ResourceLimit && !nativeLimited.model, "native budget cannot publish an incomplete ring target");
        auto reportBudget = Config(phased, true);
        reportBudget.nativeLimits.reportBytes = reportBytes - 1;
        const auto reportLimited = ImportBytes(phased, reportBudget);
        Require(
            reportLimited.status == Status::ResourceLimit && !reportLimited.model && reportLimited.report.candidateDiscarded,
            "complete ring provenance must fit before publication"
        );
        for (const auto phase : { Phase::BeforeRepairProof, Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
        {
            auto cancelled = Config(phased, true);
            cancelled.execution.control = std::make_shared<ImportControl>();
            cancelled.execution.progress = [&](Progress progress)
            {
                if (progress.phase == phase)
                {
                    cancelled.execution.control->RequestCancel();
                }
            };
            const auto stopped = ImportBytes(phased, cancelled);
            Require(stopped.status == Status::Cancelled && !stopped.model, "ring construction cancellation leaves no published candidate");
        }
        std::cout << "ring phase segmentation contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
