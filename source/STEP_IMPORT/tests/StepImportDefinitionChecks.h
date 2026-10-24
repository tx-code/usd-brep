// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmCrvOnSurf.h>
#include <SmSTEPSurface.h>
#include <SmBrep.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <cmath>
#include <stdexcept>
#include <string>
namespace step_import_test
{
inline void Require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
inline void Near(double value, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(value) && std::abs(value - expected) <= tolerance, message);
}
inline void CheckNativeWorkBudget(SmBrep& native, double maximumMm)
{
    // Inspect effective native work zones, not just the selected report label.
    // Root/face/edge/vertex tolerances govern the imported geometry consumers.
    const auto check = [maximumMm](const SmObject* object)
    {
        const double tolerance = SmTol::GetZoneTol3d(object).val;
        Require(std::isfinite(tolerance) && tolerance > 0 && tolerance <= maximumMm, "actual native work zone within explicit maximum");
    };
    check(&native);
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    native.GetFaces(faces);
    native.GetEdges(edges);
    native.GetVertices(vertices);
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        check(faces[i]);
    }
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        check(edges[i]);
    }
    for (ULONG i = 0; i < vertices.GetSize(); ++i)
    {
        check(vertices[i]);
    }
}
inline void CompareCurveCoefficients(const SmCurve& a, const SmCurve& b)
{
    // The stored-definition reader never promotes 3D or UV curves. Keep the
    // class gate in addition to all coefficient and parameter comparisons.
    const bool sameClass = a.GetType() == b.GetType();
    if (!sameClass || a.GetDegree() != b.GetDegree())
    {
        throw std::runtime_error(
            "archive curve class/degree/dim " + std::to_string(a.GetType()) + "/" + std::to_string(a.GetDegree()) + "/" + std::to_string(a.GetDim()) +
            " -> " + std::to_string(b.GetType()) + "/" + std::to_string(b.GetDegree()) + "/" + std::to_string(b.GetDim())
        );
    }
    if (const auto first = dynamic_cast<const SmCrvOnSurf*>(&a))
    {
        const auto second = dynamic_cast<const SmCrvOnSurf*>(&b);
        Require(second && first->GetUVCurve() && second->GetUVCurve(), "archive exact composition class and parameter child");
        CompareCurveCoefficients(*first->GetUVCurve(), *second->GetUVCurve());
        const auto surfaceA = first->GetBaseSurface(), surfaceB = second->GetBaseSurface();
        Require(
            surfaceA && surfaceB && dynamic_cast<const SmSTEPSurface*>(surfaceA) && dynamic_cast<const SmSTEPSurface*>(surfaceB) &&
                surfaceA->GetType() == surfaceB->GetType() && *surfaceA == *surfaceB &&
                surfaceA->GetNaturalUVDomain() == surfaceB->GetNaturalUVDomain() && surfaceA->GetSTEPUVDomain() == surfaceB->GetSTEPUVDomain(),
            "archive STEP composition retains its wrapped canonical definition and parameter domains"
        );
        return;
    }
    const auto first = dynamic_cast<const SmBSplineCurve*>(&a), second = dynamic_cast<const SmBSplineCurve*>(&b);
    Require(first && second, "admitted exact native curve net");
    SmTArray<SmPoint3d> ca, cb;
    SmTArray<double> wa, wb, ka, kb;
    Require(first->GetControlPolygon(ca, wa) == SM_SUCCESS && second->GetControlPolygon(cb, wb) == SM_SUCCESS, "archive curve coefficients");
    Require(first->GetKnotsAll(ka) == SM_SUCCESS && second->GetKnotsAll(kb) == SM_SUCCESS, "archive curve knots");
    Require(ca.GetSize() == cb.GetSize() && wa.GetSize() == wb.GetSize() && ka.GetSize() == kb.GetSize(), "archive curve net shape");
    for (ULONG i = 0; i < ca.GetSize(); ++i)
    {
        Near(ca[i].DistanceBetween(cb[i]), 0, 1e-9, "archive defining curve control point");
    }
    for (ULONG i = 0; i < wa.GetSize(); ++i)
    {
        Near(wa[i], wb[i], 1e-12, "archive curve weight");
    }
    for (ULONG i = 0; i < ka.GetSize(); ++i)
    {
        Require(ka[i] == kb[i], "archive curve knot identity");
    }
}
struct ArchiveClassChanges
{
    std::size_t extrusionSurfaces = 0, planeSurfaces = 0, linearUv = 0;
};
inline ArchiveClassChanges CompareNativeDefinitions(SmBrep& a, SmBrep& b)
{
    // Numerical integration depends on transient caches and is not an identity
    // test. Compare the actual exact native definitions as well: classes,
    // degrees, all control coefficients/weights/knots and installed UV nets.
    SmBrepCompareResult topology;
    ArchiveClassChanges changes;
    topology.dVertexTolerance = 1e-9;
    Require(a.CompareBreps(&b, topology) == SM_SUCCESS, "archive topology comparison call");
    if (topology.lFaceCountA != topology.lFaceCountB || topology.lEdgeCountA != topology.lEdgeCountB ||
        topology.lVertexCountA != topology.lVertexCountB || topology.lCurveTypeMismatches != 0 || topology.dMaxVertexGap > 1e-9)
    {
        throw std::runtime_error(
            "archive topology/classes/vertices: surface=" + std::to_string(topology.lSurfaceTypeMismatches) +
            " curve=" + std::to_string(topology.lCurveTypeMismatches) + " vertex=" + std::to_string(topology.dMaxVertexGap)
        );
    }
    SmTArray<SmEdge*> ea, eb;
    a.GetEdges(ea);
    b.GetEdges(eb);
    for (ULONG i = 0; i < ea.GetSize(); ++i)
    {
        Require(
            ea[i]->GetInterval().GetMin() == eb[i]->GetInterval().GetMin() && ea[i]->GetInterval().GetMax() == eb[i]->GetInterval().GetMax(),
            "archive edge interval"
        );
        CompareCurveCoefficients(*ea[i]->GetCurve(), *eb[i]->GetCurve());
    }
    SmTArray<SmFace*> fa, fb;
    a.GetFaces(fa);
    b.GetFaces(fb);
    for (ULONG i = 0; i < fa.GetSize(); ++i)
    {
        const auto first = dynamic_cast<SmBSplineSurface*>(fa[i]->GetSurface());
        const auto second = dynamic_cast<SmBSplineSurface*>(fb[i]->GetSurface());
        Require(first && second && first->GetType() == second->GetType(), "unqualified archive surface reclassification");
        Require(first && second, "admitted exact native surface net");
        Require(
            first->GetDegree(SM_SP_U) == second->GetDegree(SM_SP_U) && first->GetDegree(SM_SP_V) == second->GetDegree(SM_SP_V),
            "archive surface degrees"
        );
        ULONG au = 0, av = 0, bu = 0, bv = 0;
        double *ca = nullptr, *cb = nullptr;
        Require(
            first->GetControlPointsPointer(au, av, ca) == SM_SUCCESS && second->GetControlPointsPointer(bu, bv, cb) == SM_SUCCESS && au == bu &&
                av == bv,
            "archive surface net shape"
        );
        for (ULONG k = 0; k < au * av * 4; ++k)
        {
            if (ca[k] != cb[k])
            {
                Near(ca[k], cb[k], k % 4 == 3 ? 1e-12 : 1e-9, "archive surface coefficient");
            }
        }
        double *ka = nullptr, *la = nullptr, *kb = nullptr, *lb = nullptr;
        Require(
            first->GetKnotsPointers(au, av, ka, la) == SM_SUCCESS && second->GetKnotsPointers(bu, bv, kb, lb) == SM_SUCCESS && au == bu && av == bv,
            "archive surface knots shape"
        );
        for (ULONG k = 0; k < au; ++k)
        {
            Require(ka[k] == kb[k], "archive surface U knot identity");
        }
        for (ULONG k = 0; k < av; ++k)
        {
            Require(la[k] == lb[k], "archive surface V knot identity");
        }
        SmTArray<SmEdgeuse*> ua, ub;
        fa[i]->GetUpwardEdgeuses(ua);
        fb[i]->GetUpwardEdgeuses(ub);
        Require(ua.GetSize() == ub.GetSize(), "archive UV inventory");
        for (ULONG k = 0; k < ua.GetSize(); ++k)
        {
            Require(ua[k]->GetUVTrimCurve() && ub[k]->GetUVTrimCurve(), "archive UV present");
            CompareCurveCoefficients(*ua[k]->GetUVTrimCurve(), *ub[k]->GetUVTrimCurve());
            changes.linearUv += ua[k]->GetUVTrimCurve()->GetType() != ub[k]->GetUVTrimCurve()->GetType();
        }
    }
    return changes;
}
} // namespace step_import_test
