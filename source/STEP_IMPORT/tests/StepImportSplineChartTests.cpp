// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSplineChart.h"
#include "StepImportSplineChartSession.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmSTEPSurface.h>
#include <SmTol.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <nurbs.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace step_import;
using namespace step_import::detail;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Good(SmStatus status)
{
    Require(status == SM_SUCCESS, "public synthetic native operation");
}
struct Ledger
{
    std::size_t work = 0, calls = 0;
    std::size_t workLimit = std::numeric_limits<std::size_t>::max();
    std::size_t cancelAt = std::numeric_limits<std::size_t>::max();
    SplineChartCallbacks Callbacks()
    {
        return { [&]() -> Result<void>
                 {
                     ++calls;
                     if (calls == cancelAt)
                     {
                         return tl::unexpected(Failure{ Status::Cancelled, "PublicChartCancelled", "public cancellation", {}, "public" });
                     }
                     return {};
                 },
                 [&](std::size_t n) -> Result<void>
                 {
                     if (n > workLimit - work)
                     {
                         return tl::unexpected(Failure{ Status::Indeterminate, "PublicChartWorkLimit", "public work limit", {}, "public" });
                     }
                     work += n;
                     return {};
                 } };
    }
};
std::unique_ptr<SmBSplineSurface> Surface(SmContext& context, double physicalU = 256, bool interiorKnot = false, bool clamped = true)
{
    // S(u,v)=(physicalU*u,v,0). The dyadic test normally uses a single
    // bilinear patch. The optional tiny interior knot is an independent
    // faithful-parameter check for the native affine reparameterizer.
    const double lo = interiorKnot ? -1 : -0x1p-8, hi = -lo;
    double u[] = { lo, lo, hi, hi };
    if (!clamped)
    {
        u[0] = 2 * lo;
        u[3] = 2 * hi;
    }
    double splitU[] = { -1, -1, 1e-300, 1, 1 };
    double v[] = { -1, -1, 1, 1 };
    gw_CPOINT row0[] = { { physicalU * lo, -1, 0, NL_NOW }, { physicalU * lo, 1, 0, NL_NOW } };
    gw_CPOINT row1[] = { { physicalU * hi, -1, 0, NL_NOW }, { physicalU * hi, 1, 0, NL_NOW } };
    gw_CPOINT middle[] = { { physicalU * 1e-300, -1, 0, NL_NOW }, { physicalU * 1e-300, 1, 0, NL_NOW } };
    gw_CPOINT* two[] = { row0, row1 };
    gw_CPOINT* three[] = { row0, middle, row1 };
    return std::unique_ptr<SmBSplineSurface>(new (
        context
    ) SmBSplineSurface(interiorKnot ? 2 : 1, 1, 1, 1, interiorKnot ? 4 : 3, 3, interiorKnot ? three : two, interiorKnot ? splitU : u, v));
}
std::unique_ptr<SmBSplineCurve> Curve(SmContext& context, double xScale = 0x1p-8, bool clamped = true)
{
    // t is unchanged: x=2*t-1, q(t)=(x/256,x*x). Hence physical
    // S(q(t))=(x,x*x,0), whose minimum radius is 1/2, not 1/131072.
    gw_CPOINT controls[] = { { -xScale, 1, 0, NL_NOW }, { 0, -1, 0, NL_NOW }, { xScale, 1, 0, NL_NOW } };
    double ordinary[] = { 0, 0, 0, 1, 1, 1 };
    double unclamped[] = { -2, -1, 0, 1, 2, 3 };
    return std::unique_ptr<SmBSplineCurve>(new (context) SmBSplineCurve(2, 2, 5, 2, controls, clamped ? ordinary : unclamped, &context));
}
using Bits = std::vector<std::uint64_t>;
void Append(Bits& bits, double value)
{
    bits.push_back(std::bit_cast<std::uint64_t>(value));
}
Bits Stored(const SmBSplineSurface& surface)
{
    ULONG nu = 0, nv = 0, ku = 0, kv = 0;
    double *p = nullptr, *u = nullptr, *v = nullptr;
    Good(surface.GetControlPointsPointer(nu, nv, p));
    Good(surface.GetKnotsPointers(ku, kv, u, v));
    Bits bits{ nu, nv, surface.GetDegree(SM_SP_U), surface.GetDegree(SM_SP_V) };
    for (std::size_t i = 0; i < std::size_t(nu) * nv * 4; ++i)
    {
        Append(bits, p[i]);
    }
    for (ULONG i = 0; i < ku; ++i)
    {
        Append(bits, u[i]);
    }
    for (ULONG i = 0; i < kv; ++i)
    {
        Append(bits, v[i]);
    }
    const auto domain = surface.GetNaturalUVDomain();
    for (double x : { domain.GetMin().x, domain.GetMin().y, domain.GetMax().x, domain.GetMax().y })
    {
        Append(bits, x);
    }
    return bits;
}
Bits Stored(const SmBSplineCurve& curve)
{
    ULONG count = 0;
    double* p = nullptr;
    Good(curve.GetControlPointsPointer(count, p));
    SmTArray<double> knots;
    Good(curve.GetKnotsAll(knots));
    Bits bits{ count, curve.GetDegree(), curve.GetDim() };
    for (std::size_t i = 0; i < std::size_t(count) * 4; ++i)
    {
        Append(bits, p[i]);
    }
    for (const auto x : knots)
    {
        Append(bits, x);
    }
    Append(bits, curve.GetNaturalInterval().GetMin());
    Append(bits, curve.GetNaturalInterval().GetMax());
    return bits;
}
bool Valid(const SmObject& object)
{
    SmAssertArray reports;
    return object.AssertValid(&reports, SM_LEVEL_2, SM_WALK) != FALSE && reports.GetSize() == 0;
}
bool Radius13(const SmCurve& curve)
{
    SmTArray<ULONG> selected;
    selected.Add(13);
    SmAssertArray reports;
    const bool valid = curve.AssertValid(&reports, SM_LEVEL_GIVEN, SM_NO_WALK, &selected) != FALSE;
    if (valid || reports.GetSize() != 1)
    {
        return false;
    }
    return reports[0]->GetOwner() == &curve && reports[0]->GetTestIndex() == 13;
}
SplineChartCandidate Condition(
    const SmSurface& surface,
    const SmCurve& uv,
    SmContext& context,
    Ledger& ledger,
    const std::optional<BinarySplineChart>& preset = std::nullopt
)
{
    auto candidate = TryConditionSplinePcurve(surface, uv, context, preset, ledger.Callbacks());
    if (!candidate)
    {
        throw std::runtime_error(candidate.error().key);
    }
    Require(candidate->has_value(), "public anisotropic chart receives a candidate");
    return std::move(**candidate);
}
void Positive()
{
    SmContext context;
    auto surface = Surface(context);
    auto uv = Curve(context);
    const auto sourceSurface = Stored(*surface), sourceUv = Stored(*uv);
    const double tolerance = SmTol::GetZoneTol3d(surface.get());
    const double curveTolerance = SmTol::GetZoneTol3d(uv.get());
    Require(Valid(*surface) && Radius13(*uv), "only the original chart has a small coordinate radius");
    Ledger ledger;
    auto candidate = Condition(*surface, *uv, context, ledger);
    Require(candidate.surface.get() != surface.get() && candidate.uv.get() != uv.get(), "fresh independently owned candidates");
    Require(candidate.chart.UExponent() == 8 && candidate.chart.VExponent() == 0, "one physical metric chooses an exact binary plan");
    Require(
        candidate.chart.MetricUv() == std::array<double, 2>{ 0, 0 } && candidate.chart.MetricDerivativeLengths() == std::array<double, 2>{ 256, 1 } &&
            candidate.chart.MetricRatio() == 256 && candidate.chart.MetricCosine() == 0,
        "metric location and physical derivatives remain explicit"
    );
    Require(candidate.chart.SurfaceControlCount() == 4 && candidate.witness->CurveControlCount() == 3, "witness complete net inventory");
    Require(Valid(*candidate.surface) && Valid(*candidate.uv), "full native candidate checks pass");
    Require(
        Stored(*surface) == sourceSurface && Stored(*uv) == sourceUv && !surface->GetOwner() && !uv->GetOwner(),
        "original bit identity and ownership"
    );
    Require(
        std::bit_cast<std::uint64_t>(double(SmTol::GetZoneTol3d(candidate.surface.get()))) == std::bit_cast<std::uint64_t>(tolerance) &&
            std::bit_cast<std::uint64_t>(double(SmTol::GetZoneTol3d(surface.get()))) == std::bit_cast<std::uint64_t>(tolerance) &&
            std::bit_cast<std::uint64_t>(double(SmTol::GetZoneTol3d(candidate.uv.get()))) == std::bit_cast<std::uint64_t>(curveTolerance) &&
            std::bit_cast<std::uint64_t>(double(SmTol::GetZoneTol3d(uv.get()))) == std::bit_cast<std::uint64_t>(curveTolerance),
        "surface and UV effective working zones retain exact bits after copy, coordinate change and full native checks"
    );
    auto& targetUv = static_cast<SmBSplineCurve&>(*candidate.uv);
    SmTArray<double> originalT, targetT;
    Good(uv->GetKnotsAll(originalT));
    Good(targetUv.GetKnotsAll(targetT));
    Require(originalT.GetSize() == targetT.GetSize(), "same t knot inventory");
    for (ULONG i = 0; i < originalT.GetSize(); ++i)
    {
        Require(std::bit_cast<std::uint64_t>(originalT[i]) == std::bit_cast<std::uint64_t>(targetT[i]), "same t knot bits");
    }
    // These actual-t evaluations are independent regression observations, not
    // the equivalence proof (the immutable complete defining nets supply it).
    for (double t : { 0., .125, .5, .75, 1. })
    {
        SmVector3d q[3], r[3];
        Good(uv->Evaluate(t, 2, TRUE, q, FALSE));
        Good(targetUv.Evaluate(t, 2, TRUE, r, FALSE));
        for (unsigned d = 0; d < 3; ++d)
        {
            Require(r[d].x == std::ldexp(q[d].x, 8) && r[d].y == q[d].y, "same-t parameter derivatives follow the selected exact chart");
        }
        SmPoint3d a, b;
        Good(surface->EvaluatePoint({ q[0].x, q[0].y }, a));
        Good(candidate.surface->EvaluatePoint({ r[0].x, r[0].y }, b));
        const double x = 2 * t - 1;
        Require(a.DistanceBetween(b) < 1e-12 && b.DistanceBetween(SmPoint3d(x, x * x, 0)) < 1e-12, "analytic physical curve unchanged");
    }
    const auto targetSurfaceBits = Stored(static_cast<const SmBSplineSurface&>(*candidate.surface)), targetUvBits = Stored(targetUv);
    Ledger verification;
    Require(bool(candidate.witness->Verify(*candidate.surface, candidate.uv.get(), verification.Callbacks())), "complete target witness");
    auto faceSurface = ApplySplineSurfaceChart(*surface, candidate.chart, context, verification.Callbacks());
    Require(bool(faceSurface), "same basis plan independently creates target face geometry");
    Require(
        Stored(static_cast<const SmBSplineSurface&>(**faceSurface)) == targetSurfaceBits &&
            candidate.witness->Verify(**faceSurface, nullptr, verification.Callbacks()),
        "surface-only witness without carrier UV"
    );
    Require(
        std::bit_cast<std::uint64_t>(double(SmTol::GetZoneTol3d(faceSurface->get()))) == std::bit_cast<std::uint64_t>(tolerance),
        "target-face geometry copy preserves the original effective working zone"
    );
    Ledger reused;
    auto second = Condition(*surface, *uv, context, reused, candidate.chart);
    Require(
        second.chart.MetricUv() == candidate.chart.MetricUv() && Stored(static_cast<const SmBSplineSurface&>(*second.surface)) == targetSurfaceBits &&
            Stored(static_cast<const SmBSplineCurve&>(*second.uv)) == targetUvBits,
        "preset reuses exactly one plan"
    );

    auto witness = candidate.witness;
    const auto domainValues = candidate.chart.TargetDomain();
    const SmExtent2d domain(domainValues[0], domainValues[1], domainValues[2], domainValues[3]);
    SmCrvOnSurf owned(*candidate.uv, *candidate.surface, &domain, 3, &context);
    candidate.uv.release();
    candidate.surface.release();
    Require(
        owned.GetUVCurve()->GetOwner() == &owned && owned.GetBaseSurface()->GetOwner() == &owned && Valid(owned),
        "real owned composition has no native findings"
    );
    Require(bool(witness->Verify(*owned.GetBaseSurface(), owned.GetUVCurve(), verification.Callbacks())), "witness permits actual final ownership");
    auto rejectedOwned = TryConditionSplinePcurve(*owned.GetBaseSurface(), *owned.GetUVCurve(), context, std::nullopt, verification.Callbacks());
    Require(rejectedOwned && !*rejectedOwned, "construction entrypoint refuses owned definitions");
    uv.reset();
    surface.reset();
    Require(
        bool(witness->Verify(*owned.GetBaseSurface(), owned.GetUVCurve(), verification.Callbacks())),
        "witness owns its evidence after original release"
    );
}
void SessionOwnership()
{
    SmContext context;
    auto surface = Surface(context);
    auto uv = Curve(context);
    Ledger ledger;
    auto candidate = Condition(*surface, *uv, context, ledger);
    auto wrapper = std::unique_ptr<SmSTEPSurface>(new (context) SmSTEPSurface(*candidate.surface, TRUE));
    const auto* base = candidate.surface.release();
    const auto* parameter = candidate.uv.get();
    const auto domainValues = candidate.chart.TargetDomain();
    const SmExtent2d domain(domainValues[0], domainValues[1], domainValues[2], domainValues[3]);
    auto carrier = std::unique_ptr<SmCrvOnSurf>(new (context) SmCrvOnSurf(*candidate.uv, *wrapper, &domain, 3, &context));
    candidate.uv.release();
    wrapper.release();
    SmBrepConstructor constructor;
    auto brep = std::unique_ptr<SmBrep>(constructor.StartBrep(context));
    Require(bool(brep), "public topology owner");
    SmEdge* edge = nullptr;
    auto* owned = carrier.release();
    Good(brep->CreateWireEdgeFromCurve(owned, SmExtent1d(0, 1), edge));
    Require(edge && edge->GetCurve() == owned, "wire constructor owns the defining carrier");
    SplineChartSession session;
    session.bindings.push_back({ 0, candidate.witness, base, parameter, owned, nullptr, {}, 0 });
    const auto callbacks = [&](std::size_t)
    {
        return ledger.Callbacks();
    };
    Require(bool(VerifySplineChartBindings(session, context, callbacks)), "actual owning carrier session verifies");
    // Fresh public synthetic graph only. A backlink alone does not imply
    // destruction ownership; restore the flag before any exception unwinds.
    struct Restore
    {
        SmCrvOnSurf* carrier;
        ~Restore()
        {
            carrier->SetOwnerFlag(3);
        }
    } restore{ owned };
    owned->SetOwnerFlag(0);
    const auto rejected = VerifySplineChartBindings(session, context, callbacks);
    Require(!rejected && rejected.error().key == "SplineChartOwnershipChanged", "stale owner backlinks cannot substitute for actual ownership");
}
void NegativeScopes()
{
    SmContext context;
    auto surface = Surface(context);
    auto uv = Curve(context);
    const auto source = Stored(*surface), parameter = Stored(*uv);
    Ledger ledger;
    auto candidate = Condition(*surface, *uv, context, ledger);
    auto changed = Surface(context, 512);
    auto mismatch = TryConditionSplinePcurve(*changed, *uv, context, candidate.chart, ledger.Callbacks());
    Require(mismatch && !*mismatch, "preset refuses a different complete basis net");
    auto wrongFace = ApplySplineSurfaceChart(*changed, candidate.chart, context, ledger.Callbacks());
    Require(!wrongFace && wrongFace.error().key == "SplineChartSourceSurfaceMismatch", "target-face apply requires bound original net");
    auto unclamped = Curve(context, 0x1p-8, false);
    auto noClamp = TryConditionSplinePcurve(*surface, *unclamped, context, std::nullopt, ledger.Callbacks());
    Require(noClamp && !*noClamp, "unclamped source is outside the finite proof scope");
    auto unclampedSurface = Surface(context, 256, false, false);
    const auto unclampedBits = Stored(*unclampedSurface);
    auto noSurfaceClamp = TryConditionSplinePcurve(*unclampedSurface, *uv, context, std::nullopt, ledger.Callbacks());
    Require(noSurfaceClamp && !*noSurfaceClamp && Stored(*unclampedSurface) == unclampedBits, "unclamped surface is not normalized implicitly");
    SmBSplineCurve borrowed(uv->GetGwNurbPointer(), 2, TRUE, &context);
    auto noBorrow = TryConditionSplinePcurve(*surface, borrowed, context, std::nullopt, ledger.Callbacks());
    Require(noBorrow && !*noBorrow, "underlying borrowed NURBS are never edited/copied as proof inputs");
    SmBSplineSurface borrowedSurface(surface->GetGwNurbPointer(), TRUE, &context);
    auto noBorrowedSurface = TryConditionSplinePcurve(borrowedSurface, *uv, context, std::nullopt, ledger.Callbacks());
    Require(noBorrowedSurface && !*noBorrowedSurface, "borrowed surface basis cannot acquire an owning chart witness");
    SmContext other;
    auto wrongContext = TryConditionSplinePcurve(*surface, *uv, other, std::nullopt, ledger.Callbacks());
    Require(wrongContext && !*wrongContext, "actual contexts must match");
    auto balanced = Surface(context, 1);
    Require(Radius13(*uv), "balanced physical small-radius control retains its true finding");
    auto physical = TryConditionSplinePcurve(*balanced, *uv, context, std::nullopt, ledger.Callbacks());
    Require(physical && !*physical, "balanced physical metric offers no coordinate conditioning for a real small radius");
    auto ordinary = Curve(context, 1);
    auto noNeed = TryConditionSplinePcurve(*surface, *ordinary, context, candidate.chart, ledger.Callbacks());
    Require(noNeed && !*noNeed, "normal pcurve is unchanged even when a plan exists");
    auto& target = static_cast<SmBSplineCurve&>(*candidate.uv);
    Good(target.SetControlPoint(SM_CP_NON_RATIONAL, 1, SmPoint3d(.25, -1, 0), 1));
    auto badWitness = candidate.witness->Verify(*candidate.surface, &target, ledger.Callbacks());
    Require(!badWitness && badWitness.error().key == "SplineChartCurveWitnessMismatch", "changed actual target net invalidates witness");
    Require(Stored(*surface) == source && Stored(*uv) == parameter, "all negative controls preserve original storage");

    // This valid piecewise-linear surface contains a nonzero interior U knot
    // extremely near zero. N_BasisReparam uses lo+factor*(k-lo), which loses it
    // for [-1,1] -> [-256,256]. All scalar ldexp operations are reversible;
    // the actual native operation is nevertheless NOT faithful, so refuse it.
    auto cancellationNet = Surface(context, 256, true);
    const auto oldNet = Stored(*cancellationNet);
    Require(Valid(*cancellationNet) && Radius13(*uv), "the knot cancellation control starts with a valid native surface and a radius-only pcurve");
    auto inexact = TryConditionSplinePcurve(*cancellationNet, *uv, context, std::nullopt, ledger.Callbacks());
    Require(!inexact && inexact.error().key == "SplineChartSurfaceTargetMismatch", "native knot cancellation cannot obtain an equivalence witness");
    Require(Stored(*cancellationNet) == oldNet && Stored(*uv) == parameter, "inexact native reparameterization never edits its original");
}
void BudgetsAndCancellation()
{
    SmContext context;
    auto surface = Surface(context);
    auto uv = Curve(context);
    const auto source = Stored(*surface), parameter = Stored(*uv);
    Ledger measured;
    auto candidate = Condition(*surface, *uv, context, measured);
    Require(measured.work > 1 && measured.calls > 1, "actual API/storage work is charged");
    Ledger exact;
    exact.workLimit = measured.work;
    auto sufficient = TryConditionSplinePcurve(*surface, *uv, context, std::nullopt, exact.Callbacks());
    Require(sufficient && sufficient->has_value() && exact.work == measured.work, "exact N work budget succeeds");
    Ledger shortBudget;
    shortBudget.workLimit = measured.work - 1;
    auto insufficient = TryConditionSplinePcurve(*surface, *uv, context, std::nullopt, shortBudget.Callbacks());
    Require(!insufficient && insufficient.error().key == "PublicChartWorkLimit", "N-1 resource failure preserves its identity");
    for (std::size_t stop : { std::size_t(1), measured.calls - 1, measured.calls })
    {
        Ledger cancelled;
        cancelled.cancelAt = stop;
        auto result = TryConditionSplinePcurve(*surface, *uv, context, std::nullopt, cancelled.Callbacks());
        Require(
            !result && result.error().status == Status::Cancelled && result.error().key == "PublicChartCancelled" && result.error().stage == "public",
            "early/late cancellation propagates without a candidate"
        );
        Require(
            Stored(*surface) == source && Stored(*uv) == parameter && !surface->GetOwner() && !uv->GetOwner(),
            "cancelled candidates leave originals unchanged"
        );
    }
    Ledger cancelledVerify;
    cancelledVerify.cancelAt = 1;
    auto stopped = candidate.witness->Verify(*candidate.surface, candidate.uv.get(), cancelledVerify.Callbacks());
    Require(!stopped && stopped.error().status == Status::Cancelled, "final actual-storage verification remains cancellable");
}
void WorkingToleranceBinding()
{
    // Both contexts are independently constructed with explicit PUBLIC fixture
    // model-size estimates. No tolerance setter is used. Compare EFFECTIVE
    // object zones: in the current old-tolerance SDK ownerless geometry uses
    // the system zone, even when context model-size estimates differ.
    SmContext small(1.), large(1024.);
    auto surface = Surface(small);
    auto uv = Curve(small);
    const double originalSurfaceZone = SmTol::GetZoneTol3d(surface.get()), originalCurveZone = SmTol::GetZoneTol3d(uv.get());
    Ledger first;
    auto candidate = Condition(*surface, *uv, small, first);
    auto otherSurface = Surface(large);
    auto otherUv = Curve(large);
    const auto surfaceBits = Stored(*otherSurface), uvBits = Stored(*otherUv);
    const double otherSurfaceZone = SmTol::GetZoneTol3d(otherSurface.get()), otherCurveZone = SmTol::GetZoneTol3d(otherUv.get());
    Require(double(SmTol::GetZoneTol3d(&small)) != double(SmTol::GetZoneTol3d(&large)), "public context model-size estimates differ");
#ifdef SM_USE_OLDTOL
    Require(
        originalSurfaceZone == otherSurfaceZone && originalCurveZone == otherCurveZone,
        "old-tolerance ownerless object zones must not be replaced with context estimates"
    );
#endif
    Ledger reused;
    auto badApply = ApplySplineSurfaceChart(*otherSurface, candidate.chart, large, reused.Callbacks());
    auto badPreset = TryConditionSplinePcurve(*otherSurface, *otherUv, large, candidate.chart, reused.Callbacks());
    if (originalSurfaceZone != otherSurfaceZone)
    {
        Require(!badApply && badApply.error().key == "SplineChartWorkingToleranceChanged", "apply refuses a different effective working zone");
        Require(
            !badPreset && badPreset.error().key == "SplineChartWorkingToleranceChanged",
            "preset cannot silently rebind numerical working evidence"
        );
    }
    else
    {
        Require(badApply && badPreset && badPreset->has_value(), "equal actual zones are not confused with unequal context estimates");
    }
    Ledger independent;
    auto otherCandidate = Condition(*otherSurface, *otherUv, large, independent);
    Require(Stored(*otherSurface) == surfaceBits && Stored(*otherUv) == uvBits, "working-zone rejection leaves both source definitions intact");
    Require(
        double(SmTol::GetZoneTol3d(surface.get())) == originalSurfaceZone && double(SmTol::GetZoneTol3d(uv.get())) == originalCurveZone &&
            double(SmTol::GetZoneTol3d(otherSurface.get())) == otherSurfaceZone && double(SmTol::GetZoneTol3d(otherUv.get())) == otherCurveZone &&
            double(SmTol::GetZoneTol3d(otherCandidate.surface.get())) == otherSurfaceZone &&
            double(SmTol::GetZoneTol3d(otherCandidate.uv.get())) == otherCurveZone,
        "independent conditioning never transfers or changes either context's physical working zone"
    );
}
} // namespace
int main()
{
    try
    {
        Positive();
        SessionOwnership();
        NegativeScopes();
        BudgetsAndCancellation();
        WorkingToleranceBinding();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
