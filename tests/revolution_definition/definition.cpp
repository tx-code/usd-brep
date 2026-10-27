// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBrepData.h>
#include <SmCircle.h>
#include <SmNativeArchive.h>
#include <SmSTEPSurface.h>
#include <SmSurfOfRevolution.h>
#include <SmTol.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nurbs.h>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

void NonzeroEndCircle()
{
    SmContext context;
    const double radius = 0x1p-30;
    const double zone = SmTol::GetZoneTol3d();
    gw_CPOINT controls[] = { { 1, 0, 1024, 1 }, { radius, 0, 1025, 1 } };
    double knots[] = { -2, -2, 8, 8 };
    std::unique_ptr<SmBSplineCurve> generator(new (context) SmBSplineCurve(3, 1, 3, 1, controls, knots, &context));
    SmSurfOfRevolution* raw = nullptr;
    const auto status = SmSurfOfRevolution::CreateCanonical(context, generator.get(), SmPoint3d(0, 0, 1024), SmVector3d(0, 0, 1), raw, FALSE);
    std::unique_ptr<SmSurfOfRevolution> surface(raw);
    if (surface && generator->GetOwner() == surface.get())
    {
        generator.release();
    }
    Require(status == SM_SUCCESS && surface && !generator, "public synthetic revolution constructs");

    SmSTEPSurface step(*surface, FALSE);
    step.SetContext(&context);
    SmPoint2d nativeUV;
    SmPoint3d ordinaryPoint, stepPoint;
    Require(surface->ConvertUVFromSTEPToNURBS({ 180, 8 }, nativeUV) == SM_SUCCESS, "actual STEP-to-NURBS conversion");
    Require(surface->EvaluatePoint(nativeUV, ordinaryPoint) == SM_SUCCESS, "ordinary NURBS point evaluates");
    Require(step.EvaluatePoint({ 180, 8 }, stepPoint) == SM_SUCCESS, "STEP point evaluates");
    // Independent half-turn about +Z: the nonzero endpoint radius changes sign.
    // Its error budget is radial, so the large axial offset cannot hide a collapse.
    const double radialBudget = radius * 1e-6;
    std::cout << "radius=" << radius << " ordinary_x=" << ordinaryPoint.x << " STEP_x=" << stepPoint.x << '\n';
    Require(std::abs(stepPoint.x + radius) <= radialBudget, "STEP retains the independently defined end circle");
    Require(std::abs(ordinaryPoint.x + radius) <= radialBudget, "ordinary NURBS retains the nonzero end circle");
    SmAssertArray ordinaryFindings, stepFindings;
    Require(
        !surface->AssertValid(&ordinaryFindings, SM_LEVEL_2, SM_WALK) && !step.AssertValid(&stepFindings, SM_LEVEL_2, SM_WALK),
        "original LEVEL2 policy still refuses this near-pole surface in both representations"
    );
    const auto reportsPole = [](const SmAssertArray& findings)
    {
        for (ULONG i = 0; i < findings.GetSize(); ++i)
        {
            if (findings[i]->GetTestIndex() == 5)
            {
                return true;
            }
        }
        return false;
    };
    Require(reportsPole(ordinaryFindings) && reportsPole(stepFindings), "original pole-quality finding is retained");
    Require(double(SmTol::GetZoneTol3d()) == zone, "construction never changes system precision");
}

struct Input
{
    double firstRadius, lastRadius, lastWeight, offset;
    bool swap;
    unsigned frame;
};
using Point = std::array<double, 3>;
struct Definition
{
    Point origin, axis, radial, tangent;
    std::array<gw_CPOINT, 2> controls;
};
Definition Define(const Input& input)
{
    Point axis{ 0, 0, 1 }, radial{ 1, 0, 0 }, tangent{ 0, 1, 0 };
    if (input.frame == 1)
    {
        axis = { 1, 0, 0 };
        radial = { 0, 1, 0 };
        tangent = { 0, 0, 1 };
    }
    if (input.frame == 2)
    {
        const double a = 1 / std::sqrt(3.), b = 1 / std::sqrt(2.);
        axis = { a, a, a };
        radial = { b, -b, 0 };
        tangent = { a * b, a * b, -2 * a * b };
    }
    if (input.frame == 3)
    {
        const double a = 1 / std::sqrt(14.), b = 1 / std::sqrt(5.), c = 1 / std::sqrt(70.);
        axis = { a, 2 * a, 3 * a };
        radial = { 2 * b, -b, 0 };
        tangent = { 3 * c, 6 * c, -5 * c };
    }
    Definition result{ {}, axis, radial, tangent, {} };
    for (unsigned k = 0; k < 3; ++k)
    {
        result.origin[k] = input.offset * axis[k];
    }
    const Point p0{ result.origin[0] + input.firstRadius * radial[0],
                    result.origin[1] + input.firstRadius * radial[1],
                    result.origin[2] + input.firstRadius * radial[2] };
    const Point p1{ result.origin[0] + axis[0] + input.lastRadius * radial[0],
                    result.origin[1] + axis[1] + input.lastRadius * radial[1],
                    result.origin[2] + axis[2] + input.lastRadius * radial[2] };
    result.controls = { gw_CPOINT{ p0[0], p0[1], p0[2], 1 },
                        gw_CPOINT{ p1[0] * input.lastWeight, p1[1] * input.lastWeight, p1[2] * input.lastWeight, input.lastWeight } };
    return result;
}

void Check(SmSurface& surface, const Input& input, const Definition& definition, double textRounding = 0, SmExtent1d angularDomain = SmExtent1d(0, 360))
{
    auto* revolution = dynamic_cast<SmSurfOfRevolution*>(&surface);
    Require(revolution != nullptr, "original revolution type survives");
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    SmBoolean swapped = FALSE;
    Require(revolution->GetCanonical(generator, origin, axis, &swapped) == SM_SUCCESS && generator, "original generator remains accessible");
    Require(bool(swapped) == input.swap, "NURBS parameter ordering is retained");
    Require(
        revolution->GetSTEPUVDomain() == SmExtent2d(angularDomain.GetMin(), -2, angularDomain.GetMax(), 8) &&
            generator->GetSTEPInterval() == SmExtent1d(-2, 8),
        "STEP angle and original nonunit generator parameters are retained"
    );
    ULONG count = 0;
    double* actualControls = nullptr;
    Require(generator->GetControlPointsPointer(count, actualControls) == SM_SUCCESS && count == 2, "source net inventory");
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& expected = definition.controls[i];
        Require(
            std::abs(actualControls[4 * i] - expected.x) <= textRounding && std::abs(actualControls[4 * i + 1] - expected.y) <= textRounding &&
                std::abs(actualControls[4 * i + 2] - expected.z) <= textRounding && std::abs(actualControls[4 * i + 3] - expected.w) <= textRounding,
            "source homogeneous coefficients are unchanged"
        );
    }
    SmTArray<double> knots;
    Require(
        generator->GetKnotsAll(knots) == SM_SUCCESS && knots.GetSize() == 4 && knots[0] == -2 && knots[1] == -2 && knots[2] == 8 && knots[3] == 8,
        "original generator knots are unchanged"
    );
    // A rational degree-one generator has this independent closed form.
    // Check cardinal rotations, where sine/cosine are exact literals, in both
    // evaluators using the actual polar conversion rather than equal UV values.
    constexpr double cosine[] = { 1, 0, -1, 0, 1 };
    constexpr double sine[] = { 0, 1, 0, -1, 0 };
    SmSTEPSurface facade(surface, FALSE);
    facade.SetContext(surface.GetContext());
    for (double v : { -2., -.5, 3., 8. })
    {
        const double s = (v + 2) / 10;
        const double q = input.lastWeight * s / (1 - s + input.lastWeight * s);
        const double radius = (1 - q) * input.firstRadius + q * input.lastRadius;
        const double budget = std::max(std::abs(radius) * 2e-6, 1e-22) + 4 * textRounding + (input.frame >= 2 ? 4e-12 : 0);
        for (unsigned rotation = 0; rotation < 5; ++rotation)
        {
            const double degrees = angularDomain.Evaluate(double(rotation) / 4);
            const double radians = degrees * 3.14159265358979323846 / 180;
            const double c = angularDomain == SmExtent1d(0, 360) ? cosine[rotation] : std::cos(radians);
            const double sAngle = angularDomain == SmExtent1d(0, 360) ? sine[rotation] : std::sin(radians);
            const SmPoint2d uv(degrees, v);
            SmPoint2d nativeUV, restoredUV;
            Require(
                surface.ConvertUVFromSTEPToNURBS(uv, nativeUV) == SM_SUCCESS && surface.ConvertUVFromNURBSToSTEP(nativeUV, restoredUV) == SM_SUCCESS,
                "actual parameter conversion and inverse succeed"
            );
            Require(std::abs(restoredUV.y - v) <= 1e-10, "inverse conversion retains generator parameter");
            SmPoint3d normal, step;
            Require(
                surface.EvaluatePoint(nativeUV, normal) == SM_SUCCESS && facade.EvaluatePoint(uv, step) == SM_SUCCESS,
                "both defining evaluators succeed"
            );
            const Point expected{
                definition.origin[0] + q * definition.axis[0] + radius * (c * definition.radial[0] + sAngle * definition.tangent[0]),
                definition.origin[1] + q * definition.axis[1] + radius * (c * definition.radial[1] + sAngle * definition.tangent[1]),
                definition.origin[2] + q * definition.axis[2] + radius * (c * definition.radial[2] + sAngle * definition.tangent[2])
            };
            const Point normalValues{ normal.x, normal.y, normal.z }, stepValues{ step.x, step.y, step.z };
            for (unsigned k = 0; k < 3; ++k)
            {
                const double coordinateBudget = budget + (definition.axis[k] == 1 ? 4e-12 : 0);
                Require(std::abs(normalValues[k] - expected[k]) <= coordinateBudget, "ordinary NURBS agrees with independent rational revolution");
                Require(std::abs(stepValues[k] - expected[k]) <= coordinateBudget, "STEP agrees with independent rational revolution");
            }
        }
    }
}

void Controls(const std::filesystem::path& directory)
{
    const std::array cases{
        Input{ 1, 0x1p-30, 2, -1024, true, 0 }, Input{ 0x1p-30, 1, 2, 1024, false, 1 }, Input{ 0x1p-29, 0x1p-30, 1, 1024, false, 0 },
        Input{ 1, 0, 1, 1024, false, 1 },       Input{ 0, 1, 2, 0, true, 0 },           Input{ 3, 1, 2, 1048576, true, 1 },
        Input{ 3, 0, 1, 1024, false, 2 },       Input{ 1, 0x1p-44, 1, 1024, false, 0 }, Input{ 0x1p-44, 1, 2, 1024, true, 0 },
        Input{ 3, 0, 1, 0, false, 3 },          Input{ 3, 0, 2, 1024, true, 3 }
    };
    for (std::size_t i = 0; i < cases.size(); ++i)
    {
        const auto& input = cases[i];
        std::cout << "control " << i << '\n';
        const auto definition = Define(input);
        SmContext context;
        double knots[] = { -2, -2, 8, 8 };
        auto controls = definition.controls;
        std::unique_ptr<SmBSplineCurve> generator(new (context) SmBSplineCurve(3, 1, 3, 1, controls.data(), knots, &context));
        SmSurfOfRevolution* raw = nullptr;
        const auto status = SmSurfOfRevolution::CreateCanonical(
            context,
            generator.get(),
            SmPoint3d(definition.origin[0], definition.origin[1], definition.origin[2]),
            SmVector3d(definition.axis[0], definition.axis[1], definition.axis[2]),
            raw,
            input.swap ? TRUE : FALSE
        );
        std::unique_ptr<SmSurfOfRevolution> surface(raw);
        if (surface && generator->GetOwner() == surface.get())
        {
            generator.release();
        }
        Require(status == SM_SUCCESS && surface && !generator, "control revolution constructs");
        Check(*surface, input, definition);
        if (i == 0 || i == 7 || i == 8)
        {
            SmCircle sweep(SmPoint3d(0, 0, input.offset), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmExtent1d(0, 360), 2, 3, &context);
            Require(
                sweep.ScaleKnotVector(-7, 5) == SM_SUCCESS && surface->MakeNurbWithSweepParams(&sweep) == SM_SUCCESS,
                "explicit sweep parameterization constructs without snapping small rows"
            );
            Check(*surface, input, definition);
            const auto native = surface->GetNaturalUVDomain();
            Require((input.swap ? native.GetVInterval() : native.GetUInterval()) == SmExtent1d(-7, 5), "explicit sweep knot parameters are retained");
        }
        SmContext copyContext;
        SmSurface* copied = nullptr;
        Require(surface->Copy(copyContext, copied) == SM_SUCCESS && copied && copied != surface.get(), "independent copy succeeds");
        std::unique_ptr<SmSurface> copy(copied);
        Check(*copy, input, definition);
        if (i == 0)
        {
            SmTArray<SmCurve*> curves;
            SmTArray<SmSurface*> surfaces;
            SmTArray<long> trees;
            SmTArray<SmBrep*> breps;
            surfaces.Add(surface.get());
            for (const auto format : { SM_BINARY, SM_ASCII })
            {
                const auto path = directory / (format == SM_BINARY ? "revolution.smb" : "revolution.smt");
                Require(
                    SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, format, TRUE, FALSE) == SM_SUCCESS,
                    "complete revolution archive writes"
                );
                SmContext readContext;
                SmTArray<SmSurface*> restored;
                SmObjsDelete<SmSurface*> cleanup(&restored);
                Require(
                    SmNativeArchive::ReadPartFromFile(readContext, path.c_str(), curves, restored, trees, breps, format) == SM_SUCCESS &&
                        restored.GetSize() == 1 && !curves.GetSize() && !breps.GetSize(),
                    "archive inventory remains complete"
                );
                // Existing text geometry has 16 decimal places. Preserve that
                // codec contract; binary and ordinary/copy definitions are exact.
                Check(*restored[0], input, definition, format == SM_ASCII ? 1e-16 : 0);
            }
        }
        if (i == 7)
        {
            Require(surface->AdjustSTEPUVDomain(SmExtent2d(45, -2, 225, 8)) == SM_SUCCESS, "partial angular domain constructs");
            Check(*surface, input, definition, 0, SmExtent1d(45, 225));
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        NonzeroEndCircle();
        Require(argc == 2, "archive output directory is required");
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        Controls(directory);
        std::cout << "revolution definition passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
