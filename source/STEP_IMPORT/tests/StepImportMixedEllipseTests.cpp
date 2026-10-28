// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmEllipse.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
#include <numbers>
#include <set>

namespace
{
using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
Options Settings()
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    return o;
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    Require(bool(in), "public mixed ellipse fixture");
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}
void Native(SmBrep& brep, bool material)
{
    SmAssertArray pointers, deep;
    Require(brep.ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize(), "mixed ellipse native pointers");
    Require(brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && !deep.GetSize(), "mixed ellipse LEVEL2");
    step_import_test::CheckNativeWorkBudget(brep, .001);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    brep.GetFaces(faces);
    brep.GetEdges(edges);
    brep.GetVertices(vertices);
    Require(
        faces.GetSize() == (material ? 4U : 1U) && edges.GetSize() == (material ? 6U : 5U) && vertices.GetSize() == 4,
        "one global ellipse subdivision and one meridian, shared with caps"
    );
    std::vector<const SmCurve*> ellipse;
    std::set<std::pair<double, double>> intervals;
    for (const auto edge : edges)
    {
        if (edge->GetCurve()->GetType() == SmEllipse_TYPE)
        {
            ellipse.push_back(edge->GetCurve());
            const auto iv = edge->GetInterval();
            intervals.emplace(iv.GetMin(), iv.GetMax());
        }
    }
    Require(ellipse.size() == 2 && intervals.size() == 2, "two children retain analytic ellipse class");
    step_import_test::CompareCurveCoefficients(*ellipse[0], *ellipse[1]);
    Require(
        intervals.begin()->first == 0 && intervals.rbegin()->second == 1 && intervals.begin()->second == intervals.rbegin()->first,
        "complete original native ellipse interval, without reparameterizing either child"
    );
    bool derived = false;
    for (const auto v : vertices)
    {
        const auto p = v->GetPoint();
        derived = derived || (std::abs(p.x - 2) < 1e-9 && std::abs(p.y) < 1e-9 && std::abs(p.z - 2.5) < 1e-9);
    }
    Require(derived, "derived cut is on the unchanged ellipse at the source meridian");
}
Outcome Check(const std::string& text, const std::filesystem::path& archive, bool material)
{
    auto r = ImportBytes(text, Settings());
    if (!r.model)
    {
        for (const auto& d : r.report.diagnostics)
        {
            std::cerr << d.source.key << '\n';
        }
    }
    Require(r.status == Status::Accepted && r.model, "complete mixed ellipse import");
    const auto model = r.model;
    Require(model->Source()->Source().Bytes() == text, "immutable source bytes");
    Require(model->Info().shapeRole == (material ? "MaterialSolid" : "OpenSurfaceModel"), "source material/surface role retained");
    // R=2. Side height is 3 on one half and 3+1.5*cos(theta) on the other:
    // side area=12*pi+6. Caps add 4*pi+2*pi+2.5*pi; volume=12*pi+4.
    const double expectedArea = (material ? 20.5 : 12) * std::numbers::pi + 6;
    // As in the other general-import controls, compare the area observation at
    // twice the requested 1e-4 relative quadrature stopping accuracy. Derived
    // trims retain their original 1e-3 mm budget; this is no new geometry or
    // continuous-accuracy allowance.
    Near(model->Info().areaMm2, expectedArea, expectedArea * 2e-4, "independent area integral");
    if (material)
    {
        Require(model->MaterialVolumeMm3().has_value(), "explicit material volume is applicable");
        Near(*model->MaterialVolumeMm3(), 12 * std::numbers::pi + 4, 1e-4, "independent material volume");
    }
    else
    {
        Require(!model->MaterialVolumeMm3(), "closed angular support does not grant material semantics");
    }
    const auto normalization = model->TopologyNormalization();
    Require(
        normalization && normalization->completed && normalization->sourceCurveIntervalsCovered && normalization->sourceVerticesPreserved,
        "owning complete source-use/definition coverage"
    );
    Require(
        std::count_if(
            normalization->edges.begin(),
            normalization->edges.end(),
            [](const auto& e)
            {
                return e.sourceEdgeId == "72";
            }
        ) == 2,
        "both child edges retain the original source ellipse identity"
    );
    auto copy = model->CopyBrepForModeling();
    Require(copy.model != nullptr, "independent modeling copy");
    copy.model->Edit(
        [&](SmBrep& b)
        {
            Native(b, material);
        }
    );
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
        "complete native archive replay"
    );
    Native(*bodies[0], material);
    copy.model->Edit(
        [&](SmBrep& b)
        {
            step_import_test::CompareNativeDefinitions(b, *bodies[0]);
        }
    );
    const auto closest = model->Face().Closest({ 3, 0, -1 });
    Near(closest.distanceMm, 1., 1e-6, "independent cylinder boundary query");
    r.report = {};
    Require(model->TopologyNormalization()->completed, "snapshot correspondence survives report release");
    return ImportBytes(text, Settings());
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "fixture directory and output required");
        const auto fixtures = std::filesystem::path(argv[1]);
        const auto directory = std::filesystem::path(argv[2]);
        std::filesystem::create_directories(directory);
        const auto text = Read(fixtures / "mixed_ellipse_cylinder_band.step");
        auto r = Check(text, directory / "surface.smb", false);
        const auto calls = r.report.nativeCalls, exact = r.report.exactGeometryWork ? r.report.exactGeometryWork->usedUnits : 0,
                   bytes = r.report.json.size();
        for (unsigned mode = 0; mode < 3; ++mode)
        {
            auto o = Settings();
            if (mode == 0)
            {
                o.nativeLimits.nativeCalls = calls;
            }
            if (mode == 1)
            {
                o.nativeLimits.exactWorkUnits = exact;
            }
            if (mode == 2)
            {
                o.nativeLimits.reportBytes = bytes;
            }
            Require(ImportBytes(text, o).status == Status::Accepted, "exact selected execution boundary");
            // This native meridian survey may need no exact-arithmetic work.
            // An absent receipt means zero use, not an engaged optional or a
            // positive minimum. There is no unsigned N-1 boundary for N=0.
            if (mode == 1 && exact == 0)
            {
                continue;
            }
            if (mode == 0)
            {
                --o.nativeLimits.nativeCalls;
            }
            if (mode == 1)
            {
                --o.nativeLimits.exactWorkUnits;
            }
            if (mode == 2)
            {
                --o.nativeLimits.reportBytes;
            }
            const auto failed = ImportBytes(text, o);
            if (failed.status != Status::ResourceLimit || failed.model)
            {
                std::cerr << "budget mode=" << mode << " status=" << static_cast<int>(failed.status) << " published=" << bool(failed.model) << '\n';
            }
            Require(failed.status == Status::ResourceLimit && !failed.model, "N-1 withholds the whole candidate");
        }
        for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeReport, Phase::BeforeCommit })
        {
            auto o = Settings();
            o.execution.control = std::make_shared<ImportControl>();
            o.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    o.execution.control->RequestCancel();
                }
            };
            auto failed = ImportBytes(text, o);
            Require(failed.status == Status::Cancelled && !failed.model, "cancel releases all unpublished subdivisions");
        }
        Check(Read(fixtures / "mixed_ellipse_cylinder_solid.step"), directory / "material.smb", true);
        auto off = text;
        auto at = off.find("ELLIPSE('',#51,2.5,2.)");
        Require(at != std::string::npos, "independent source gap mutation");
        off.replace(at, std::string("ELLIPSE('',#51,2.5,2.)").size(), "ELLIPSE('',#51,2.6,2.)");
        Require(!ImportBytes(off, Settings()).model, "seam planning cannot fit an off-surface defining ellipse");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
