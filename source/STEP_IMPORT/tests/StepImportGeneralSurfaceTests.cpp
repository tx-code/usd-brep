// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmApiGeneral.h>
#include <SmApiQueries.h>
#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmFaceuse.h>
#include <SmNativeArchive.h>
#include <SmRegion.h>
#include <SmShell.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>

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
void Near(double actual, double expected, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= 1e-6, message);
}
void Within(double actual, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
struct Truth
{
    const char* role;
    std::size_t faces, edges, vertices, freeEdges;
    double area, uniqueLength;
    Point centroid;
    Bounds bounds;
    bool closed, material;
    std::vector<double> faceAreas;
    double areaTolerance = 1e-6, centroidTolerance = 1e-6;
};
Truth Expected(std::string_view mode)
{
    // Independent analytic oracles for the committed public fixtures. The
    // rotated P01 rectangle is 12 by 8; P04/P05 share a 10 by 20 by 30 box.
    if (mode == "plane")
    {
        return {
            "OpenSurfaceModel", 1, 4, 4, 4, 96., 40., { 3.4, 5.2, 5. }, { Point{ -3.4, -2., 5. }, Point{ 10.2, 12.4, 5. } }, false, false, { 96. }
        };
    }
    if (mode == "closed" || mode == "material")
    {
        return { mode == "closed" ? "ClosedSurfaceModel" : "MaterialSolid",
                 6,
                 12,
                 8,
                 0,
                 2200.,
                 240.,
                 { 7., 7., 20. },
                 { Point{ 2., -3., 5. }, Point{ 12., 17., 35. } },
                 true,
                 mode == "material",
                 { 200., 200., 300., 300., 600., 600. } };
    }
    if (mode == "folded")
    {
        return { "OpenSurfaceModel", 2, 7, 6, 6, 18., 21., { 8. / 3., 1.5, 1. / 3. }, { Point{ 0., 0., 0. }, Point{ 4., 3., 2. } }, false, false,
                 { 6., 12. } };
    }
    if (mode == "coplanar")
    {
        return { "OpenSurfaceModel", 2, 7, 6, 6, 18., 21., { 3., 1.5, 0. }, { Point{ 0., 0., 0. }, Point{ 6., 3., 0. } }, false, false, { 6., 12. } };
    }
    if (mode == "cylinder_band")
    {
        const double area = 42. * std::numbers::pi;
        return { "OpenSurfaceModel",
                 1,
                 3,
                 2,
                 2,
                 area,
                 12. * std::numbers::pi + 7.,
                 { 0., 0., 3.5 },
                 { Point{ -3., -3., 0. }, Point{ 3., 3., 7. } },
                 false,
                 false,
                 { area },
                 area * 1e-7,
                 1e-5 };
    }
    throw std::invalid_argument("Unknown general surface mode");
}
Options General()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
void CheckStatistics(const Statistics& info, const Truth& truth)
{
    Require(
        info.shapeRole == truth.role && info.faces == truth.faces && info.edges == truth.edges && info.vertices == truth.vertices &&
            info.freeBoundaryEdges == truth.freeEdges,
        "Independent role and topology census"
    );
    Within(info.areaMm2, truth.area, truth.areaTolerance, "Independent boundary area, each geometric face once");
    Near(info.boundaryMm, truth.uniqueLength, "Unique native edge length includes a shared edge once");
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Within(info.centroidMm[axis], truth.centroid[axis], truth.centroidTolerance, "Independent area centroid");
        Near(info.boundsMm[0][axis], truth.bounds[0][axis], "Independent tight lower bound");
        Near(info.boundsMm[1][axis], truth.bounds[1][axis], "Independent tight upper bound, including zero thickness");
    }
    if (!truth.material)
    {
        Require(!info.materialVolumeMm3, "Surface volume is absent, not a fabricated zero");
    }
}
void CheckNative(SmBrep& brep, const Truth& truth)
{
    step_import_test::CheckNativeWorkBudget(brep, 1e-3);
    SmAssertArray pointers;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize(), "Native graph is traversable before incidence checks");
    SmTArray<SmRegion*> regions;
    SmTArray<SmShell*> shells;
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    brep.GetRegions(regions);
    brep.GetShells(shells);
    brep.GetFaces(faces);
    brep.GetEdges(edges);
    Require(
        regions.GetSize() == (truth.closed ? 2u : 1u) && shells.GetSize() == (truth.closed ? 2u : 1u) && faces.GetSize() == truth.faces &&
            edges.GetSize() == truth.edges,
        "Actual region, shell, face and edge inventory"
    );
    Require(brep.GetInfiniteRegion() && brep.GetInfiniteRegion()->IsVoid() == TRUE, "Infinite region is void for every role");
    std::size_t materials = 0, boundedVoids = 0;
    for (ULONG i = 0; i < regions.GetSize(); ++i)
    {
        if (regions[i] != brep.GetInfiniteRegion())
        {
            if (regions[i]->IsVoid() == TRUE)
            {
                ++boundedVoids;
            }
            else
            {
                ++materials;
            }
        }
    }
    Require(
        materials == (truth.material ? 1u : 0u) && boundedVoids == (truth.closed && !truth.material ? 1u : 0u),
        "Actual IsVoid flags distinguish closed surface from material"
    );
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        SmFaceuse *a = nullptr, *b = nullptr;
        faces[i]->GetFaceuses(a, b);
        Require(a && b && a->GetMate() == b && b->GetMate() == a && a->GetOrientation() != b->GetOrientation(), "Each face keeps both opposite uses");
        Require((a->GetShell() != b->GetShell()) == truth.closed, "Open face sides share the infinite-region shell; closed sides do not");
    }
    std::size_t freeEdges = 0;
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        SmTArray<SmEdgeuse*> uses;
        Require(
            edges[i]->GetEdgeuses(uses) == SM_SUCCESS && (uses.GetSize() == 2 || uses.GetSize() == 4),
            "Actual edge incidence is one or two logical uses"
        );
        if (uses.GetSize() == 2)
        {
            ++freeEdges;
        }
        for (ULONG j = 0; j < uses.GetSize(); ++j)
        {
            Require(
                uses[j]->GetMate() && uses[j]->GetMate()->GetMate() == uses[j] && uses[j]->GetShell(),
                "Every edgeuse has reciprocal mate and real shell"
            );
        }
    }
    Require(freeEdges == truth.freeEdges, "Actual free edges, independently of the report");
}
std::string_view JsonMember(std::string_view json, std::string_view key)
{
    const auto needle = "\"" + std::string(key) + "\"";
    std::size_t at = std::string_view::npos, level = 0;
    bool inString = false, escape = false;
    // Search the entire direct object; nested reports reuse field names. The
    // writer's member names are fixed unescaped ASCII. Refuse escaped direct
    // keys too, so an escape-spelled duplicate cannot evade this test guard.
    for (std::size_t i = 0; i < json.size(); ++i)
    {
        const char c = json[i];
        if (inString)
        {
            if (escape)
            {
                escape = false;
            }
            else if (c == '\\')
            {
                escape = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
        }
        else if (c == '"')
        {
            if (level == 1)
            {
                bool escapedKey = false;
                auto end = i + 1;
                for (; end < json.size(); ++end)
                {
                    if (json[end] == '\\')
                    {
                        escapedKey = true;
                        ++end;
                    }
                    else if (json[end] == '"')
                    {
                        break;
                    }
                }
                Require(end < json.size(), "Direct JSON string is complete");
                const auto after = json.find_first_not_of(" \t\r\n", end + 1);
                if (after != std::string_view::npos && json[after] == ':')
                {
                    Require(!escapedKey, "Checked writer objects use unescaped ASCII member names");
                    if (json.substr(i, end - i + 1) == needle)
                    {
                        Require(at == std::string_view::npos, "Checked direct JSON member is not duplicated");
                        at = i;
                    }
                }
            }
            inString = true;
        }
        else if (c == '{' || c == '[')
        {
            ++level;
        }
        else if (c == '}' || c == ']')
        {
            --level;
        }
    }
    Require(at != std::string_view::npos, "Required JSON member exists");
    auto start = json.find(':', at);
    Require(start != std::string_view::npos, "JSON member has a value");
    start = json.find_first_not_of(" \t\r\n", start + 1);
    Require(start != std::string_view::npos, "JSON member value is not empty");
    bool quoted = false, escaped = false;
    std::size_t depth = 0;
    for (std::size_t i = start; i < json.size(); ++i)
    {
        const char c = json[i];
        if (quoted)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (c == '\\')
            {
                escaped = true;
            }
            else if (c == '"')
            {
                quoted = false;
                if (depth == 0)
                {
                    return json.substr(start, i - start + 1);
                }
            }
        }
        else if (c == '"')
        {
            quoted = true;
        }
        else if (c == '{' || c == '[')
        {
            ++depth;
        }
        else if (c == '}' || c == ']')
        {
            if (depth == 0)
            {
                return json.substr(start, i - start);
            }
            if (--depth == 0)
            {
                return json.substr(start, i - start + 1);
            }
        }
        else if (c == ',' && depth == 0)
        {
            return json.substr(start, i - start);
        }
    }
    return json.substr(start);
}
std::size_t Occurrences(std::string_view text, std::string_view token)
{
    std::size_t count = 0, at = 0;
    while ((at = text.find(token, at)) != std::string_view::npos)
    {
        ++count;
        at += token.size();
    }
    return count;
}
void CheckSurfaceReport(const Outcome& imported, const Truth& truth)
{
    const auto& report = imported.report;
    Require(
        report.generalBrep && report.sourceShape && report.sourceShape->role == truth.role &&
            report.sourceShape->shapeType == "SHELL_BASED_SURFACE_MODEL" &&
            report.sourceShape->shellType == (truth.closed ? "CLOSED_SHELL" : "OPEN_SHELL"),
        "General report retains the true source shape branch"
    );
    Require(
        report.nativeRole && report.nativeRole->regions == (truth.closed ? 2u : 1u) && report.nativeRole->shells == (truth.closed ? 2u : 1u) &&
            report.nativeRole->materialRegions == 0 && report.nativeRole->boundedVoidRegions == (truth.closed ? 1u : 0u),
        "General report records the actual nonmaterial native role"
    );
    Require(
        !report.solidRegions && !report.voidCavities && !report.volumeBoundMm3 && !report.volumeJunctionBoundMm && !report.volumeGapFluxBoundMm3 &&
            !report.volumeSkippedFluxBoundMm3,
        "Material census and volume evidence remain inapplicable"
    );
    Require(report.adjacency.size() == truth.edges && report.faces.size() == truth.faces, "Complete general edge adjacency and face observations");
    std::size_t freeEdges = 0;
    for (const auto& edge : report.adjacency)
    {
        Require(edge.nativeUses == 2 || edge.nativeUses == 4, "Reported edgeuse incidence comes from native topology");
        if (edge.nativeUses == 2)
        {
            ++freeEdges;
        }
    }
    Require(freeEdges == truth.freeEdges, "Report free count agrees with independent oracle");
    std::vector<double> areas;
    std::size_t faceFreeEdges = 0;
    for (const auto& face : report.faces)
    {
        Require(face.native.shapeRole == "FaceSurface" && !face.native.materialVolumeMm3, "Surface face observations have no material role");
        areas.push_back(face.native.areaMm2);
        faceFreeEdges += face.native.freeBoundaryEdges;
        const auto expectedFree = truth.closed ? 0u : truth.faces == 1 ? truth.freeEdges : 3u;
        Require(face.native.freeBoundaryEdges == expectedFree, "Per-face free edges come from actual incidence");
        const double expectedBoundary = truth.faces == 1 ? truth.uniqueLength :
                                        truth.closed     ? (face.native.areaMm2 < 250. ? 60. :
                                                            face.native.areaMm2 < 450. ? 80. :
                                                                                         100.) :
                                                           (face.native.areaMm2 < 9. ? 10. : 14.);
        Near(face.native.boundaryMm, expectedBoundary, "Independent per-face unique edge length");
    }
    std::sort(areas.begin(), areas.end());
    for (std::size_t i = 0; i < areas.size(); ++i)
    {
        Within(areas[i], truth.faceAreas[i], truth.areaTolerance, "Independent per-face area");
    }
    Require(faceFreeEdges == truth.freeEdges, "Every free edge belongs to exactly one face observation");
    const auto shape = JsonMember(report.json, "source_shape");
    Require(
        JsonMember(shape, "role") == "\"" + std::string(truth.role) + "\"" && shape.find("SHELL_BASED_SURFACE_MODEL") != std::string_view::npos,
        "Serialized source role and shape type"
    );
    const auto role = JsonMember(report.json, "native_role");
    Require(
        JsonMember(role, "regions") == (truth.closed ? "2" : "1") && JsonMember(role, "shells") == (truth.closed ? "2" : "1") &&
            JsonMember(role, "material_regions") == "0" && JsonMember(role, "bounded_void_regions") == (truth.closed ? "1" : "0"),
        "Serialized native role numbers, not just presence of a key"
    );
    const auto adjacency = JsonMember(report.json, "edge_adjacency");
    Require(
        Occurrences(adjacency, "\"native_edgeuses\":2") == truth.freeEdges &&
            Occurrences(adjacency, "\"native_edgeuses\":4") == truth.edges - truth.freeEdges,
        "Serialized 2/4 edgeuse inventory"
    );
    const auto faceRows = JsonMember(report.json, "face_mappings");
    Require(
        Occurrences(faceRows, "\"FaceSurface\"") == truth.faces && Occurrences(faceRows, "\"source_surface\":") == truth.faces,
        "Every surface face has serialized source identity and native statistics"
    );
    const auto census = JsonMember(report.json, "material_census");
    Require(
        JsonMember(census, "solid_regions") == "null" && JsonMember(census, "void_cavities") == "null",
        "Serialized material census is inapplicable"
    );
}
std::string ReadPublic(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    Require(bool(input), "Public fixture exists");
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}
void Replace(std::string& bytes, const std::string& from, const std::string& to)
{
    const auto at = bytes.find(from);
    Require(at != std::string::npos, "Known public fixture role token");
    bytes.replace(at, from.size(), to);
}
std::string BandBytes(const std::filesystem::path& fixture)
{
    auto bytes = ReadPublic(fixture);
    // Select the existing complete cylindrical side as a surface model. The
    // circles, meridian, senses, frames, coordinates and parameters stay intact;
    // the meridian has two logical uses on the same face and is not free.
    Replace(bytes, "#20=ADVANCED_BREP_SHAPE_REPRESENTATION", "#20=MANIFOLD_SURFACE_SHAPE_REPRESENTATION");
    Replace(bytes, "#21=MANIFOLD_SOLID_BREP('',#22);", "#21=SHELL_BASED_SURFACE_MODEL('',(#22));");
    Replace(bytes, "#22=CLOSED_SHELL('',(#180,#181,#182));", "#22=OPEN_SHELL('',(#182));");
    // Author a complete public surface variant: the unselected caps and their
    // private profile nodes must not remain as orphan source content. Shared
    // circle/placement/vertex and meridian definitions retain their exact bytes.
    for (unsigned id : { 140u, 141u, 150u, 151u, 160u, 161u, 170u, 171u, 180u, 181u })
    {
        const auto at = bytes.find("#" + std::to_string(id) + "=");
        Require(at != std::string::npos, "Known public cap-only entity exists");
        const auto end = bytes.find(';', at);
        Require(end != std::string::npos, "Public cap-only entity has a complete record");
        bytes.erase(at, end - at + 1);
    }
    return bytes;
}
Options BandOptions()
{
    auto options = General();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-7 };
    return options;
}
Outcome Accepted(std::string bytes, const Options& options)
{
    auto imported = ImportBytes(std::move(bytes), options);
    if (imported.status != Status::Accepted || !imported.model)
    {
        for (const auto& diagnostic : imported.report.diagnostics)
        {
            std::cerr << diagnostic.source.key << '\n';
        }
        throw std::runtime_error("Public general surface fixture is accepted");
    }
    return imported;
}
void CheckIntegration(const ModelSnapshot& model, double accuracy)
{
    const auto observation = model.NumericalIntegration();
    Require(
        observation && observation->completed && observation->requestedRelativeAccuracy == accuracy &&
            observation->appliedRelativeAccuracy == accuracy,
        "Area-only integral completion preserves the selected numerical control"
    );
}
void Band(const std::filesystem::path& fixture)
{
    const auto truth = Expected("cylinder_band");
    const auto bytes = BandBytes(fixture);
    const auto imported = Accepted(bytes, BandOptions());
    CheckStatistics(imported.model->Info(), truth);
    CheckSurfaceReport(imported, truth);
    CheckIntegration(*imported.model, 1e-7);
    Require(
        imported.report.source && imported.report.source->Bytes() == bytes && !imported.model->MaterialVolumeMm3() &&
            !imported.model->ClassifyMaterial({ 0., 0., 3.5 }),
        "Band keeps immutable surface-role source and no material APIs"
    );
    auto copy = imported.model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "Nonplanar surface has a normal modeling copy");
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            CheckNative(brep, truth);
        }
    );
    const auto shared = std::find_if(
        imported.report.adjacency.begin(),
        imported.report.adjacency.end(),
        [](const auto& edge)
        {
            return edge.nativeUses == 4;
        }
    );
    Require(
        shared != imported.report.adjacency.end() && shared->faceIds.size() == 2 &&
            std::set<std::string>(shared->faceIds.begin(), shared->faceIds.end()) == std::set<std::string>{ "182" },
        "Same-face meridian use is shared incidence, not two fabricated faces"
    );
}
struct NativePart
{
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> cleanCurves{ &curves };
    SmObjsDelete<SmSurface*> cleanSurfaces{ &surfaces };
    SmObjsDelete<SmBrep*> cleanBreps{ &breps };
};
void CheckParameterIdentity(SmBrep& a, SmBrep& b)
{
    (void)step_import_test::CompareNativeDefinitions(a, b);
    SmTArray<SmFace*> fa, fb;
    SmTArray<SmEdge*> ea, eb;
    a.GetFaces(fa);
    b.GetFaces(fb);
    a.GetEdges(ea);
    b.GetEdges(eb);
    const auto sameInterval = [](const SmExtent1d& x, const SmExtent1d& y)
    {
        return x.GetMin() == y.GetMin() && x.GetMax() == y.GetMax();
    };
    for (ULONG i = 0; i < fa.GetSize(); ++i)
    {
        Require(
            fa[i]->GetUVDomain() == fb[i]->GetUVDomain() && fa[i]->GetSurface()->GetNaturalUVDomain() == fb[i]->GetSurface()->GetNaturalUVDomain() &&
                fa[i]->GetSurface()->GetSTEPUVDomain() == fb[i]->GetSurface()->GetSTEPUVDomain(),
            "Native/STEP face parameter domains survive archive"
        );
    }
    for (ULONG i = 0; i < ea.GetSize(); ++i)
    {
        Require(
            sameInterval(ea[i]->GetCurve()->GetNaturalInterval(), eb[i]->GetCurve()->GetNaturalInterval()) &&
                sameInterval(ea[i]->GetCurve()->GetSTEPInterval(), eb[i]->GetCurve()->GetSTEPInterval()),
            "Native/STEP curve intervals survive archive"
        );
    }
}
void Archive(const std::filesystem::path& fixture, const std::filesystem::path& directory, const std::string& mode)
{
    const bool band = mode == "archive_band";
    const auto truth = Expected(band ? "cylinder_band" : mode == "archive_closed" ? "closed" : "plane");
    auto imported = Accepted(band ? BandBytes(fixture) : ReadPublic(fixture), band ? BandOptions() : General());
    const auto original = imported.model->Info();
    const auto report = imported.report;
    auto views = imported.model->Faces();
    const auto domain = views[0].ParameterDomain();
    const double u = (domain[0] + domain[2]) / 2., v = (domain[1] + domain[3]) / 2.;
    const auto evaluated = views[0].EvaluateSurface(u, v);
    auto copy = imported.model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "Normal surface copy is published");
    std::filesystem::create_directories(directory);
    const auto path = directory / (mode + ".smb");
    imported.model->WriteNative(path);
    Require(
        imported.model->Info().boundsMm == original.boundsMm && imported.report.json == report.json,
        "Archive never mutates accepted snapshot/report"
    );
    NativePart loaded;
    Require(
        SmNativeArchive::ReadPartFromFile(loaded.context, path.c_str(), loaded.curves, loaded.surfaces, loaded.trees, loaded.breps, SM_BINARY) ==
                SM_SUCCESS &&
            loaded.breps.GetSize() == 1 && !loaded.curves.GetSize() && !loaded.surfaces.GetSize() && !loaded.trees.GetSize(),
        "Complete native surface archive without spline fitting"
    );
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            CheckNative(brep, truth);
            CheckNative(*loaded.breps[0], truth);
            CheckParameterIdentity(brep, *loaded.breps[0]);
            Require(SmApiTranslate(&brep, { 100., -20., 3. }) == SM_SUCCESS, "Only the modeling copy is translated");
        }
    );
    const std::weak_ptr<const ModelSnapshot> wrapper = imported.model;
    imported = {};
    Require(wrapper.expired() && views[0].ParameterDomain() == domain, "Face views retain native parameter identity after root release");
    const auto after = views[0].EvaluateSurface(u, v);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Near(after[axis], evaluated[axis], "Owning face evaluation survives copy edit/root disposal");
    }
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            SmPoint3d minimum, maximum;
            Require(SmApiBrepBoundingBox(&brep, TRUE, minimum, maximum) == SM_SUCCESS, "Independent copy remains usable after root release");
            Near(minimum.x, truth.bounds[0][0] + 100., "Translated modeling-copy bounds");
            Near(maximum.z, truth.bounds[1][2] + 3., "Translated modeling-copy bounds");
            CheckNative(brep, truth);
        }
    );
    views.clear();
    // The result and every source native face owner are now gone. Report and
    // semantic-source values retain no native graph, so this query exercises
    // the modeling copy after the source BRep/context have been released.
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            SmPoint3d minimum, maximum;
            Require(
                SmApiBrepBoundingBox(&brep, TRUE, minimum, maximum) == SM_SUCCESS,
                "Modeling copy remains usable after all source native owners are released"
            );
            Near(minimum.x, truth.bounds[0][0] + 100., "Independent modeling-copy minimum after source destruction");
            Near(maximum.z, truth.bounds[1][2] + 3., "Independent modeling-copy maximum after source destruction");
            CheckNative(brep, truth);
        }
    );
    copy.model.reset();
    CheckNative(*loaded.breps[0], truth);
    Require(
        report.source && !report.json.empty() && report.native && report.native->shapeRole == truth.role,
        "Report/source survive all snapshot and copy owners"
    );
    // Positive copy/archive cases establish these complete fixtures only; they
    // do not inject or certify whole-BRep constructor/merge failure cleanup.
}
void Lifecycle(const std::filesystem::path& fixture)
{
    const auto bytes = ReadPublic(fixture);
    auto imported = Accepted(bytes, General());
    const auto truth = Expected("plane");
    CheckStatistics(imported.model->Info(), truth);
    auto face = imported.model->Face();
    const auto domain = face.ParameterDomain();
    const double u = (domain[0] + domain[2]) / 2., v = (domain[1] + domain[3]) / 2.;
    const auto point = face.EvaluateSurface(u, v);
    const auto classification = face.ClassifyUV(u, v);
    const auto observation = imported.model->NumericalIntegration();
    Require(observation && observation->completed, "Default surface area integration actually completed");
    CheckIntegration(*imported.model, 1e-4);
    const auto report = imported.report;
    const auto source = imported.model->Source();
    const auto units = imported.model->LengthUnits();
    const std::weak_ptr<const ModelSnapshot> wrapper = imported.model;
    imported = {};
    Require(
        wrapper.expired() && source->Source().Bytes() == bytes && report.source->Bytes() == bytes && units.sourceLengthUnitId == "10",
        "Face, source, units and report have independent owning lifetimes"
    );
    Require(face.ParameterDomain() == domain && face.ClassifyUV(u, v) == classification, "Native chart parameters survive root disposal");
    const auto after = face.EvaluateSurface(u, v);
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Near(after[axis], point[axis], "Native same-parameter evaluation survives root disposal");
    }
    const auto closest = face.Closest({ truth.centroid[0], truth.centroid[1], truth.centroid[2] + 3. });
    Near(closest.distanceMm, 3., "Independent closest distance after root disposal");
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Near(closest.pointMm[axis], truth.centroid[axis], "Independent closest point after root disposal");
    }
    bool rejected = false;
    try
    {
        (void)face.EvaluateSurface(std::numeric_limits<double>::infinity(), v);
    }
    catch (const std::out_of_range&)
    {
        rejected = true;
    }
    Require(rejected, "Unusable native parameter rejected before kernel clamping");
    // A general chart is not promised to be source UV or millimetres. This
    // checks actual retained native parameters, not an invented source mapping.
}
void Integration(const std::filesystem::path& fixture)
{
    const auto bytes = ReadPublic(fixture);
    for (const auto requested : std::array<std::optional<double>, 4>{ std::nullopt, 1e-4, 1e-7, 1e-8 })
    {
        auto options = General();
        if (requested)
        {
            options.numericalIntegration = NumericalIntegrationSettings{ *requested };
        }
        auto imported = Accepted(bytes, options);
        const double applied = requested.value_or(1e-4);
        CheckIntegration(*imported.model, applied);
        CheckStatistics(imported.model->Info(), Expected("plane"));
        if (requested)
        {
            Require(
                imported.report.numericalIntegration && imported.report.numericalIntegration->completed &&
                    imported.report.numericalIntegration->requestedRelativeAccuracy == applied &&
                    imported.report.numericalIntegration->appliedRelativeAccuracy == applied,
                "Explicit surface integration is reported without clamping"
            );
        }
        auto snapshot = imported.model;
        auto observation = snapshot->NumericalIntegration();
        observation->requestedRelativeAccuracy = 1.;
        imported = {};
        CheckIntegration(*snapshot, applied);
        Require(!snapshot->MaterialVolumeMm3(), "Completed area observation does not fabricate volume");
    }
}
// Test-only complete JSON grammar/UTF-8 check, RFC 8259 sections 2-7:
// https://www.rfc-editor.org/rfc/rfc8259 . Consume the complete report, rather
// than treating a closing brace as proof that no nested value was truncated.
class JsonSyntax
{
    std::string_view m_text;
    std::size_t m_at = 0;
    void Space()
    {
        while (m_at < m_text.size() && std::string_view(" \t\r\n").find(m_text[m_at]) != std::string_view::npos)
        {
            ++m_at;
        }
    }
    bool Take(char expected)
    {
        if (m_at == m_text.size() || m_text[m_at] != expected)
        {
            return false;
        }
        ++m_at;
        return true;
    }
    bool String()
    {
        if (!Take('"'))
        {
            return false;
        }
        while (m_at < m_text.size())
        {
            const auto c = static_cast<unsigned char>(m_text[m_at++]);
            if (c == '"')
            {
                return true;
            }
            if (c < 0x20)
            {
                return false;
            }
            if (c == '\\')
            {
                if (m_at == m_text.size())
                {
                    return false;
                }
                const char escape = m_text[m_at++];
                if (escape == 'u')
                {
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        if (m_at == m_text.size())
                        {
                            return false;
                        }
                        const char digit = m_text[m_at++];
                        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F')))
                        {
                            return false;
                        }
                    }
                }
                else if (
                    escape != '"' && escape != '\\' && escape != '/' && escape != 'b' && escape != 'f' && escape != 'n' && escape != 'r' &&
                    escape != 't'
                )
                {
                    return false;
                }
            }
            else if (c >= 0x80)
            {
                unsigned tails = 0, point = 0, minimum = 0;
                if (c >= 0xc2 && c <= 0xdf)
                {
                    tails = 1;
                    point = c & 0x1f;
                    minimum = 0x80;
                }
                else if (c >= 0xe0 && c <= 0xef)
                {
                    tails = 2;
                    point = c & 0x0f;
                    minimum = 0x800;
                }
                else if (c >= 0xf0 && c <= 0xf4)
                {
                    tails = 3;
                    point = c & 0x07;
                    minimum = 0x10000;
                }
                else
                {
                    return false;
                }
                for (unsigned i = 0; i < tails; ++i)
                {
                    if (m_at == m_text.size())
                    {
                        return false;
                    }
                    const auto tail = static_cast<unsigned char>(m_text[m_at++]);
                    if ((tail & 0xc0) != 0x80)
                    {
                        return false;
                    }
                    point = (point << 6) | (tail & 0x3f);
                }
                if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff))
                {
                    return false;
                }
            }
        }
        return false;
    }
    bool Digits()
    {
        const auto start = m_at;
        while (m_at < m_text.size() && m_text[m_at] >= '0' && m_text[m_at] <= '9')
        {
            ++m_at;
        }
        return m_at != start;
    }
    bool Number()
    {
        (void)Take('-');
        if (!Take('0') && !Digits())
        {
            return false;
        }
        if (Take('.') && !Digits())
        {
            return false;
        }
        if (Take('e') || Take('E'))
        {
            if (!Take('+'))
            {
                (void)Take('-');
            }
            if (!Digits())
            {
                return false;
            }
        }
        return true;
    }
    bool Word(std::string_view word)
    {
        if (m_text.substr(m_at, word.size()) != word)
        {
            return false;
        }
        m_at += word.size();
        return true;
    }
    bool Compound(char close, bool object, unsigned depth)
    {
        ++m_at;
        Space();
        if (Take(close))
        {
            return true;
        }
        for (;;)
        {
            if (object)
            {
                if (!String())
                {
                    return false;
                }
                Space();
                if (!Take(':'))
                {
                    return false;
                }
            }
            if (!Value(depth))
            {
                return false;
            }
            Space();
            if (Take(close))
            {
                return true;
            }
            if (!Take(','))
            {
                return false;
            }
            Space();
        }
    }
    bool Value(unsigned depth)
    {
        if (depth > 512)
        {
            return false;
        }
        Space();
        if (m_at == m_text.size())
        {
            return false;
        }
        switch (m_text[m_at])
        {
            case '{':
                return Compound('}', true, depth + 1);
            case '[':
                return Compound(']', false, depth + 1);
            case '"':
                return String();
            case 't':
                return Word("true");
            case 'f':
                return Word("false");
            case 'n':
                return Word("null");
            default:
                return Number();
        }
    }

public:

    explicit JsonSyntax(std::string_view text) : m_text(text)
    {
    }
    bool CompleteObject()
    {
        Space();
        if (m_at == m_text.size() || m_text[m_at] != '{' || !Value(0))
        {
            return false;
        }
        Space();
        return m_at == m_text.size();
    }
    std::optional<std::size_t> PrefixLength()
    {
        if (!Value(0))
        {
            return {};
        }
        return m_at;
    }
};
void CheckJsonTestGuards()
{
    constexpr std::string_view valid = R"({"status":"ResourceLimit","model":null})";
    Require(
        JsonSyntax(valid).CompleteObject() && JsonMember(valid, "status") == "\"ResourceLimit\"" && JsonMember(valid, "model") == "null",
        "Budget JSON guard accepts its fixed-key control"
    );
    Require(!JsonSyntax(valid.substr(0, valid.size() - 1)).CompleteObject(), "Budget JSON guard rejects a truncated object");
    Require(!JsonSyntax(std::string(valid) + " tail").CompleteObject(), "Budget JSON guard rejects trailing garbage");
    const auto rejects = [](std::string_view text, std::string_view key)
    {
        try
        {
            (void)JsonMember(text, key);
        }
        catch (const std::runtime_error&)
        {
            return true;
        }
        return false;
    };
    Require(
        rejects(R"({"status":"ResourceLimit","status":"Accepted","model":null})", "status"),
        "Budget JSON guard rejects a duplicate checked status"
    );
    Require(rejects(R"({"status":"ResourceLimit","model":null,"model":{}})", "model"), "Budget JSON guard rejects a duplicate checked model");
    Require(
        rejects(R"({"status":"ResourceLimit","sta\u0074us":"Accepted","model":null})", "status"),
        "Fixed-ASCII writer guard rejects escape-spelled direct keys"
    );
}
std::string_view FirstJsonArrayValue(std::string_view array)
{
    const auto open = array.find_first_not_of(" \t\r\n");
    Require(open != std::string_view::npos && array[open] == '[', "Expected a JSON array");
    const auto start = array.find_first_not_of(" \t\r\n", open + 1);
    Require(start != std::string_view::npos, "Nested definition evidence is present");
    const auto length = JsonSyntax(array.substr(start)).PrefixLength();
    Require(length.has_value(), "Nested definition is a complete JSON value");
    return array.substr(start, *length);
}
void ReportBudgetDiagnostic(const char* label, Status status, bool published, const Report& report, std::size_t budget, const char* expectedRole)
{
    const bool byteLimit = std::any_of(
        report.diagnostics.begin(),
        report.diagnostics.end(),
        [](const auto& diagnostic)
        {
            return diagnostic.source.key == "ImportReportByteLimit";
        }
    );
    const auto matchesRole = [expectedRole](const Report& evidence)
    {
        return evidence.sourceShape && evidence.sourceShape->role == expectedRole && evidence.nativeRole && evidence.nativeRole->materialRegions == 0;
    };
    bool roleEvidence = matchesRole(report);
    for (const auto& definition : report.definitions)
    {
        if (definition.checks)
        {
            roleEvidence = roleEvidence || matchesRole(*definition.checks);
        }
    }
    std::cerr << label << ": status=" << StatusName(status) << " published=" << (published ? "true" : "false")
              << " discarded=" << (report.candidateDiscarded ? "true" : "false") << " budget=" << budget << " bytes=" << report.json.size()
              << " root_status=" << (report.json.empty() ? std::string_view("absent") : JsonMember(report.json, "status"))
              << " root_model_null=" << (!report.json.empty() && JsonMember(report.json, "model") == "null" ? "true" : "false")
              << " byte_limit_diagnostic=" << (byteLimit ? "true" : "false")
              << " serialized_byte_limit_diagnostic=" << (report.json.find("ImportReportByteLimit") != std::string::npos ? "true" : "false")
              << " role_evidence=" << (roleEvidence ? "true" : "false") << '\n';
}
void CheckReportBudgetFailure(
    const char* label,
    Status status,
    bool published,
    const Report& report,
    std::size_t budget,
    const Truth& truth,
    bool assembly
)
{
    try
    {
        Require(
            status == Status::ResourceLimit && !published && report.candidateDiscarded,
            "Report N-1 discards the candidate and never publishes an owner"
        );
        Require(
            std::any_of(
                report.diagnostics.begin(),
                report.diagnostics.end(),
                [](const auto& diagnostic)
                {
                    return diagnostic.source.key == "ImportReportByteLimit";
                }
            ),
            "Typed mandatory-report byte-limit diagnostic retained"
        );
        if (report.json.empty())
        {
            return;
        }
        Require(report.json.size() <= budget && JsonSyntax(report.json).CompleteObject(), "Failure JSON fits its budget and is complete valid JSON");
        Require(
            JsonMember(report.json, "status") == "\"ResourceLimit\"" && JsonMember(report.json, "model") == "null" &&
                JsonMember(report.json, "candidate_discarded") == "true",
            "Root failure JSON cannot claim accepted publication"
        );
        Require(
            JsonMember(report.json, "diagnostics").find("\"key\":\"ImportReportByteLimit\"") != std::string_view::npos,
            "Serialized complete failure retains the actual mandatory-report diagnostic"
        );
        const Report* evidence = &report;
        std::string_view json = report.json;
        if (assembly)
        {
            Require(report.definitions.size() == 2 && report.definitions[0].checks, "Complete nested surface observations survive assembly failure");
            evidence = report.definitions[0].checks.get();
            const auto document = JsonMember(report.json, "product_document");
            Require(JsonMember(document, "complete") == "false", "Failed assembly report cannot advertise a complete published product document");
            const auto definition = FirstJsonArrayValue(JsonMember(document, "definitions"));
            Require(JsonMember(definition, "source_definition_id") == "\"7\"", "Failure evidence binds the selected public surface definition");
            json = JsonMember(definition, "checks");
            Require(
                JsonMember(json, "status") == "\"ResourceLimit\"" && JsonMember(json, "model") == "null",
                "Nested evidence cannot masquerade as a published model"
            );
        }
        Require(
            evidence->sourceShape && evidence->sourceShape->role == truth.role && evidence->nativeRole &&
                evidence->nativeRole->materialRegions == 0 && evidence->native && !evidence->native->materialVolumeMm3 && !evidence->solidRegions &&
                !evidence->voidCavities && !evidence->volumeBoundMm3 && !evidence->volumeJunctionBoundMm && !evidence->volumeGapFluxBoundMm3 &&
                !evidence->volumeSkippedFluxBoundMm3,
            "Failure preserves true nonmaterial role and inapplicable material evidence"
        );
        Require(
            JsonMember(JsonMember(json, "source_shape"), "role") == "\"" + std::string(truth.role) + "\"",
            "Complete failure JSON retains the selected source role"
        );
        const auto native = JsonMember(json, "native_role");
        Require(
            JsonMember(native, "regions") == (truth.closed ? "2" : "1") && JsonMember(native, "shells") == (truth.closed ? "2" : "1") &&
                JsonMember(native, "material_regions") == "0" && JsonMember(native, "bounded_void_regions") == (truth.closed ? "1" : "0"),
            "Complete failure JSON retains actual native surface role evidence"
        );
        const auto census = JsonMember(json, "material_census");
        Require(
            JsonMember(census, "solid_regions") == "null" && JsonMember(census, "void_cavities") == "null" &&
                JsonMember(JsonMember(json, "measurement_policy"), "material_volume") == "\"NotApplicable\"",
            "Complete failure JSON distinguishes inapplicable volume from incomplete work"
        );
    }
    catch (...)
    {
        ReportBudgetDiagnostic(label, status, published, report, budget, truth.role);
        throw;
    }
}
void Budgets(const std::filesystem::path& fixture, bool band)
{
    const auto truth = Expected(band ? "cylinder_band" : "plane");
    const auto bytes = band ? BandBytes(fixture) : ReadPublic(fixture);
    for (auto encoding : { ReportEncoding::ExpandedV1, ReportEncoding::CompactV2 })
    {
        auto options = band ? BandOptions() : General();
        options.reportEncoding = encoding;
        const auto baseline = Accepted(bytes, options);
        CheckStatistics(baseline.model->Info(), truth);
        CheckSurfaceReport(baseline, truth);
        const auto acceptedBounds = baseline.model->Info().boundsMm;
        const auto calls = baseline.report.nativeCalls;
        Require(calls > 0 && !baseline.report.json.empty(), "Complete baseline exposes actual accounting boundaries");
        auto limit = options;
        limit.nativeLimits.nativeCalls = calls;
        Require(ImportBytes(bytes, limit).model != nullptr, "Native call budget N publishes the complete selected surface");
        for (std::size_t boundary : { std::size_t(0), calls / 2, calls - 1 })
        {
            limit.nativeLimits.nativeCalls = boundary;
            const auto failed = ImportBytes(bytes, limit);
            Require(
                failed.status == Status::ResourceLimit && !failed.model && failed.report.nativeCalls == boundary,
                "Native budget boundary publishes no prefix of the selected surface"
            );
        }
        limit = options;
        limit.nativeLimits.reportBytes = baseline.report.json.size();
        const auto exact = ImportBytes(bytes, limit);
        Require(exact.model && exact.report.json == baseline.report.json, "Complete general report fits exact byte budget N");
        --limit.nativeLimits.reportBytes;
        const auto shortReport = ImportBytes(bytes, limit);
        CheckReportBudgetFailure(
            band ? "band_report_n_minus_one" : "plane_report_n_minus_one",
            shortReport.status,
            bool(shortReport.model),
            shortReport.report,
            limit.nativeLimits.reportBytes,
            truth,
            false
        );
        for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::ReportReady, Phase::BeforeCommit })
        {
            auto cancelledOptions = options;
            const auto control = std::make_shared<ImportControl>();
            cancelledOptions.execution.control = control;
            cancelledOptions.execution.progress = [control, phase](Progress progress)
            {
                if (progress.phase == phase)
                {
                    control->RequestCancel();
                }
            };
            const auto cancelled = ImportBytes(bytes, cancelledOptions);
            Require(
                cancelled.status == Status::Cancelled && !cancelled.model,
                "Cancellation at native/report/commit boundaries withholds surface publication"
            );
            if (phase != Phase::BeforeNativeCall)
            {
                Require(
                    cancelled.report.candidateDiscarded && cancelled.report.native && cancelled.report.native->shapeRole == truth.role,
                    "Late cancellation retains completed candidate role observations without publishing it"
                );
            }
        }
        Require(
            baseline.model->Info().boundsMm == acceptedBounds && baseline.report.source->Bytes() == bytes,
            "Failed sibling requests cannot mutate the accepted surface or immutable source"
        );
    }
}
void Assembly(const std::filesystem::path& fixture)
{
    auto bytes = ReadPublic(fixture);
    Replace(bytes, "#20=ADVANCED_BREP_SHAPE_REPRESENTATION", "#20=MANIFOLD_SURFACE_SHAPE_REPRESENTATION");
    Replace(bytes, "#21=MANIFOLD_SOLID_BREP('',#22);", "#21=SHELL_BASED_SURFACE_MODEL('',(#22));");
    const auto truth = Expected("closed");
    const AssemblyLimits limits{ 2, 3, 1 };
    for (auto encoding : { ReportEncoding::ExpandedV1, ReportEncoding::CompactV2 })
    {
        auto options = General();
        options.reportEncoding = encoding;
        options.numericalIntegration = NumericalIntegrationSettings{ 1e-7 };
        auto result = ImportAssemblyBytes(bytes, options, limits);
        if (!result.assembly)
        {
            for (const auto& diagnostic : result.report.diagnostics)
            {
                std::cerr << diagnostic.source.key << '\n';
            }
        }
        Require(result.status == Status::Accepted && result.assembly, "Surface definition and placement-only parent publish together");
        auto assembly = result.assembly;
        Require(
            assembly->DefinitionCount() == 2 && assembly->DefinitionRole(0) == "ClosedSurfaceModel" &&
                assembly->DefinitionRole(1) == "AssemblyPlacementWrapper" && assembly->BodyCount(0) == 1 && assembly->BodyCount(1) == 0 &&
                assembly->InstanceCount() == 3,
            "Repeated placements share one local surface definition"
        );
        const auto first = assembly->Instance(1), second = assembly->Instance(2);
        Require(
            first.definitionIndex == second.definitionIndex && first.translationToRootMm == Point{ 100., 200., 300. } &&
                second.translationToRootMm == Point{ -40., 0., 0. },
            "Independent assembly placement oracle; no baked or unioned body"
        );
        auto body = assembly->Body(0, 0);
        CheckStatistics(body->Info(), truth);
        Require(!body->MaterialVolumeMm3() && !body->ClassifyMaterial(truth.centroid), "Nested surface definition never gains material qualification");
        CheckIntegration(*body, 1e-7);
        Require(
            result.report.definitions.size() == 2 && result.report.definitions[0].checksPassed && result.report.definitions[1].checksPassed &&
                result.report.definitions[1].checks->nativeCalls == 0 && !result.report.definitions[1].checks->numericalIntegration,
            "Placement-only wrapper has no fabricated native body or integral"
        );
        const auto& nested = *result.report.definitions[0].checks;
        Require(
            nested.sourceShape && nested.sourceShape->role == truth.role && nested.nativeRole && nested.nativeRole->materialRegions == 0 &&
                nested.adjacency.size() == 12 && nested.faces.size() == 6,
            "Nested C++ report retains complete surface role and incidence"
        );
        // The complete published assembly JSON must retain child evidence;
        // checking the in-memory child report alone would miss serializer gaps.
        Require(
            result.report.json.find("\"source_shape\"") != std::string::npos &&
                result.report.json.find("\"ClosedSurfaceModel\"") != std::string::npos && Occurrences(result.report.json, "\"FaceSurface\"") >= 6 &&
                result.report.json.find("\"edge_adjacency\"") != std::string::npos,
            "Complete assembly JSON includes nested surface evidence"
        );
        auto exact = options;
        exact.nativeLimits.reportBytes = result.report.json.size();
        const auto complete = ImportAssemblyBytes(bytes, exact, limits);
        Require(complete.assembly && complete.report.json == result.report.json, "Nested report byte budget N publishes the whole assembly");
        --exact.nativeLimits.reportBytes;
        const auto shortReport = ImportAssemblyBytes(bytes, exact, limits);
        CheckReportBudgetFailure(
            "assembly_report_n_minus_one",
            shortReport.status,
            bool(shortReport.assembly),
            shortReport.report,
            exact.nativeLimits.reportBytes,
            truth,
            true
        );
        for (auto phase : { Phase::NativeReady, Phase::BeforeCommit })
        {
            auto cancelledOptions = options;
            const auto control = std::make_shared<ImportControl>();
            cancelledOptions.execution.control = control;
            cancelledOptions.execution.progress = [control, phase](Progress progress)
            {
                if (progress.phase == phase)
                {
                    control->RequestCancel();
                }
            };
            const auto cancelled = ImportAssemblyBytes(bytes, cancelledOptions, limits);
            Require(cancelled.status == Status::Cancelled && !cancelled.assembly, "Late nested cancellation withholds complete assembly publication");
        }
        result = {};
        assembly.reset();
        CheckStatistics(body->Info(), truth);
        Require(body->Source()->Source().Bytes() == bytes, "Nested body/source outlive assembly and mutable report");
    }
}
void Basic(const std::filesystem::path& fixture, const Truth& truth)
{
    auto imported = ImportFile(fixture, General());
    if (imported.status != Status::Accepted || !imported.model)
    {
        for (const auto& diagnostic : imported.report.diagnostics)
        {
            std::cerr << diagnostic.source.key << '\n';
        }
        throw std::runtime_error("General must accept the requested public surface or material role");
    }
    CheckStatistics(imported.model->Info(), truth);
    Require(imported.report.pointerReports == 0 && imported.report.deepReports == 0, "Both native validators ran");
    if (truth.material)
    {
        Require(
            imported.model->MaterialVolumeMm3().has_value() && imported.model->ClassifyMaterial(truth.centroid) == MaterialPointClass::Inside,
            "General material control retains volume and material classification"
        );
        Near(*imported.model->MaterialVolumeMm3(), 6000., "Independent material control volume");
    }
    else
    {
        Require(
            !imported.model->MaterialVolumeMm3() && !imported.model->ClassifyMaterial(truth.centroid),
            "Surface APIs never grant material qualification"
        );
        CheckSurfaceReport(imported, truth);
    }
    auto copy = imported.model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "Normal independent copy is available");
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            CheckNative(brep, truth);
        }
    );
    // This positive Copy path checks actual topology. It makes no claim about
    // full-BRep failure atomicity or allocation/merge failure cleanup.
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3 || argc == 4, "Pass mode, public STEP fixture and optional disposable output directory");
        CheckJsonTestGuards();
        const std::string mode(argv[1]);
        if (mode == "cylinder_band")
        {
            Band(argv[2]);
        }
        else if (mode.starts_with("archive_"))
        {
            Require(argc == 4, "Archive mode requires disposable output directory");
            Archive(argv[2], argv[3], mode);
        }
        else if (mode == "lifecycle")
        {
            Lifecycle(argv[2]);
        }
        else if (mode == "integration")
        {
            Integration(argv[2]);
        }
        else if (mode == "budget_plane" || mode == "budget_band")
        {
            Budgets(argv[2], mode == "budget_band");
        }
        else if (mode == "assembly_surface")
        {
            Assembly(argv[2]);
        }
        else
        {
            Basic(argv[2], Expected(mode));
        }
        std::cout << "general_surface_role_contract: true\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
