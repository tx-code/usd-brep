// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <nurbs.h>
#include <stdexcept>
#include <string_view>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Check(std::string_view mode)
{
    const bool right = mode == "right", full = mode == "full", separated = mode == "separated";
    Require(right || full || separated || mode == "left", "known partial-coincidence control");
    SmContext context;
    std::unique_ptr<SmBrep> body(new (context) SmBrep());
    SmTArray<SmPoint3d> points;
    for (const auto point : { SmPoint3d(-1, -1, 0), SmPoint3d(-1, 1, 0), SmPoint3d(4, -1, 0), SmPoint3d(4, 1, 0) })
    {
        points.Add(point);
    }
    SmTArray<ULONG> multiplicities;
    multiplicities.Add(2);
    multiplicities.Add(2);
    SmTArray<double> values;
    values.Add(0);
    values.Add(1);
    SmBSplineSurface* raw = nullptr;
    Require(
        SmBSplineSurface::CreateCanonical(
            context,
            1,
            1,
            points,
            SM_SF_UNSPECIFIED,
            multiplicities,
            multiplicities,
            values,
            values,
            SM_KT_UNSPECIFIED,
            nullptr,
            nullptr,
            raw
        ) == SM_SUCCESS,
        "affine planar support"
    );
    std::unique_ptr<SmBSplineSurface> support(raw);
    const double left = right || separated ? .65 : .25;
    SmFace* face = nullptr;
    const auto made = body->CreateFaceFromSurface(support.get(), SmExtent2d(left, .375, left + .1, .625), face);
    if (support->GetOwner())
    {
        support.release();
    }
    Require(made == SM_SUCCESS && face, "bounded rectangle on the support");
    // Broader support coordinates do not enlarge the trimmed rectangle. Clear
    // the rectangular-domain shortcut so classification uses its actual loops.
    face->SetUVDomain(SmExtent2d(0, 0, 1, 1));
    face->SetIsRectangularTrim(FALSE);
    gw_CPOINT poles[] = { { 0, 0, 0, NL_NOW },      { 1. / 3, 0, 0, NL_NOW }, { 2. / 3, 0, 0, NL_NOW }, { 1, 0, 0, NL_NOW },
                          { 4. / 3, 0, 0, NL_NOW }, { 5. / 3, 0, 1, NL_NOW }, { 2, 0, 1, NL_NOW },      { 7. / 3, 0, 1, NL_NOW },
                          { 8. / 3, 0, 1, NL_NOW }, { 3, 0, 1, NL_NOW } };
    for (auto& pole : poles)
    {
        pole.z = full ? 0 : right ? 1 - pole.z : pole.z;
    }
    double knots[] = { 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3 };
    std::unique_ptr<SmBSplineCurve> curve(new (context) SmBSplineCurve(3, 9, 13, 3, poles, knots, &context));
    SmEdge* edge = nullptr;
    Require(body->CreateWireEdgeFromCurve(curve.get(), SmExtent1d(0, 3), edge) == SM_SUCCESS, "independent wire edge");
    curve.release();
    constexpr double tolerance = 1e-5;
    for (const auto partial : { FALSE, TRUE })
    {
        SmBoolean found = FALSE;
        SmSolution solution;
        Require(
            face->GetSurface()
                        ->SimpleCoincidenceChecker(face->GetUVDomain(), *edge->GetCurve(), edge->GetInterval(), tolerance, found, solution, partial) ==
                    SM_SUCCESS &&
                bool(found) == (partial || full),
            "partial screen finds the known on-plane segment"
        );
        if (!found)
        {
            continue;
        }
        Require(solution.m_eSolutionType == SM_ST_RANGE_OF_VALUES, "range result for coincidence");
        for (const auto* endpoint : { &solution.m_vStart, &solution.m_vEnd })
        {
            SmPoint3d actual, lifted;
            Require(
                edge->GetCurve()->EvaluatePoint((*endpoint)[0], actual) == SM_SUCCESS &&
                    face->GetSurface()->EvaluatePoint(SmPoint2d((*endpoint)[1], (*endpoint)[2]), lifted) == SM_SUCCESS,
                "returned tuple evaluates"
            );
            Require(actual.DistanceBetween(lifted) <= tolerance, "returned parameter and UV describe the same sampled pair");
            // z=0 is the independent support plane, and all curve XY lies inside
            // its domain. Thus |z| is the exact point-to-support distance here.
            Require(
                std::abs(endpoint->m_dSolutionValue - std::abs(actual.z)) < 1e-10,
                "returned distance belongs to its reported parameter, not the initial midpoint"
            );
        }
    }
    SmAssertArray pointers, reports;
    Require(body->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "valid native pointer graph");
    const bool coincidence = body->ContainsCoincidentTopology(tolerance, nullptr, nullptr, &reports, SM_LEVEL_2, nullptr, TRUE) != FALSE;
    // The separated rectangle is at x in [2.25,2.75], where this curve has
    // z=1. Its on-plane segment ends at x=1. The other controls overlap z=0.
    Require(coincidence != separated, "trimmed-face coincidence agrees with independent piecewise geometry");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "one control required");
        Check(argv[1]);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
