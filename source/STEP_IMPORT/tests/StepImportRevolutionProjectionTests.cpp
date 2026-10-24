// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "PublicSorReprojectionFixture.h"
#include "StepImportDerivedUv.h"
#include "StepImportTrimMeasurement.h"

#include <SmEdgeuse.h>
#include <iostream>
#include <numbers>
#include <vector>

using namespace step_import;
using namespace step_import::detail;
using public_sor_repro::Require;
namespace
{
struct Signature
{
    std::vector<double> controls, u, v;
    SmExtent2d natural, step;
    bool operator==(const Signature&) const = default;
};
Signature Stored(const SmBSplineSurface& surface)
{
    ULONG nu = 0, nv = 0, ku = 0, kv = 0;
    double *p = nullptr, *u = nullptr, *v = nullptr;
    public_sor_repro::RequireStatus(surface.GetControlPointsPointer(nu, nv, p));
    public_sor_repro::RequireStatus(surface.GetKnotsPointers(ku, kv, u, v));
    return { { p, p + std::size_t(nu) * nv * 4 }, { u, u + ku }, { v, v + kv }, surface.GetNaturalUVDomain(), surface.GetSTEPUVDomain() };
}
SmPoint3d Independent(double t)
{
    // Exact public rational UV and generator definitions, independent of drop.
    const double a = (t + 3) / 12, b = 1 - a, w = b * b + 4 * a * b + a * a;
    const double degrees = (130 * b * b + 4 * 47.2 * a * b + 40 * a * a) / w;
    const double v = (6 * b * b + 4 * .9 * a * b) / w;
    const double s = (v + 2) / 10, q = 1 + 2 * s - 2 * s * s;
    const double radius = (3 + 10 * s - 10 * s * s) / q;
    const double z = (6 * s + s * s) / q, angle = degrees * std::numbers::pi / 180;
    return { -radius * std::sin(angle), radius * std::cos(angle), z };
}
void Run(bool originalBasis)
{
    SmContext context;
    auto input = public_sor_repro::Create(context);
    auto* sourceCurve = input.edge->GetCurve();
    auto* surface = input.face->GetSurface();
    const auto signature = Stored(*static_cast<SmBSplineSurface*>(surface));
    const auto interval = input.edge->GetInterval();
    const auto natural = sourceCurve->GetNaturalInterval();
    const auto tolerance = input.edge->GetTolerance();
    auto* use = input.edge->GetPrimaryEdgeuse();
    Require(!use->GetUVTrimCurve(), "Public operation starts before lazy trim materialization");
    std::size_t calls = 0;
    auto candidate = ProjectDerivedUv(
        *use,
        .001,
        [&]() -> Result<void>
        {
            ++calls;
            return {};
        },
        originalBasis
    );
    Require(
        candidate && candidate->status == DerivedUvStatus::Ready && candidate->curve && candidate->rawPair,
        "Controlled projection admits one original-parameter candidate"
    );
    Require(candidate->curve->GetOwner() == nullptr && candidate->curve->GetDim() == 2, "Unpublished derived candidate has sole caller ownership");
    Require(candidate->rawPair->maximumMm <= .001, "Original paired budget is unchanged");
    // Finite observations supplement the product knot-span measurement. Neither
    // establishes a continuous error certificate or changes acceptance budgets.
    for (unsigned i = 0; i <= 2048; ++i)
    {
        const auto t = interval.Evaluate(double(i) / 2048);
        SmPoint3d source, uv, lifted;
        public_sor_repro::RequireStatus(sourceCurve->EvaluatePoint(t, source));
        public_sor_repro::RequireStatus(candidate->curve->EvaluatePoint(t, uv));
        public_sor_repro::RequireStatus(surface->EvaluatePoint({ uv.x, uv.y }, lifted));
        Require(source.DistanceBetween(Independent(t)) <= 1e-10, "Independent exact source composition");
        Require(source.DistanceBetween(lifted) <= .001, "Same-t lifted sample retains physical budget");
    }
    Require(
        input.edge->GetCurve() == sourceCurve && sourceCurve->GetOwner() == input.edge && input.face->GetSurface() == surface &&
            surface->GetOwner() == input.face && input.edge->GetInterval() == interval && sourceCurve->GetNaturalInterval() == natural &&
            input.edge->GetTolerance() == tolerance && !use->GetUVTrimCurve() && Stored(*static_cast<SmBSplineSurface*>(surface)) == signature,
        "Definition, surface net, topology owner, parameters and work zone stay unchanged"
    );
    SmSTEPSurface borrowed(*surface, FALSE);
    borrowed.SetContext(&context);
    SmAxis2Placement transform;
    transform.Translate(SmVector3d(10, -2, 3));
    Require(
        borrowed.Transform(transform) != SM_SUCCESS && Stored(*static_cast<SmBSplineSurface*>(surface)) == signature,
        "Borrowed STEP support cannot mutate someone else's geometry"
    );
    SmSurface* rawCopy = nullptr;
    public_sor_repro::RequireStatus(borrowed.Copy(context, rawCopy));
    std::unique_ptr<SmSurface> ownedCopy(rawCopy);
    SmPoint3d beforeMove, afterMove;
    public_sor_repro::RequireStatus(borrowed.EvaluatePoint({ 80, 2 }, beforeMove));
    public_sor_repro::RequireStatus(ownedCopy->Transform(transform));
    public_sor_repro::RequireStatus(ownedCopy->EvaluatePoint({ 80, 2 }, afterMove));
    Require(
        afterMove.DistanceBetween(beforeMove + SmVector3d(10, -2, 3)) <= 1e-10 && ownedCopy->GetNaturalUVDomain() == borrowed.GetNaturalUVDomain() &&
            Stored(*static_cast<SmBSplineSurface*>(surface)) == signature,
        "Owned wrapper moves its independent definition while retaining source STEP parameters"
    );
    for (std::size_t failAt = 1; failAt <= calls; ++failAt)
    {
        std::size_t seen = 0;
        auto stopped = ProjectDerivedUv(
            *use,
            .001,
            [&]() -> Result<void>
            {
                if (++seen == failAt)
                {
                    return tl::unexpected(Failure{ Status::Cancelled, "ProjectionCallerCancel", "caller", 17, "Caller" });
                }
                return {};
            },
            originalBasis
        );
        Require(
            !stopped && stopped.error().status == Status::Cancelled && stopped.error().key == "ProjectionCallerCancel" &&
                stopped.error().record == 17 && !use->GetUVTrimCurve() && surface->GetOwner() == input.face,
            "Every native boundary preserves cancellation and restores surface owner"
        );
    }
}
} // namespace
int main()
{
    try
    {
        Run(false);
        Run(true);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
