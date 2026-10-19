// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmApiGeneral.h>
#include <SmBSplineSurface.h>
#include <SmContext.h>
#include <SmCylinder.h>
#include <SmPlane.h>
#include <SmSolutionArray.h>
#include <SmSphere.h>
#include <SmTorus.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{
void Require(bool ok, const char* message)
{
    if (!ok)
    {
        throw std::runtime_error(message);
    }
}
void Good(SmStatus status)
{
    Require(status == SM_SUCCESS, "native call failed");
}
constexpr SM_TYPE trackedSurfaceType = 0x7f181;
class TrackedSurface final : public SmBSplineSurface
{
public:

    explicit TrackedSurface(const SmBSplineSurface& surface) : SmBSplineSurface(surface)
    {
    }
    using Key = std::tuple<std::uint64_t, std::uint64_t, ULONG, ULONG, SmBoolean, SmBoolean, SmBoolean, SmBoolean, SmBoolean>;
    mutable std::vector<Key> calls;
    bool interrupt = false;
    SmPoint2d interruptedUV;
    SmStatus Evaluate(
        const SmPoint2d& uv,
        ULONG du,
        ULONG dv,
        SmBoolean leftU,
        SmBoolean leftV,
        SmBoolean upper,
        SmVector3d* values,
        SmBoolean nonzero = TRUE,
        SmBoolean zeroSampling = TRUE
    ) const override
    {
        calls.emplace_back(std::bit_cast<std::uint64_t>(uv.x), std::bit_cast<std::uint64_t>(uv.y), du, dv, leftU, leftV, upper, nonzero, zeroSampling);
        if (interrupt && std::abs(uv.x - interruptedUV.x) < 1e-8 && std::abs(uv.y - interruptedUV.y) < 1e-8)
        {
            throw std::runtime_error("injected evaluator interruption");
        }
        return SmBSplineSurface::Evaluate(uv, du, dv, leftU, leftV, upper, values, nonzero, zeroSampling);
    }
    SM_COMMON(TrackedSurface, SmBSplineSurface, trackedSurfaceType);
};
SmBoolean TrackedSurface::IsKindOf(SM_TYPE type) const
{
    return type == trackedSurfaceType || SmBSplineSurface::IsKindOf(type);
}
void TrackedSurface::Dump() const
{
    SmBSplineSurface::Dump(FALSE);
}

SmPoint3d Solve(
    TrackedSurface& surface,
    const SmExtent2d& domain,
    const SmPoint3d& target,
    const SmPoint2d& guess,
    SmSolverOperationType op = SM_SO_MINIMIZE
)
{
    SmBoolean found = FALSE;
    SmSolution solution;
    surface.calls.clear();
    Good(surface.LocalPointSolve(domain, op, target, guess, found, solution));
    Require(found != FALSE, "local solver did not find the controlled answer");
    const SmPoint2d uv(solution.m_vStart[0], solution.m_vStart[1]);
    Require(
        domain.GetUInterval().ContainsValue(uv.x, 1e-8) && domain.GetVInterval().ContainsValue(uv.y, 1e-8),
        "local solver escaped its actual search domain"
    );
    // Evaluate the answer outside the traced solve. The tracing override remains
    // active inside the solver, including registered-derived virtual dispatch.
    const auto calls = surface.calls;
    SmPoint3d answer;
    Good(surface.EvaluatePoint(uv, answer));
    surface.calls = calls;
    return answer;
}
void CheckPlane(SmContext& context)
{
    const SmExtent2d domain(SmPoint2d(0, 0), SmPoint2d(10, 10));
    SmPlane plane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), domain, &context);
    TrackedSurface surface(plane);
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const SmPoint3d target = iteration % 2 ? SmPoint3d(8, 1, 4) : SmPoint3d(2, 3, 4);
        const auto answer = Solve(surface, domain, target, SmPoint2d(7, 8));
        Require(answer.DistanceBetween(SmPoint3d(target.x, target.y, 0)) < 1e-9, "affine plane closest point");
        const std::set<TrackedSurface::Key> unique(surface.calls.begin(), surface.calls.end());
        std::printf("plane solve: evaluations=%zu unique=%zu\n", surface.calls.size(), unique.size());
        // The affine patch reaches its exact closest point in one Newton step.
        // Only the initial point and proposed point need surface evaluation;
        // repeating the solved request also proves no cache crosses a query.
        Require(unique.size() == 2 && surface.calls.size() == 2, "redundant surface evaluation during one affine Newton step");
    }
    const SmPoint3d target(2, 3, 0);
    for (int i = 0; i < 2; ++i)
    {
        const auto answer = Solve(surface, domain, target, SmPoint2d(2, 3), SM_SO_INTERSECT);
        Require(answer.DistanceBetween(target) < 1e-9 && surface.calls.size() == 1, "exact initial guess evaluates once per request");
    }
    const SmExtent2d clipped(SmPoint2d(0, 0), SmPoint2d(1, 10));
    const auto clippedAnswer = Solve(surface, clipped, SmPoint3d(2, 3, 0), SmPoint2d(.8, 3));
    Require(clippedAnswer.DistanceBetween(SmPoint3d(1, 3, 0)) < 1e-8, "clipped-domain boundary solution");

    surface.interrupt = true;
    surface.interruptedUV = SmPoint2d(2, 3);
    bool interrupted = false;
    try
    {
        (void)Solve(surface, domain, SmPoint3d(2, 3, 4), SmPoint2d(7, 8));
    }
    catch (const std::runtime_error& e)
    {
        interrupted = std::string_view(e.what()) == "injected evaluator interruption";
    }
    Require(interrupted, "a failure at the new UV must propagate through copied state");
    surface.interrupt = false;
    Require(
        Solve(surface, domain, SmPoint3d(8, 1, 4), SmPoint2d(7, 8)).DistanceBetween(SmPoint3d(8, 1, 0)) < 1e-9,
        "a fresh request after evaluator interruption"
    );
}
void OnSurface(const SmBSplineSurface& definition, double u, double v, bool exactGuess = false)
{
    TrackedSurface surface(definition);
    const auto domain = surface.GetNaturalUVDomain();
    const auto wanted = domain.Evaluate(u, v);
    SmPoint3d target;
    Good(definition.EvaluatePoint(wanted, target));
    const auto guess = exactGuess ? wanted : domain.Evaluate(std::min(.99, u + .015), std::min(.99, v + .02));
    for (const auto op : { SM_SO_MINIMIZE, SM_SO_NORMALIZE, SM_SO_INTERSECT })
    {
        const auto answer = Solve(surface, domain, target, guess, op);
        Require(answer.DistanceBetween(target) < 1e-7, "point-on-surface solve disagrees with defining geometry");
    }
}
void CheckCurved(SmContext& context)
{
    SmCylinder cylinder(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), 5, 0, 360, 7, FALSE, FALSE, TRUE, &context);
    for (double u : { .01, .24, .51, .74, .99 })
    {
        OnSurface(cylinder, u, .4);
    }
    OnSurface(cylinder, 0, .4, true);
    OnSurface(cylinder, 1, .4, true);
    SmSphere sphere(
        SmPoint3d(0, 0, 0),
        SmVector3d(1, 0, 0),
        SmVector3d(0, 1, 0),
        SmExtent2d(SmPoint2d(0, -90), SmPoint2d(360, 90)),
        3,
        FALSE,
        FALSE,
        nullptr,
        &context
    );
    OnSurface(sphere, .22, .45);
    OnSurface(sphere, .53, 0, true);
    OnSurface(sphere, .53, 1, true);
    SmTorus torus(
        SmPoint3d(0, 0, 0),
        SmVector3d(1, 0, 0),
        SmVector3d(0, 1, 0),
        SmExtent2d(SmPoint2d(0, 0), SmPoint2d(360, 360)),
        5,
        2,
        FALSE,
        FALSE,
        nullptr,
        &context
    );
    OnSurface(torus, .24, .42);
    OnSurface(torus, 0, .5, true);
}
void CheckKnots(bool rational)
{
    // Two positive-length quadratic pieces meet at a C0 knot. Positive rational
    // weights change parameterization; neither query nor its guess is normalized
    // by the implementation under test. The shifted domain is also intentional.
    const std::array<double, 8> u{ 1000002, 1000002, 1000002, 1000003, 1000003, 1000005, 1000005, 1000005 };
    const std::array<double, 4> v{ -4, -4, 2, 2 };
    std::array<double, 40> controls{};
    const std::array<double, 5> weights{ 1, 2, 1, 3, 1 };
    for (std::size_t i = 0; i < 5; ++i)
    {
        for (std::size_t j = 0; j < 2; ++j)
        {
            const auto at = 8 * i + 4 * j;
            controls[at] = double(i) / 2;
            controls[at + 1] = 6 * double(j);
            controls[at + 2] = i > 2 ? double(i - 2) : 0;
            controls[at + 3] = rational ? weights[i] : 1;
        }
    }
    SmBSplineSurface surface;
    Good(surface.SetExpert(
        2,
        1,
        SM_SF_UNSPECIFIED,
        SM_EK_CLAMPPED,
        8,
        u.data(),
        4,
        v.data(),
        rational ? SM_CP_EUCLIDIAN_RATIONAL : SM_CP_NON_RATIONAL,
        8,
        4,
        controls.data()
    ));
    for (double normalizedU : { .1, .2, .5, .8 })
    {
        OnSurface(surface, normalizedU, .4);
    }
    OnSurface(surface, 1.0 / 3, .4, true);
}
} // namespace
int main()
{
    try
    {
        SmApiCreateContext();
        SmContext context;
        CheckPlane(context);
        CheckCurved(context);
        CheckKnots(false);
        CheckKnots(true);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
