// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmCylinder.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void RebuildContract(const step_import::ModelSnapshot& imported, bool adjust, bool validateOnly)
{
    auto copy = imported.CopyForModeling();
    Require(bool(copy.model), "independent modeling copy");
    copy.model->Edit([&](SmBrep& brep) {
        SmTArray<SmFace*> faces;
        SmTArray<SmEdge*> edges;
        SmTArray<SmVertex*> vertices;
        brep.GetFaces(faces);
        brep.GetEdges(edges);
        brep.GetVertices(vertices);
        Require(faces.GetSize() == 3 && edges.GetSize() == 5 && vertices.GetSize() == 4, "complete split-rim cylinder");

        std::vector<double> edgeTolerances, vertexTolerances;
        std::vector<SmCurve*> curves;
        std::vector<SmExtent1d> intervals;
        std::vector<std::array<SmPoint3d, 5>> samples;
        std::vector<SmPoint3d> points;
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            edgeTolerances.push_back(SmTol::GetZoneTol3d(edges[i]));
            curves.push_back(edges[i]->GetCurve());
            intervals.push_back(edges[i]->GetInterval());
            std::array<SmPoint3d, 5> evaluated;
            for (unsigned j = 0; j < evaluated.size(); ++j)
            {
                Require(curves.back()->EvaluatePoint(intervals.back().Evaluate(double(j) / 4), evaluated[j]) == SM_SUCCESS,
                        "original defining curve evaluation");
            }
            samples.push_back(evaluated);
        }
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            vertexTolerances.push_back(SmTol::GetZoneTol3d(vertices[i]));
            points.push_back(vertices[i]->GetPoint());
        }

        // Rebuild only derived trims on an independent modeling copy. Its
        // edits cannot grant or change the snapshot/report's import evidence.
        brep.m_bValidateOnly = validateOnly ? TRUE : FALSE;
        ULONG rebuilt = 0;
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            if (!faces[i]->GetSurface()->IsKindOf(SmCylinder_TYPE))
            {
                continue;
            }
            ++rebuilt;
            const auto* surface = faces[i]->GetSurface();
            Require(faces[i]->RemoveUVTrimCurves() == SM_SUCCESS, "remove only derived UV trims");
            double meanCurve = 0, maxCurve = 0, meanVertex = 0, maxVertex = 0, meanUv = 0, maxUv = 0;
            const auto status = faces[i]->CreateUVTrimCurves(adjust ? TRUE : FALSE, nullptr, nullptr,
                meanCurve, maxCurve, meanVertex, maxVertex, meanUv, maxUv);
            Require(status == SM_SUCCESS, "rebuild derived UV trims");
            Require(bool(brep.m_bValidateOnly) == validateOnly, "restore enclosing validation policy");
            Require(faces[i]->GetSurface() == surface, "preserve defining surface");
            Require(std::isfinite(maxCurve) && maxCurve > 0 && maxCurve < .001,
                    "retain measured approximation instead of hiding its gap");

            SmTArray<SmEdgeuse*> uses;
            faces[i]->GetUpwardEdgeuses(uses);
            Require(uses.GetSize() == 6, "preserve each logical cylindrical boundary use");
            for (ULONG j = 0; j < uses.GetSize(); ++j)
            {
                SmEdgeuse* owner = nullptr;
                const auto* uv = uses[j]->GetUVTrimCurve(&owner);
                const auto interval = uses[j]->GetEdge()->GetInterval();
                Require(uv && owner && owner->GetOrientation() == SM_OT_SAME && uv->GetOwner() == owner,
                        "one SAME-owner UV per mate pair");
                Require(std::abs(uv->GetNaturalInterval().GetMin() - interval.GetMin()) < 1e-12 &&
                        std::abs(uv->GetNaturalInterval().GetMax() - interval.GetMax()) < 1e-12,
                        "preserve actual edge parameter interval");
            }
        }
        Require(rebuilt == 1, "one cylindrical face rebuilt");
        bool grew = false;
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            const double tolerance = SmTol::GetZoneTol3d(edges[i]);
            grew |= tolerance > edgeTolerances[i];
            if (!adjust || validateOnly)
            {
                Require(tolerance == edgeTolerances[i], "no-adjust must preserve every edge working tolerance");
            }
            Require(edges[i]->GetCurve() == curves[i] && edges[i]->GetInterval() == intervals[i],
                    "preserve defining curve and modeled parameter interval");
            for (unsigned j = 0; j < samples[i].size(); ++j)
            {
                SmPoint3d actual;
                Require(curves[i]->EvaluatePoint(intervals[i].Evaluate(double(j) / 4), actual) == SM_SUCCESS &&
                        actual.DistanceBetween(samples[i][j]) == 0, "preserve defining curve values");
            }
        }
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            Require(vertices[i]->GetPoint().DistanceBetween(points[i]) == 0, "preserve source vertex positions");
            if (!adjust || validateOnly)
            {
                Require(double(SmTol::GetZoneTol3d(vertices[i])) == vertexTolerances[i],
                        "no-adjust must preserve every vertex working tolerance");
            }
        }
        if (adjust && !validateOnly)
        {
            Require(grew, "retain explicitly requested legacy tolerance adjustment");
        }
    });
}

void FailedCallRestoresPolicy(const step_import::ModelSnapshot& imported, bool validateOnly)
{
    auto copy = imported.CopyForModeling();
    Require(bool(copy.model), "independent failure-control copy");
    copy.model->Edit([&](SmBrep& brep) {
        brep.m_bValidateOnly = validateOnly ? TRUE : FALSE;
        SmTArray<SmFace*> faces;
        brep.GetFaces(faces);
        SmTArray<SmBSplineCurve*> supplied;
        double meanCurve = 0, maxCurve = 0, meanVertex = 0, maxVertex = 0, meanUv = 0, maxUv = 0;
        // Supplied UVs require a matching orientation array. This public
        // argument failure occurs inside the no-adjust scope, before mutation.
        Require(faces[0]->CreateUVTrimCurves(FALSE, &supplied, nullptr,
                    meanCurve, maxCurve, meanVertex, maxVertex, meanUv, maxUv) != SM_SUCCESS,
                "reject incomplete supplied-UV arguments");
        Require(bool(brep.m_bValidateOnly) == validateOnly, "restore policy on failed call");
    });
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "split-rim fixture path");
        auto options = step_import::Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = step_import::Quality::GeneralDevelopment();
        const auto imported = step_import::ImportFile(argv[1], options);
        Require(imported.status == step_import::Status::Accepted && imported.model, "complete source import");
        RebuildContract(*imported.model, false, false);
        RebuildContract(*imported.model, false, true);
        RebuildContract(*imported.model, true, false);
        RebuildContract(*imported.model, true, true);
        FailedCallRestoresPolicy(*imported.model, false);
        FailedCallRestoresPolicy(*imported.model, true);
        std::cout << "UV tolerance policy contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "UV tolerance contract failed: " << error.what() << '\n';
        return 1;
    }
}
