// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmAttribute.h>
#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmSurface.h>
#include <SmCrvOnSurf.h>
#include <SmNativeArchive.h>
#include <SmSTEPSurface.h>
#include <SmSurfOfRevolution.h>
#include <SmTol.h>
#include <nurbs.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace curve_copy_controls
{
namespace
{
void Require(bool value, const char* message)
{
    if (!value) { throw std::runtime_error(message); }
}
std::unique_ptr<SmCrvOnSurf> Definition(SmContext& context)
{
    // Entirely synthetic near-axis frustum. The nonzero circular end boundary
    // must remain a circle; using a finer numerical zone must not snap its net.
    gw_CPOINT cp[] = { { 1, 0, 1024, 1 }, { 0x1p-30, 0, 1025, 1 } };
    double knots[] = { 0, 0, 1, 1 };
    std::unique_ptr<SmBSplineCurve> generator(new (context) SmBSplineCurve(3, 1, 3, 1, cp, knots, &context));
    SmSurfOfRevolution* raw = nullptr;
    const auto status = SmSurfOfRevolution::CreateCanonical(context, generator.get(), SmPoint3d(0, 0, 1024), SmVector3d(0, 0, 1), raw, FALSE);
    std::unique_ptr<SmSurfOfRevolution> base(raw);
    if (base && generator->GetOwner() == base.get()) { generator.release(); }
    Require(status == SM_SUCCESS && base && !generator, "synthetic revolution construction");
    std::unique_ptr<SmSTEPSurface> wrapper(new (context) SmSTEPSurface(*base, TRUE));
    base.release();
    wrapper->SetContext(&context);
    gw_CPOINT uvcp[] = { { 30, .2, 0, 1 }, { 120, .8, 0, 1 } };
    double uvKnots[] = { -2, -2, 3, 3 };
    std::unique_ptr<SmBSplineCurve> uv(new (context) SmBSplineCurve(2, 1, 3, 1, uvcp, uvKnots, &context));
    std::unique_ptr<SmCrvOnSurf> curve(new (context) SmCrvOnSurf(*uv, *wrapper, nullptr, 3, &context));
    uv.release(); wrapper.release();
    return curve;
}
bool Valid(const SmCurve& curve)
{
    SmAssertArray findings;
    return curve.AssertValid(&findings, SM_LEVEL_2, SM_WALK) != FALSE && findings.GetSize() == 0;
}
void Check(const SmCrvOnSurf& curve, double requested, double scale = 1, double textRounding = 0)
{
    double value = 0;
    Require(curve.GetSupportWorkTolerance(value) == SM_SUCCESS && value == requested, "exact owning work precision");
    Require(double(SmTol::GetZoneTol3d(curve.GetBaseSurface())) == std::min(double(SmTol::GetZoneTol3d()), requested), "local support zone");
    Require(Valid(curve), "full composed-curve LEVEL_2 validity");
    const auto* wrapper = dynamic_cast<const SmSTEPSurface*>(curve.GetBaseSurface());
    const auto* revolution = wrapper ? dynamic_cast<const SmSurfOfRevolution*>(wrapper->GetBaseSurface()) : nullptr;
    Require(revolution && wrapper->GetOwner() == &curve && curve.GetUVCurve()->GetOwner() == &curve, "actual owned carrier graph");
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    Require(revolution->GetCanonical(generator, origin, axis) == SM_SUCCESS && generator, "retained original generator");
    ULONG count = 0;
    double* controls = nullptr;
    Require(generator->GetControlPointsPointer(count, controls) == SM_SUCCESS && count == 2, "generator net inventory");
    const double expected[] = { scale, 0, 1024 * scale, 1, 0x1p-30 * scale, 0, 1025 * scale, 1 };
    for (unsigned i = 0; i < 8; ++i)
    {
        // The unchanged legacy text geometry codec has 16 decimal places.
        // Only the separately encoded precision must round-trip bitwise there.
        Require(std::abs(controls[i] - expected[i]) <= textRounding, "retained defining generator coefficients");
    }
    // Independent closed-form parameter law, not a fitted curve or mesh oracle.
    for (double t : { -2., -.75, 1., 3. })
    {
        const double a = (t + 2) / 5;
        const double u = (30 + 90 * a) * std::numbers::pi / 180;
        const double v = .2 + .6 * a;
        const double radius = 1 + (0x1p-30 - 1) * v;
        SmPoint3d actual;
        Require(curve.EvaluatePoint(t, actual) == SM_SUCCESS &&
                std::abs(actual.x - scale * radius * std::cos(u)) < 1e-12 * scale &&
                std::abs(actual.y - scale * radius * std::sin(u)) < 1e-12 * scale &&
                std::abs(actual.z - scale * (1024 + v)) < 1e-12 * scale,
                "retained STEP UV and independent frustum equation");
    }
}
struct Part
{
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> dc{ &curves };
    SmObjsDelete<SmSurface*> ds{ &surfaces };
    SmObjsDelete<SmBrep*> db{ &breps };
};
class BadFrame : public SmAxis2Placement
{
public:
    explicit BadFrame(bool nonfinite)
    {
        if (nonfinite) { m_vOrigin.x = std::numeric_limits<double>::infinity(); }
        else { m_vXAxis = SmVector3d(2, 0, 0); }
    }
};
class EditingCurve : public SmCrvOnSurf
{
public:
    using SmCrvOnSurf::SmCrvOnSurf;
    unsigned action = 0;
    double replacement = 0;
    void Notify(SmNotifyOperation operation, SmObject* a, SmObject* b, SmObject* c) override
    {
        if (operation == SM_NO_PRE_EDIT && action)
        {
            const auto selected = action;
            action = 0;
            if (selected == 3) { throw std::domain_error("ActualNotificationException"); }
            Require(SetSupportWorkTolerance(selected == 1 ? 0 : replacement) == SM_SUCCESS, "test notification edit");
        }
        SmCrvOnSurf::Notify(operation, a, b, c);
    }
};
void CallbackCases(SmContext& context, double precision)
{
    for (unsigned action = 1; action <= 3; ++action)
    {
        auto original = Definition(context);
        auto* uv = const_cast<SmCurve*>(original->GetUVCurve());
        auto* support = const_cast<SmSurface*>(original->GetBaseSurface());
        original->SetOwnerFlag(0);
        std::unique_ptr<EditingCurve> curve(new (context) EditingCurve(*uv, *support, nullptr, 3, &context));
        original.reset();
        Require(curve->SetSupportWorkTolerance(precision) == SM_SUCCESS, "notification test precision");
        curve->replacement = precision / 2;
        curve->action = action;
        SmVector3d scale(2, 2, 2);
        bool threw = false;
        SmStatus status = SM_SUCCESS;
        try { status = curve->Transform(SmAxis2Placement(), &scale); }
        catch (const std::domain_error& error) { threw = std::string(error.what()) == "ActualNotificationException"; }
        Require(action == 3 ? threw : status != SM_SUCCESS, "notification edits cannot trigger stale attribute access or silent overwrite");
        double observed = 0;
        Require(curve->GetSupportWorkTolerance(observed) == SM_SUCCESS && observed == (action == 1 ? 0 : action == 2 ? precision / 2 : precision),
                "notification changes and real exceptions retain their own state");
    }
}
}

bool PrecisionArchive(const char* outputDirectory, bool ascii)
{
    try
    {
        SmContext context, copyContext;
        const double global = SmTol::GetZoneTol3d();
        const double precision = std::nextafter(1e-11, std::numeric_limits<double>::infinity());
        auto curve = Definition(context), unrelated = Definition(context);
        Require(!Valid(*curve) && !Valid(*unrelated), "baseline near-axis validator refusal");
        Require(curve->SetSupportWorkTolerance(precision) == SM_SUCCESS, "explicit local precision selection");
        Check(*curve, precision);
        CallbackCases(context, precision);
        Require(!Valid(*unrelated) && double(SmTol::GetZoneTol3d()) == global, "unrelated carrier and global zone unchanged");
        for (double invalid : { -1., global * 2, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
        {
            Require(curve->SetSupportWorkTolerance(invalid) != SM_SUCCESS, "invalid or wider selection refuses");
            Check(*curve, precision);
        }
        auto* attribute = curve->FindAttribute(SM_AI_COS_SUPPORT_WORK_TOLERANCE);
        auto* duplicate = attribute->MakeCopy(context);
        curve->AddAttribute(duplicate);
        double observed = 0;
        SmCurve* badCopy = nullptr;
        Require(curve->GetSupportWorkTolerance(observed) != SM_SUCCESS && !Valid(*curve) &&
                    curve->Copy(copyContext, badCopy) != SM_SUCCESS && !badCopy, "duplicate policy cannot be normalized by Copy");
        curve->RemoveAttribute(duplicate);
        unrelated->AddAttribute(attribute);
        Require(curve->GetSupportWorkTolerance(observed) != SM_SUCCESS && unrelated->GetSupportWorkTolerance(observed) != SM_SUCCESS,
                "shared COPY precision refuses");
        unrelated->RemoveAttribute(attribute);
        Check(*curve, precision);
        static_cast<SmGenericAttribute*>(attribute)->SetLongElement(0, 2);
        Require(curve->GetSupportWorkTolerance(observed) != SM_SUCCESS && !Valid(*curve), "unknown payload version refuses");
        static_cast<SmGenericAttribute*>(attribute)->SetLongElement(0, 1);
        SmCurve* copied = nullptr;
        Require(curve->Copy(copyContext, copied) == SM_SUCCESS && copied, "precision carrier independent copy");
        std::unique_ptr<SmCurve> copyOwner(copied);
        auto* copy = dynamic_cast<SmCrvOnSurf*>(copied);
        Require(copy, "copy preserves composition");
        curve.reset();
        Check(*copy, precision);
        SmBSplineCurve* cache = nullptr;
        Require(copy->GetOrCreateBSApproxPointer(cache, 1e-6) == SM_SUCCESS && cache, "warm derived cache control");
        Require(copy->Transform(BadFrame(false)) != SM_SUCCESS && copy->Transform(BadFrame(true)) != SM_SUCCESS,
                "distorted or nonfinite frame refuses before editing");
        Check(*copy, precision);
        for (auto rejectedScale : { SmVector3d(2, 3, 2), SmVector3d(1, -1, 1), SmVector3d(0, 0, 0) })
        {
            Require(copy->Transform(SmAxis2Placement(), &rejectedScale) != SM_SUCCESS, "unsupported scale preflight");
            Check(*copy, precision);
        }
        SmVector3d scale(2, 2, 2);
        Require(copy->Transform(SmAxis2Placement(), &scale) == SM_SUCCESS, "uniform scale support precision");
        Check(*copy, precision * 2, 2);
        // Write inventories borrow their entries; copyOwner retains ownership
        // even if writing throws before returning a status.
        SmTArray<SmCurve*> curves;
        SmTArray<SmSurface*> surfaces;
        SmTArray<long> trees;
        SmTArray<SmBrep*> breps;
        curves.Add(copy);
        const auto directory = std::filesystem::path(outputDirectory);
        std::filesystem::create_directories(directory);
        const auto format = ascii ? SM_ASCII : SM_BINARY;
        const auto path = directory / (ascii ? "precision.smt" : "precision.smb");
        const auto written = SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, format, TRUE, FALSE);
        Require(written == SM_SUCCESS, "exact precision native write");
        Part read;
        Require(SmNativeArchive::ReadPartFromFile(read.context, path.c_str(), read.curves, read.surfaces, read.trees, read.breps, format) == SM_SUCCESS &&
                    read.curves.GetSize() == 1 && !read.surfaces.GetSize() && !read.breps.GetSize(), "complete precision archive read");
        copyOwner.reset();
        const auto* roundtrip = dynamic_cast<const SmCrvOnSurf*>(read.curves[0]);
        Require(roundtrip, "archive preserves composition");
        Check(*roundtrip, precision * 2, 2, ascii ? 1e-12 : 0);
        Require(double(SmTol::GetZoneTol3d()) == global, "no global precision mutation");
        std::cout << "local carrier precision, independent equation, ownership, Copy, scale and archive passed\n";
        return true;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return false;
    }
}
}
