// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmCylinder.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmLine.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
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

// Preserve real cylinder evaluation and the real DropAndTrimCurve fallback.
// Only the replaceable first projection strategy reports nonconvergence: no
// candidate UV, deviation or recovery-path result is fabricated by this class.
class FallbackCylinder final : public SmCylinder
{
public:
    explicit FallbackCylinder(const SmContext* context) :
        SmCylinder(SmPoint3d(0, 0, 0), SmVector3d(std::cos(.2), std::sin(.2), 0),
            SmVector3d(-std::sin(.2), std::cos(.2), 0), 3.0014, 0, 360, 7, FALSE, FALSE, FALSE, context)
    {
    }

    mutable std::vector<double> requestedAccuracies;

    SmStatus DropCurve(const SmContext&, const SmExtent2d&, const SmCurve&, const SmExtent1d&,
        SmApproxTol3d accuracy, double& distance, double& deviation, SmTArray<SmBSplineCurve*>& curves,
        SmBoolean, SmDropCurveFail*, SmBoolean) const override
    {
        requestedAccuracies.push_back(double(accuracy));
        curves.ReSet();
        distance = 0;
        deviation = 0;
        return SM_ERR_NOT_CONVERGING;
    }

    SM_COMMON(FallbackCylinder, SmCylinder, 19989);
};

SmBoolean FallbackCylinder::IsKindOf(SM_TYPE type) const
{
    return type == 19989 || SmCylinder::IsKindOf(type);
}

void FallbackCylinder::Dump() const
{
    SmCylinder::Dump();
}

void RebuildContract(const step_import::ModelSnapshot& imported, bool adjust, bool validateOnly, bool clearDerived = true)
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
            if (clearDerived)
            {
                Require(faces[i]->RemoveUVTrimCurves() == SM_SUCCESS, "remove only derived UV trims");
            }
            double meanCurve = 0, maxCurve = 0, meanVertex = 0, maxVertex = 0, meanUv = 0, maxUv = 0;
            const auto status = faces[i]->CreateUVTrimCurves(adjust ? TRUE : FALSE, nullptr, nullptr,
                meanCurve, maxCurve, meanVertex, maxVertex, meanUv, maxUv);
            Require(status == SM_SUCCESS, "rebuild derived UV trims");
            Require(bool(brep.m_bValidateOnly) == validateOnly, "restore enclosing validation policy");
            Require(faces[i]->GetSurface() == surface, "preserve defining surface");
            Require(std::isfinite(maxCurve) && maxCurve >= 0 && maxCurve < .001, "finite projection observation");
            if (clearDerived)
            {
                Require(maxCurve > 0, "retain measured approximation instead of hiding its gap");
            }

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

void ProjectionRetryPreservesPolicy(const step_import::ModelSnapshot& imported)
{
    auto copy = imported.CopyForModeling();
    Require(bool(copy.model), "independent retry-control copy");
    copy.model->Edit([&](SmBrep& brep) {
        SmTArray<SmFace*> faces;
        SmTArray<SmEdge*> edges;
        SmTArray<SmVertex*> vertices;
        brep.GetFaces(faces);
        brep.GetEdges(edges);
        brep.GetVertices(vertices);
        std::vector<double> edgeTolerances, vertexTolerances;
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            edgeTolerances.push_back(SmTol::GetZoneTol3d(edges[i]));
        }
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            vertexTolerances.push_back(SmTol::GetZoneTol3d(vertices[i]));
        }
        brep.m_bValidateOnly = TRUE;
        bool observedOutsideOriginalBudget = false;
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            auto* face = faces[i];
            if (!face->GetSurface()->IsKindOf(SmCylinder_TYPE))
            {
                continue;
            }
            Require(face->RemoveUVTrimCurves() == SM_SUCCESS, "fresh retry projection");
            auto domain = face->GetUVDomain();
            // Deliberately make a negative modeling control: this fixture's V
            // direction is axial, so the cut excludes 7*.0002 = .0014 mm of
            // the boundary, beyond its original .001 mm working tolerance.
            // It is not a qualified import result. A relaxed projection may
            // return a candidate or fail, but neither may change validation's
            // working tolerance. The normal exact cylinder only tests UV
            // installation, and cannot expose an unguarded projection retry.
            Require(domain.SetVMin(domain.GetVMin() + .0002 * (domain.GetVMax() - domain.GetVMin())) == SM_SUCCESS,
                    "negative-control modeled domain");
            face->SetUVDomain(domain);
            SmTArray<SmEdgeuse*> uses;
            face->GetUpwardEdgeuses(uses);
            for (ULONG j = 0; j < uses.GetSize(); ++j)
            {
                SmBSplineCurve* raw = nullptr;
                double maximum = 0;
                const auto status = uses[j]->CreateUVTrimCurve(maximum, raw);
                std::unique_ptr<SmBSplineCurve> cleanup(raw);
                observedOutsideOriginalBudget |= status == SM_SUCCESS && maximum > .001;
            }
        }
        Require(observedOutsideOriginalBudget, "exercise relaxed projection, not only exact/cached UV");
        Require(brep.m_bValidateOnly != FALSE, "projection preserves enclosing validation policy");
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            Require(double(SmTol::GetZoneTol3d(edges[i])) == edgeTolerances[i], "projection retry must preserve edge tolerance");
        }
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            Require(double(SmTol::GetZoneTol3d(vertices[i])) == vertexTolerances[i], "projection retry must preserve vertex tolerance");
        }
    });
}

void ShortProjectionRetryContract(const step_import::ModelSnapshot& imported, bool validateOnly, bool shiftedParameters)
{
    const std::string originalSource = imported.Source()->Source().Bytes();
    auto copy = imported.CopyForModeling();
    Require(bool(copy.model), "independent short-retry copy");
    copy.model->Edit([&](SmBrep& brep) {
        SmTArray<SmFace*> faces;
        brep.GetFaces(faces);
        SmFace* face = nullptr;
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            if (faces[i]->GetSurface()->IsKindOf(SmCylinder_TYPE))
            {
                face = faces[i];
            }
        }
        Require(face != nullptr, "short-retry cylindrical face");

        // Negative modeling control only: source radius 3, support radius
        // 3.0014 and an axial face-domain cut of 7*.00005 = .00035 mm.
        // The .0014 mm normal gap exceeds the original .001 mm solver request,
        // but the clipped endpoint is still within the existing .002 mm
        // intersection tolerance. Neither model tolerance is changed here.
        auto* surface = new (*brep.GetContext()) FallbackCylinder(brep.GetContext());
        face->SetSurface(surface, TRUE);
        Require(face->RemoveUVTrimCurves() == SM_SUCCESS, "fresh short-retry UV");
        auto domain = surface->GetNaturalUVDomain();
        Require(domain.SetVMin(domain.GetVMin() + .00005 * (domain.GetVMax() - domain.GetVMin())) == SM_SUCCESS,
                "short-retry axial domain");
        face->SetUVDomain(domain);

        SmTArray<SmEdgeuse*> uses;
        face->GetUpwardEdgeuses(uses);
        SmEdgeuse* use = nullptr;
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            if (uses[i]->GetEdge()->GetCurve()->IsKindOf(SmLine_TYPE) && uses[i]->GetOrientation() == SM_OT_SAME)
            {
                use = uses[i];
            }
        }
        Require(use != nullptr, "short-retry axial edgeuse");
        auto* edge = use->GetEdge();
        auto* curve = edge->GetCurve();
        if (shiftedParameters)
        {
            // An explicit fixture variant, before the operation under test.
            // The expected lift below retains this actual basis parameter;
            // it must not be silently normalized to the surviving edge span.
            Require(curve->EditParameterization(SmExtent1d(2, 5)) == SM_SUCCESS, "shifted fixture parameters");
            edge->SetInterval(SmExtent1d(2, 5));
        }
        const auto basis = curve->GetNaturalInterval();
        const auto originalInterval = edge->GetInterval();
        std::array<SmPoint3d, 5> originalCurvePoints;
        for (unsigned i = 0; i < originalCurvePoints.size(); ++i)
        {
            Require(curve->EvaluatePoint(basis.Evaluate(double(i) / 4), originalCurvePoints[i]) == SM_SUCCESS,
                    "original short-retry basis evaluation");
        }
        SmTArray<SmEdge*> edges;
        SmTArray<SmVertex*> vertices;
        brep.GetEdges(edges);
        brep.GetVertices(vertices);
        std::vector<double> edgeTolerances, vertexTolerances;
        std::vector<SmPoint3d> vertexPoints;
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            edgeTolerances.push_back(SmTol::GetZoneTol3d(edges[i]));
        }
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            vertexTolerances.push_back(SmTol::GetZoneTol3d(vertices[i]));
            vertexPoints.push_back(vertices[i]->GetPoint());
        }
        const double originalTolerance = SmTol::GetZoneTol3d(edge);
        Require(originalTolerance == .001, "original short-retry working tolerance");
        brep.m_bValidateOnly = validateOnly ? TRUE : FALSE;
        SmBSplineCurve* raw = nullptr;
        double maximum = 0;
        SmDropCurveFail failure;
        const auto status = use->CreateUVTrimCurve(maximum, raw, nullptr, &failure);
        std::unique_ptr<SmBSplineCurve> uv(raw);
        Require(status == SM_SUCCESS && uv, "real short-retry fallback result");
        Require(failure.m_eCreatePathType == SM_CTC_1PERCENT_EDGETRIM_RECOVERY1, "short-interval recovery path");
        bool originalAccuracyTried = false, relaxedAccuracyTried = false;
        for (double accuracy : surface->requestedAccuracies)
        {
            originalAccuracyTried |= accuracy == originalTolerance;
            relaxedAccuracyTried |= accuracy > originalTolerance;
        }
        Require(originalAccuracyTried && relaxedAccuracyTried, "reach retry after original-accuracy fallback fails");
        Require(std::isfinite(maximum) && std::abs(maximum - .0014) < 1e-10, "retain real short-retry gap");

        // Pin the existing short-interval recovery, including its modeled-edge
        // trim. The original basis curve and its parameterization stay intact;
        // this unqualified copy is never published as an accepted STEP model.
        const auto interval = edge->GetInterval();
        Require(interval.GetMin() > originalInterval.GetMin() && interval.GetMax() == originalInterval.GetMax(),
                "shortened modeled interval, not the full-interval retry branch");
        Require(std::abs(interval.GetMin() - originalInterval.Evaluate(.00005)) < 1e-12,
                "physical axial cut retains original basis parameter");
        Require(uv->GetNaturalInterval() == interval && edge->GetCurve() == curve && curve->GetNaturalInterval() == basis,
                "UV/edge parameter pairing and unchanged basis");
        for (unsigned i = 0; i < originalCurvePoints.size(); ++i)
        {
            SmPoint3d actual;
            Require(curve->EvaluatePoint(basis.Evaluate(double(i) / 4), actual) == SM_SUCCESS &&
                    actual.DistanceBetween(originalCurvePoints[i]) == 0, "unchanged 3D basis curve");
        }
        for (unsigned i = 0; i <= 16; ++i)
        {
            const double parameter = interval.Evaluate(double(i) / 16);
            SmPoint3d point, lifted;
            Require(uv->EvaluatePoint(parameter, point) == SM_SUCCESS &&
                    surface->EvaluatePoint(SmPoint2d(point.x, point.y), lifted) == SM_SUCCESS, "short UV lift");
            const double height = 7 * (parameter - basis.GetMin()) / basis.GetLength();
            const SmPoint3d expected(3.0014 * std::cos(.2), 3.0014 * std::sin(.2), height);
            Require(lifted.DistanceBetween(expected) < 1e-10, "independent physical and actual-parameter oracle");
        }
        Require(bool(brep.m_bValidateOnly) == validateOnly, "unchanged enclosing short-retry permission");
        for (ULONG i = 0; i < vertices.GetSize(); ++i)
        {
            Require(vertices[i]->GetPoint().DistanceBetween(vertexPoints[i]) == 0, "short retry preserves vertex positions");
            if (validateOnly)
            {
                Require(double(SmTol::GetZoneTol3d(vertices[i])) == vertexTolerances[i], "short retry preserves vertex tolerance");
            }
        }
        if (validateOnly)
        {
            for (ULONG i = 0; i < edges.GetSize(); ++i)
            {
                Require(double(SmTol::GetZoneTol3d(edges[i])) == edgeTolerances[i], "short retry preserves edge tolerance");
            }
        }
        else
        {
            Require(double(SmTol::GetZoneTol3d(edge)) > originalTolerance, "retain permitted short-retry tolerance adjustment");
        }
    });
    Require(imported.Source()->Source().Bytes() == originalSource, "short-retry controls preserve immutable source");
}

void OriginalCylinderParametersRemainUnchanged(const step_import::ModelSnapshot& imported)
{
    auto copy = imported.CopyForModeling();
    Require(bool(copy.model), "original snapshot remains independently usable");
    copy.model->Edit([](SmBrep& brep) {
        SmTArray<SmFace*> faces;
        SmTArray<SmEdge*> edges;
        brep.GetFaces(faces);
        brep.GetEdges(edges);
        unsigned cylinders = 0, axialEdges = 0;
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            if (faces[i]->GetSurface()->IsKindOf(SmCylinder_TYPE))
            {
                ++cylinders;
                Require(static_cast<SmCylinder*>(faces[i]->GetSurface())->GetRadius() == 3,
                        "negative support stays confined to its modeling copy");
            }
        }
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            if (edges[i]->GetCurve()->IsKindOf(SmLine_TYPE))
            {
                ++axialEdges;
                Require(edges[i]->GetInterval() == SmExtent1d(0, 1) &&
                        edges[i]->GetCurve()->GetNaturalInterval() == SmExtent1d(0, 1),
                        "negative trim and shifted parameters do not change the original snapshot");
            }
        }
        Require(cylinders == 1 && axialEdges == 1, "original cylinder definitions remain complete");
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
        const bool shortRetryOnly = argc == 3 && std::string(argv[2]) == "--short-retry-only";
        Require(argc == 2 || shortRetryOnly, "split-rim fixture path and optional short-retry selection");
        auto options = step_import::Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = step_import::Quality::GeneralDevelopment();
        const auto imported = step_import::ImportFile(argv[1], options);
        Require(imported.status == step_import::Status::Accepted && imported.model, "complete source import");
        if (shortRetryOnly)
        {
            for (bool validateOnly : { true, false })
            {
                for (bool shiftedParameters : { false, true })
                {
                    ShortProjectionRetryContract(*imported.model, validateOnly, shiftedParameters);
                }
            }
            OriginalCylinderParametersRemainUnchanged(*imported.model);
            std::cout << "Short-interval UV retry policy contracts passed\n";
            return 0;
        }
        RebuildContract(*imported.model, false, false);
        RebuildContract(*imported.model, false, true);
        RebuildContract(*imported.model, true, false);
        RebuildContract(*imported.model, true, true);
        RebuildContract(*imported.model, false, false, false);
        RebuildContract(*imported.model, false, true, false);
        ProjectionRetryPreservesPolicy(*imported.model);
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
