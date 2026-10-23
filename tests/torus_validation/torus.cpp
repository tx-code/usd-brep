// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmAxis2Placement.h>
#include <SmContext.h>
#include <SmTorus.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void CheckSpindleDomain(const SmExtent2d& domain, bool expected, bool swap)
{
    SmContext context;
    SmTorus* raw = nullptr;
    // The profile radius from the revolution axis is R + r*cos(v).
    // R=1,r=2 vanishes at v=120/240 degrees independently of the major
    // rotation u. These public angular intervals stay far from the endpoints.
    Require(
        SmTorus::CreateCanonical(context, SmAxis2Placement(), 1, 2, raw, swap ? TRUE : FALSE, FALSE, nullptr, &domain) == SM_SUCCESS && raw,
        "create public spindle support"
    );
    std::unique_ptr<SmTorus> torus(raw);
    SmAssertArray reports;
    SmTArray<ULONG> ownTests;
    for (ULONG identity : { 0UL, 1UL, 2UL, 3UL, 4UL })
    {
        ownTests.Add(identity);
    }
    // Invoke all torus predicates without parent checks first: a parent finding
    // must not mask whether this particular analytic-domain guard is correct.
    const bool actual = torus->AssertValid(&reports, SM_LEVEL_GIVEN, SM_NO_WALK, &ownTests) != FALSE;
    if (actual != expected)
    {
        std::cerr << "spindle U=[" << domain.GetMin().x << ',' << domain.GetMax().x << "] V=[" << domain.GetMin().y << ',' << domain.GetMax().y
                  << "] swap=" << swap << " expected=" << expected << " actual=" << actual << '\n';
    }
    Require(actual == expected, "spindle self-intersection must depend on analytic V, independently of U and NURBS axis swap");
    if (!expected)
    {
        return;
    }
    SmAssertArray fullReports;
    Require(
        torus->AssertValid(&fullReports, SM_LEVEL_0, SM_NO_WALK) != FALSE && fullReports.GetSize() == 0,
        "regular public support must also pass the normal complete surface validation"
    );
    // Use the defining equation as independent point truth, rather than an
    // importer or another kernel. Both analytic and converted NURBS evaluation
    // must describe the same unchanged support before/after validation.
    const double u = (domain.GetMin().x + domain.GetMax().x) / 2;
    const double v = (domain.GetMin().y + domain.GetMax().y) / 2;
    const double radians = std::numbers::pi / 180;
    const SmPoint3d truth(
        (1 + 2 * std::cos(v * radians)) * std::cos(u * radians),
        (1 + 2 * std::cos(v * radians)) * std::sin(u * radians),
        2 * std::sin(v * radians)
    );
    SmPoint3d analytic, ordinary;
    SmPoint2d nurbs;
    Require(torus->EvaluateSTEPPoint(SmPoint2d(u, v), analytic) == SM_SUCCESS, "analytic support evaluation");
    Require(torus->ConvertUVFromSTEPToNURBS(SmPoint2d(u, v), nurbs) == SM_SUCCESS, "analytic to NURBS parameter conversion");
    Require(torus->EvaluatePoint(nurbs, ordinary) == SM_SUCCESS, "ordinary support evaluation");
    Require(analytic.DistanceBetween(truth) < 1e-12 && ordinary.DistanceBetween(truth) < 1e-12, "retain torus definition and parameter semantics");
    Require(torus->GetMajorRadius() == 1 && torus->GetMinorRadius() == 2, "validation does not change radii");
}
class DomainTorus : public SmTorus
{
public:

    using SmTorus::SmTorus;
    void SetInvalidDomain(const SmExtent2d& domain)
    {
        // Deliberately corrupt only the stored analytic domain, after normal
        // construction, to exercise the validator's own registered predicates.
        m_vAnalUVDomain = domain;
    }
};
void CheckRegisteredDomainTest(ULONG identity, const SmExtent2d& domain, bool expected)
{
    SmContext context;
    DomainTorus torus(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmExtent2d(0, 0, 360, 360), 5, 2, FALSE, FALSE, nullptr, &context);
    Require(torus.MakeNurb() == SM_SUCCESS, "create ordinary public torus basis");
    torus.SetInvalidDomain(domain);
    SmTArray<ULONG> requested;
    requested.Add(identity);
    SmAssertArray reports;
    const bool valid = torus.AssertValid(&reports, SM_LEVEL_GIVEN, SM_NO_WALK, &requested) != FALSE;
    bool found = false;
    for (ULONG i = 0; i < reports.GetSize(); ++i)
    {
        if (reports[i]->GetTestIndex() == identity)
        {
            found = true;
            Require(reports[i]->GetOwner() == &torus, "domain diagnostic retains its actual owner");
        }
    }
    Require(found == !expected, "registered analytic U/V extent diagnostic must use its declared identity");
#ifdef SM_DEBUG_CODE
    // Test-selection filtering is debug-only in the existing report macros.
    // Release additionally runs the other local predicates, including the
    // intentionally inconsistent analytic/NURBS parameterization in this test.
    Require(valid == expected && reports.GetSize() == (expected ? 0U : 1U), "selected debug predicate runs alone");
#else
    Require(valid == (reports.GetSize() == 0), "release local findings remain consistent with the validation result");
#endif
}
} // namespace
int main()
{
    bool passed = true;
    const auto run = [&](const auto& check)
    {
        try
        {
            check();
        }
        catch (const std::exception& error)
        {
            passed = false;
            std::cerr << error.what() << '\n';
        }
    };
    for (bool swap : { false, true })
    {
        for (const auto domain :
             { SmExtent2d(0, -60, 360, 60), SmExtent2d(0, -60, 60, 60), SmExtent2d(150, -60, 210, 60), SmExtent2d(-360, -60, 0, 60) })
        {
            run(
                [&]
                {
                    CheckSpindleDomain(domain, true, swap);
                }
            );
        }
        run(
            [&]
            {
                CheckSpindleDomain(SmExtent2d(0, 150, 60, 210), false, swap);
            }
        );
        run(
            [&]
            {
                CheckSpindleDomain(SmExtent2d(0, 0, 360, 360), false, swap);
            }
        );
    }
    run(
        [&]
        {
            CheckRegisteredDomainTest(3, SmExtent2d(0, 0, 361, 90), false);
        }
    );
    run(
        [&]
        {
            CheckRegisteredDomainTest(4, SmExtent2d(0, 0, 90, 361), false);
        }
    );
    run(
        [&]
        {
            CheckRegisteredDomainTest(3, SmExtent2d(0, 0, 90, 361), true);
        }
    );
    run(
        [&]
        {
            CheckRegisteredDomainTest(4, SmExtent2d(0, 0, 361, 90), true);
        }
    );
    run(
        [&]
        {
            CheckRegisteredDomainTest(3, SmExtent2d(0, 0, 360, 360), true);
        }
    );
    run(
        [&]
        {
            CheckRegisteredDomainTest(4, SmExtent2d(0, 0, 360, 360), true);
        }
    );
    std::cout << "torus analytic domain contracts passed=" << passed << '\n';
    return passed ? 0 : 1;
}
