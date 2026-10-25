// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveLineLift.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmContext.h>
#include <cfenv>
#include <iostream>
#include <limits>
#include <nurbs.h>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
struct Ledger
{
    std::size_t work = 0, calls = 0, limit = std::numeric_limits<std::size_t>::max(), cancelAt = std::numeric_limits<std::size_t>::max();
    SplineChartCallbacks Callbacks()
    {
        return { [&]() -> Result<void>
                 {
                     if (++calls == cancelAt)
                     {
                         return tl::unexpected(Failure{ Status::Cancelled, "PublicLineCancel", "public", 23, "public" });
                     }
                     return {};
                 },
                 [&](std::size_t n) -> Result<void>
                 {
                     if (n > limit - work)
                     {
                         return tl::unexpected(Failure{ Status::Indeterminate, "PublicLineWork", "public", 29, "public" });
                     }
                     work += n;
                     return {};
                 } };
    }
};
std::unique_ptr<SmBSplineSurface> Surface(SmContext& context, double xScale = 2, bool clamped = true, bool rational = false)
{
    // Polynomial oracle: S(u,v)=(xScale*u,3*v,0), u in [-1,2],
    // v in [-1,3]. Optional positive rational control has the independent
    // expression x=xScale*(-1+5*s)/(1+s), y=3*v, s=(u+1)/3.
    double u[] = { clamped ? -1. : -2., -1, 2, 2 }, v[] = { -1, -1, 3, 3 };
    const double w = rational ? 2. : NL_NOW;
    const double factor = rational ? 2. : 1.;
    const double firstWeight = rational ? 1. : NL_NOW;
    gw_CPOINT row0[] = { { -xScale, -3, 0, firstWeight }, { -xScale, 9, 0, firstWeight } };
    gw_CPOINT row1[] = { { 2 * xScale * factor, -3 * factor, 0, w }, { 2 * xScale * factor, 9 * factor, 0, w } };
    gw_CPOINT* rows[] = { row0, row1 };
    return std::unique_ptr<SmBSplineSurface>(new (context) SmBSplineSurface(1, 1, 1, 1, 3, 3, rows, u, v));
}
using Ends = std::array<std::array<double, 2>, 2>;
std::unique_ptr<SmBSplineCurve> Uv(SmContext& context, const Ends& ends)
{
    gw_CPOINT points[] = { { ends[0][0], ends[0][1], 0, NL_NOW }, { ends[1][0], ends[1][1], 0, NL_NOW } };
    double knots[] = { 0, 0, 1, 1 };
    return std::unique_ptr<SmBSplineCurve>(new (context) SmBSplineCurve(2, 1, 3, 1, points, knots, &context));
}
void Test()
{
    SmContext context;
    const Ends ends{ std::array{ -1., -1. }, std::array{ 2., 3. } };
    auto surface = Surface(context);
    auto uv = Uv(context, ends);
    Ledger exact;
    auto zero = ProvePcurveLineLift(*surface, *uv, ends, { 0, 0 }, 0, exact.Callbacks());
    if (!zero)
    {
        std::cerr << zero.error().key << '\n';
    }
    Require(bool(zero) && (*zero)->MaximumMm() == 0 && !(*zero)->DerivativeBoundsMmPerUv(), "exact interpolation needs no derivative bound");
    Ledger round;
    auto proof = ProvePcurveLineLift(*surface, *uv, ends, { .125, .0625 }, .438, round.Callbacks());
    Require(bool(proof), "polynomial whole-domain lift bound");
    // Independent norm bound is 2/8 + 3/16 = 7/16 mm. No native
    // evaluator or imported report supplies this truth.
    Require((*proof)->MaximumMm() >= .4375 && (*proof)->MaximumMm() < .43750001, "analytic lift bound enclosure");
    const auto derivative = (*proof)->DerivativeBoundsMmPerUv();
    Require(derivative && (*derivative)[0] >= 2 && (*derivative)[1] >= 3, "physical derivative bounds");
    Ledger verify;
    Require(bool((*proof)->Verify(*surface, *uv, verify.Callbacks())), "owning complete-net witness");
    Ledger below;
    auto refuse = ProvePcurveLineLift(*surface, *uv, ends, { .125, .0625 }, .43, below.Callbacks());
    Require(!refuse && refuse.error().key == "PcurveLineDefinitionErrorUnestablished", "no physical-budget enlargement");
    auto huge = Surface(context, 1e12);
    Ledger amplified;
    auto tooLarge = ProvePcurveLineLift(*huge, *uv, ends, { .125, .0625 }, 1, amplified.Callbacks());
    Require(!tooLarge && tooLarge.error().key == "PcurveLineDefinitionErrorUnestablished", "UV errors must be lifted to physical millimetres");
    auto weighted = Surface(context, 2, true, true);
    Ledger rational;
    auto rationalProof = ProvePcurveLineLift(*weighted, *uv, ends, { .125, .0625 }, 100, rational.Callbacks());
    Require(bool(rationalProof) && (*rationalProof)->MaximumMm() >= .4375, "positive rational denominator handled conservatively");
    Ledger limit;
    limit.limit = round.work;
    Require(bool(ProvePcurveLineLift(*surface, *uv, ends, { .125, .0625 }, .438, limit.Callbacks())), "exact N work budget");
    Ledger shortLimit;
    shortLimit.limit = round.work - 1;
    auto shortProof = ProvePcurveLineLift(*surface, *uv, ends, { .125, .0625 }, .438, shortLimit.Callbacks());
    Require(!shortProof && shortProof.error().key == "PublicLineWork" && shortProof.error().record == 29, "N-1 callback failure preserved");
    Ledger cancel;
    cancel.cancelAt = 1;
    auto cancelled = ProvePcurveLineLift(*surface, *uv, ends, { 0, 0 }, 0, cancel.Callbacks());
    Require(!cancelled && cancelled.error().status == Status::Cancelled && cancelled.error().record == 23, "cancellation identity retained");
    auto unclamped = Surface(context, 2, false);
    Ledger clamp;
    auto noClamp = ProvePcurveLineLift(*unclamped, *uv, ends, { 0, 0 }, 0, clamp.Callbacks());
    Require(!noClamp && noClamp.error().key == "PcurveLineCarrierKnotsUnestablished", "unclamped carrier not silently altered");
    const auto oldRounding = std::fegetround();
    Require(std::fesetround(FE_DOWNWARD) == 0, "set rounding mode");
    Ledger environment;
    auto badEnvironment = ProvePcurveLineLift(*surface, *uv, ends, { 0, 0 }, 0, environment.Callbacks());
    Require(std::fesetround(oldRounding) == 0, "restore rounding mode");
    Require(!badEnvironment && badEnvironment.error().key == "PcurveLineNumericEnvironment", "unsupported rounding refused");
    auto throwing = round.Callbacks();
    throwing.chargeWork = [](std::size_t n) -> Result<void>
    {
        if (n == 16)
        {
            throw std::overflow_error("public callback overflow must remain an exception");
        }
        return {};
    };
    bool propagated = false;
    try
    {
        (void)ProvePcurveLineLift(*surface, *uv, ends, { .125, .0625 }, .438, throwing);
    }
    catch (const std::overflow_error& error)
    {
        propagated = std::string_view(error.what()) == "public callback overflow must remain an exception";
    }
    Require(propagated, "callback overflow during derivative proof is not numeric refusal");
    double* raw = nullptr;
    ULONG nu = 0, nv = 0;
    Require(surface->GetControlPointsPointer(nu, nv, raw) == SM_SUCCESS, "mutate public synthetic net");
    raw[0] += .5;
    Ledger changed;
    auto stale = (*proof)->Verify(*surface, *uv, changed.Callbacks());
    Require(!stale && stale.error().key == "PcurveLineCarrierDefinitionChanged", "final actual carrier must match the proof");
}
} // namespace
int main()
{
    try
    {
        Test();
        std::cout << "pcurve LINE lift contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
