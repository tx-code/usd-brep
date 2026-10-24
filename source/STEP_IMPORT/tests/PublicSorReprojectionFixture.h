// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Public synthetic input only. No CAD-library data or fitted defining geometry.
#pragma once
#include <SmSurface.h> // SmCrvOnSurf requires the complete base type.

#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmBSplineCurve.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmSTEPSurface.h>
#include <SmSurfOfRevolution.h>
#include <SmVertex.h>
#include <nurbs.h>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace public_sor_repro
{
inline void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
inline void RequireStatus(SmStatus status)
{
    Require(status == SM_SUCCESS, "Public SOR fixture native construction failed");
}
struct Fixture
{
    // The caller's context must outlive this fixture. Face and edge borrow from brep.
    std::unique_ptr<SmBrep> brep;
    SmFace* face = nullptr;
    SmEdge* edge = nullptr;
};

inline std::unique_ptr<SmCrvOnSurf> SourceEdge(
    SmContext& context,
    const SmSurface& surface,
    const SmPoint2d& a,
    const SmPoint2d& b,
    bool nonlinear,
    const SmExtent1d& interval
)
{
    SmSurface* copied = nullptr;
    const auto status = surface.Copy(context, copied);
    std::unique_ptr<SmSurface> base(copied);
    RequireStatus(status);
    Require(copied != nullptr, "Public SOR independent source surface copy");
    auto wrapped = std::unique_ptr<SmSurface>(new (context) SmSTEPSurface(*base, TRUE));
    base.release();
    wrapped->SetContext(&context);
    const double weight = nonlinear ? 2 : 1;
    const double midU = std::lerp(a.x, b.x, nonlinear ? .92 : .5);
    const double midV = std::lerp(a.y, b.y, nonlinear ? .85 : .5);
    // Raw homogeneous coordinates. Source U is in STEP degrees, V retains the
    // generator parameter [-2,8]; interval below belongs to the edge's own t.
    gw_CPOINT points[] = { { a.x, a.y, 0, 1 }, { midU * weight, midV * weight, 0, weight }, { b.x, b.y, 0, 1 } };
    const double begin = interval.GetMin(), end = interval.GetMax();
    double knots[] = { begin, begin, begin, end, end, end };
    auto uv = std::unique_ptr<SmCurve>(new (context) SmBSplineCurve(2, 2, 5, 2, points, knots, &context));
    // ownerFlag=3 gives the exact composition independent source UV + surface.
    auto curve = std::unique_ptr<SmCrvOnSurf>(new (context) SmCrvOnSurf(*uv, *wrapped, nullptr, 3, &context));
    uv.release();
    wrapped.release();
    return curve;
}

inline Fixture Create(SmContext& context, bool shiftedParameters = true, bool modeledWindow = true)
{
    gw_CPOINT points[] = { { 0, 3, 0, 1 }, { 0, 8, 3, 2 }, { 0, 3, 7, 1 } };
    double knots[] = { -2, -2, -2, 8, 8, 8 };
    auto generator = std::unique_ptr<SmBSplineCurve>(new (context) SmBSplineCurve(3, 2, 5, 2, points, knots, &context));
    SmSurfOfRevolution* raw = nullptr;
    const auto status = SmSurfOfRevolution::CreateCanonical(context, generator.get(), SmPoint3d(0, 0, 0), SmVector3d(0, 0, 1), raw, FALSE);
    auto surface = std::unique_ptr<SmSurfOfRevolution>(raw);
    // Native construction can adopt the generator before a later failure.
    if (surface && generator->GetOwner() == surface.get())
    {
        generator.release();
    }
    RequireStatus(status);
    Require(bool(surface), "Public SOR support");
    SmBrepConstructor constructor;
    Fixture result;
    result.brep.reset(constructor.StartBrep(context));
    Require(bool(result.brep), "Public SOR BRep");
    result.brep->SetTolerance(.001, FALSE);
    result.brep->m_bValidateOnly = TRUE;
    const std::array<SmPoint2d, 4> corners = { SmPoint2d(130, 6), SmPoint2d(40, 0), SmPoint2d(40, -1), SmPoint2d(140, -1) };
    std::array<SmVertex*, 4> vertices{};
    for (unsigned i = 0; i < vertices.size(); ++i)
    {
        SmPoint3d point;
        RequireStatus(surface->EvaluateSTEPPoint(corners[i], point));
        vertices[i] = constructor.StartVertex(point);
        Require(vertices[i] != nullptr, "Public SOR vertex");
        vertices[i]->SetTolerance(.001, FALSE);
        RequireStatus(constructor.EndVertex());
    }
    SmShell* outside = nullptr;
    SmShell* inside = nullptr;
    RequireStatus(constructor.StartShell(outside, inside));
    Require(outside != nullptr && inside == nullptr, "Public SOR open shell role");
    result.face = constructor.StartFace(SM_OT_SAME);
    Require(result.face != nullptr, "Public SOR face");
    result.face->SetTolerance(.001, FALSE);
    SmExtent2d domain = surface->GetNaturalUVDomain();
    if (modeledWindow)
    {
        RequireStatus(surface->ConvertDomainFromSTEPToNURBS(SmExtent2d(40, -1, 140, 6), domain));
    }
    auto* actual = surface.get();
    RequireStatus(constructor.SetFaceSurface(actual, domain));
    Require(surface->GetOwner() == result.face, "Public SOR face ownership");
    surface.release();
    Require(constructor.StartLoop(SM_OT_SAME) != nullptr, "Public SOR loop");
    const SmExtent1d interval = shiftedParameters ? SmExtent1d(-3, 9) : SmExtent1d(0, 1);
    for (unsigned i = 0; i < vertices.size(); ++i)
    {
        auto* edge = constructor.StartEdge(SM_OT_SAME, nullptr, vertices[i], vertices[(i + 1) % 4]);
        Require(edge != nullptr, "Public SOR edge");
        edge->SetTolerance(.001, FALSE);
        auto curve = SourceEdge(context, *actual, corners[i], corners[(i + 1) % 4], i == 0, interval);
        RequireStatus(constructor.SetEdgeCurve(curve.get(), FALSE));
        Require(curve->GetOwner() == edge, "Public SOR curve ownership");
        curve.release();
        RequireStatus(constructor.SetLimits(SmVertexNode(interval.GetMin()), SmVertexNode(interval.GetMax())));
        RequireStatus(constructor.EndEdge());
        if (i == 0)
        {
            result.edge = edge;
        }
    }
    RequireStatus(constructor.EndLoop());
    RequireStatus(constructor.EndFace());
    RequireStatus(constructor.EndShell(FALSE));
    RequireStatus(constructor.EndBrep());
    return result;
}
} // namespace public_sor_repro
