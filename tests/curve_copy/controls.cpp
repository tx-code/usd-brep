// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAttribute.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmCylinder.h>
#include <SmLine.h>
#include <SmNativeArchive.h>
#include <SmPlane.h>
#include <SmSTEPSurface.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <numbers>
#include <vector>

namespace curve_copy_controls
{
namespace
{
bool Check(const char* name, bool value)
{
    std::cout << name << ": " << (value ? "true" : "false") << '\n';
    return value;
}
bool SameInterval(const SmExtent1d& a, const SmExtent1d& b)
{
    return a.GetMin() == b.GetMin() && a.GetMax() == b.GetMax();
}
bool SameDomain(const SmExtent2d& a, const SmExtent2d& b)
{
    return a.GetUMin() == b.GetUMin() && a.GetUMax() == b.GetUMax() && a.GetVMin() == b.GetVMin() && a.GetVMax() == b.GetVMax();
}
bool Near(double a, double b, double tolerance)
{
    return a == b || std::abs(a - b) <= tolerance;
}
bool SameCurveBasis(const SmCurve& a, const SmCurve& b, double tolerance = 0.)
{
    const auto* x = dynamic_cast<const SmBSplineCurve*>(&a);
    const auto* y = dynamic_cast<const SmBSplineCurve*>(&b);
    if (!x || !y || a.GetType() != b.GetType() || a.GetDim() != b.GetDim() || a.GetDegree() != b.GetDegree() ||
        !SameInterval(a.GetNaturalInterval(), b.GetNaturalInterval()))
    {
        return false;
    }
    ULONG nx = 0, ny = 0;
    double *px = nullptr, *py = nullptr;
    if (x->GetControlPointsPointer(nx, px) != SM_SUCCESS || y->GetControlPointsPointer(ny, py) != SM_SUCCESS || nx != ny)
    {
        return false;
    }
    for (ULONG i = 0; i < 4 * nx; ++i)
    {
        if (!Near(px[i], py[i], tolerance))
        {
            return false;
        }
    }
    SmTArray<double> kx, ky;
    if (x->GetKnotsAll(kx) != SM_SUCCESS || y->GetKnotsAll(ky) != SM_SUCCESS || kx.GetSize() != ky.GetSize())
    {
        return false;
    }
    for (ULONG i = 0; i < kx.GetSize(); ++i)
    {
        if (kx[i] != ky[i])
        {
            return false;
        }
    }
    return true;
}
bool SameSurfaceBasis(const SmSurface& a, const SmSurface& b)
{
    const auto* x = dynamic_cast<const SmBSplineSurface*>(&a);
    const auto* y = dynamic_cast<const SmBSplineSurface*>(&b);
    if (!x || !y || a.GetType() != b.GetType() || x->GetDegree(SM_SP_U) != y->GetDegree(SM_SP_U) || x->GetDegree(SM_SP_V) != y->GetDegree(SM_SP_V) ||
        !SameDomain(a.GetNaturalUVDomain(), b.GetNaturalUVDomain()))
    {
        return false;
    }
    ULONG xu = 0, xv = 0, yu = 0, yv = 0;
    double *px = nullptr, *py = nullptr;
    if (x->GetControlPointsPointer(xu, xv, px) != SM_SUCCESS || y->GetControlPointsPointer(yu, yv, py) != SM_SUCCESS || xu != yu || xv != yv)
    {
        return false;
    }
    for (ULONG i = 0; i < 4 * xu * xv; ++i)
    {
        if (px[i] != py[i])
        {
            return false;
        }
    }
    double *kx = nullptr, *lx = nullptr, *ky = nullptr, *ly = nullptr;
    if (x->GetKnotsPointers(xu, xv, kx, lx) != SM_SUCCESS || y->GetKnotsPointers(yu, yv, ky, ly) != SM_SUCCESS || xu != yu || xv != yv)
    {
        return false;
    }
    for (ULONG i = 0; i < xu; ++i)
    {
        if (kx[i] != ky[i])
        {
            return false;
        }
    }
    for (ULONG i = 0; i < xv; ++i)
    {
        if (lx[i] != ly[i])
        {
            return false;
        }
    }
    return true;
}
bool HasUserAttribute(const SmCurve& curve)
{
    const auto* attribute = curve.FindAttribute(SM_AI_USER_1);
    return attribute && attribute->GetNumLongElements() == 1 && *attribute->GetLongElementsAddress() == 42;
}
bool PlaneParametersAndDerivatives(const SmCrvOnSurf& curve)
{
    if (!SameInterval(curve.GetNaturalInterval(), SmExtent1d(-2., 3.)) || !SameDomain(curve.GetUVDomain(), SmExtent2d(-1., -0.5, 2., 2.5)))
    {
        return false;
    }
    // Independent analytic result: UV(t) = (.25,.5) + .125*t*(1,.5)/sqrt(1.25),
    // S(u,v) = (1+2u, 2+3v, 3). Derivatives are with respect to stored t.
    const SmVector3d tangent(0.25 / std::sqrt(1.25), 0.1875 / std::sqrt(1.25), 0.);
    for (double t : { -1.5, 0., 2.5 })
    {
        SmVector3d derivatives[3];
        const auto expected = SmPoint3d(1.5, 3.5, 3.) + t * tangent;
        if (curve.Evaluate(t, 2, TRUE, derivatives, FALSE) != SM_SUCCESS || derivatives[0].DistanceBetween(expected) > 1e-12 ||
            derivatives[1].DistanceBetween(tangent) > 1e-12 || derivatives[2].Length() > 1e-12)
        {
            return false;
        }
    }
    return true;
}
std::unique_ptr<SmCrvOnSurf> NativeDefinition(SmContext& context)
{
    std::unique_ptr<SmLine> uv(new (context) SmLine(SmPoint3d(0.25, 0.5, 0.), SmVector3d(1., 0.5, 0.), SmExtent1d(-2., 3.), 0.125, 2, &context));
    std::unique_ptr<SmCylinder> surface(new (
        context
    ) SmCylinder(SmPoint3d(1., 2., 3.), SmVector3d(1., 0., 0.), SmVector3d(0., 1., 0.), 3., 0., 180., 4., FALSE, FALSE, FALSE, &context));
    const SmExtent2d domain(0., 0., 1., 1.);
    std::unique_ptr<SmCrvOnSurf> curve(new (context) SmCrvOnSurf(*uv, *surface, &domain, 3));
    uv.release();
    surface.release();
    curve->AddAttribute(new (context) SmLongAttribute(SM_AI_USER_1, 42));
    return curve;
}
bool SameCurveDerivatives(const SmCrvOnSurf& a, const SmCrvOnSurf& b)
{
    if (!SameInterval(a.GetNaturalInterval(), b.GetNaturalInterval()) || !SameDomain(a.GetUVDomain(), b.GetUVDomain()))
    {
        return false;
    }
    for (double t : { -1.5, 0., 2.5 })
    {
        SmVector3d x[3], y[3];
        if (a.Evaluate(t, 2, TRUE, x, FALSE) != SM_SUCCESS || b.Evaluate(t, 2, TRUE, y, FALSE) != SM_SUCCESS)
        {
            return false;
        }
        for (unsigned i = 0; i < 3; ++i)
        {
            if (x[i].DistanceBetween(y[i]) > 1e-12)
            {
                return false;
            }
        }
    }
    return true;
}
std::unique_ptr<SmCrvOnSurf> WrapperDefinition(SmContext& context)
{
    std::unique_ptr<SmLine> uv(new (context) SmLine(SmPoint3d(45., 1.5, 0.), SmVector3d(1., 0.01, 0.), SmExtent1d(-2., 3.), 10., 2, &context));
    auto* cylinder = new (context)
        SmCylinder(SmPoint3d(1., 2., 3.), SmVector3d(1., 0., 0.), SmVector3d(0., 1., 0.), 3., 0., 180., 4., FALSE, FALSE, FALSE, &context);
    std::unique_ptr<SmSTEPSurface> surface(new (context) SmSTEPSurface(*cylinder, TRUE));
    const SmExtent2d domain(10., 0.5, 100., 3.);
    std::unique_ptr<SmCrvOnSurf> curve(new (context) SmCrvOnSurf(*uv, *surface, &domain, 3));
    uv.release();
    surface.release();
    curve->AddAttribute(new (context) SmLongAttribute(SM_AI_USER_1, 42));
    return curve;
}
bool WrapperParametersAndDerivatives(const SmCrvOnSurf& curve)
{
    if (curve.GetType() != SmCrvOnSurf_TYPE || curve.GetUVCurve()->GetType() != SmLine_TYPE ||
        curve.GetBaseSurface()->GetType() != SmSTEPSurface_TYPE || !SameInterval(curve.GetNaturalInterval(), SmExtent1d(-2., 3.)) ||
        !SameDomain(curve.GetUVDomain(), SmExtent2d(10., 0.5, 100., 3.)))
    {
        return false;
    }
    // The wrapped cylinder consumes degrees and length. These closed-form
    // expectations independently check the stored t and the degree chain rule.
    const double uRate = 10. / std::sqrt(1.0001);
    const double vRate = 0.1 / std::sqrt(1.0001);
    const double radiansRate = uRate * std::numbers::pi / 180.;
    for (double t : { -1.5, 0., 2.5 })
    {
        const double angle = (45. + t * uRate) * std::numbers::pi / 180.;
        const double c = std::cos(angle), s = std::sin(angle);
        const SmPoint3d point(1. + 3. * c, 2. + 3. * s, 4.5 + t * vRate);
        const SmVector3d tangent(-3. * s * radiansRate, 3. * c * radiansRate, vRate);
        const SmVector3d second(-3. * c * radiansRate * radiansRate, -3. * s * radiansRate * radiansRate, 0.);
        SmVector3d derivatives[3];
        if (curve.Evaluate(t, 2, TRUE, derivatives, FALSE) != SM_SUCCESS || derivatives[0].DistanceBetween(point) > 1e-11 ||
            derivatives[1].DistanceBetween(tangent) > 1e-11 || derivatives[2].DistanceBetween(second) > 1e-11)
        {
            return false;
        }
    }
    return true;
}
struct Part
{
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> cleanCurves{ &curves };
    SmObjsDelete<SmSurface*> cleanSurfaces{ &surfaces };
    SmObjsDelete<SmBrep*> cleanBreps{ &breps };
};
bool SameBytes(const std::filesystem::path& a, const std::filesystem::path& b)
{
    std::ifstream x(a, std::ios::binary), y(b, std::ios::binary);
    return x && y && std::vector<char>(std::istreambuf_iterator<char>(x), {}) == std::vector<char>(std::istreambuf_iterator<char>(y), {});
}
} // namespace

bool Ownership(unsigned flag)
{
    SmContext sourceContext, copyContext;
    std::unique_ptr<SmLine> uv(new (sourceContext)
                                   SmLine(SmPoint3d(0.25, 0.5, 0.), SmVector3d(1., 0.5, 0.), SmExtent1d(-2., 3.), 0.125, 2, &sourceContext));
    std::unique_ptr<SmPlane> surface(new (
        sourceContext
    ) SmPlane(SmPoint3d(1., 2., 3.), SmVector3d(1., 0., 0.), SmVector3d(0., 1., 0.), SmVector2d(2., 3.), SmExtent2d(-2., -2., 4., 4.), &sourceContext));
    const SmExtent2d domain(-1., -0.5, 2., 2.5);
    std::unique_ptr<SmCrvOnSurf> source(new (sourceContext) SmCrvOnSurf(*uv, *surface, &domain, flag));
    if (flag == 3)
    {
        uv.release();
        surface.release();
    }
    source->AddAttribute(new (sourceContext) SmLongAttribute(SM_AI_USER_1, 42));
    SmCurve* rawCopy = nullptr;
    const auto status = source->Copy(copyContext, rawCopy);
    std::unique_ptr<SmCurve> owner(rawCopy);
    const auto* copy = dynamic_cast<const SmCrvOnSurf*>(rawCopy);
    bool passed = Check("ownership_copy_succeeds", status == SM_SUCCESS && copy);
    if (!copy)
    {
        return false;
    }
    passed &= Check("ownership_copy_preserves_composed_type", copy->GetType() == SmCrvOnSurf_TYPE && !dynamic_cast<const SmBSplineCurve*>(copy));
    passed &= Check("ownership_copy_uses_requested_context", copy->GetContext() == &copyContext);
    passed &= Check("ownership_copy_uv_alias_matches_flag", (copy->GetUVCurve() == source->GetUVCurve()) == (flag == 0));
    passed &= Check("ownership_copy_surface_alias_matches_flag", (copy->GetBaseSurface() == source->GetBaseSurface()) == (flag < 2));
    passed &= Check("ownership_copy_uv_owner_matches_flag", (copy->GetUVCurve()->GetOwner() == copy) == (flag > 0));
    passed &= Check("ownership_copy_surface_owner_matches_flag", (copy->GetBaseSurface()->GetOwner() == copy) == (flag > 1));
    passed &= Check(
        "ownership_copy_owned_children_use_requested_context",
        (flag == 0 || copy->GetUVCurve()->GetContext() == &copyContext) && (flag < 2 || copy->GetBaseSurface()->GetContext() == &copyContext)
    );
    passed &= Check(
        "ownership_copy_preserves_exact_bases",
        SameCurveBasis(*source->GetUVCurve(), *copy->GetUVCurve()) && SameSurfaceBasis(*source->GetBaseSurface(), *copy->GetBaseSurface())
    );
    passed &= Check("ownership_copy_preserves_parameters_and_exact_derivatives", PlaneParametersAndDerivatives(*copy));
    passed &= Check("ownership_copy_preserves_attributes", HasUserAttribute(*copy));
    if (flag == 2)
    {
        uv.reset();
        surface.reset();
    }
    source.reset();
    // Borrowed inputs for flags 0/1 deliberately outlive both curves. Owned
    // inputs for 2/3 have been released along with their source curve.
    passed &= Check("ownership_copy_survives_source_destruction", PlaneParametersAndDerivatives(*copy) && HasUserAttribute(*copy));
    return passed;
}

bool Definition(bool warm)
{
    SmContext sourceContext, copyContext;
    auto source = NativeDefinition(sourceContext);
    bool passed = true;
    if (warm)
    {
        SmBSplineCurve* cache = nullptr;
        passed &= Check("definition_source_cache_materializes", source->GetOrCreateBSApproxPointer(cache, 1e-6) == SM_SUCCESS && cache);
    }
    SmCurve* rawCopy = nullptr;
    const auto status = source->Copy(copyContext, rawCopy);
    std::unique_ptr<SmCurve> owner(rawCopy);
    const auto* copy = dynamic_cast<const SmCrvOnSurf*>(rawCopy);
    passed &= Check("definition_copy_succeeds", status == SM_SUCCESS && copy);
    if (!copy)
    {
        return false;
    }
    passed &= Check("definition_copy_keeps_exact_composed_type", copy->GetType() == SmCrvOnSurf_TYPE && !dynamic_cast<const SmBSplineCurve*>(copy));
    passed &= Check(
        "definition_copy_preserves_uv_and_surface_coefficients",
        SameCurveBasis(*source->GetUVCurve(), *copy->GetUVCurve()) && SameSurfaceBasis(*source->GetBaseSurface(), *copy->GetBaseSurface())
    );
    passed &= Check("definition_copy_preserves_parameters_and_derivatives", SameCurveDerivatives(*source, *copy));
    SmVector3d expected[3], actual[3];
    const bool sourceEvaluates = source->Evaluate(0.25, 2, TRUE, expected, FALSE) == SM_SUCCESS;
    source.reset();
    bool same = sourceEvaluates && copy->Evaluate(0.25, 2, TRUE, actual, FALSE) == SM_SUCCESS;
    for (unsigned i = 0; i < 3 && same; ++i)
    {
        same &= actual[i].DistanceBetween(expected[i]) <= 1e-12;
    }
    passed &= Check("definition_copy_survives_source_and_cache_destruction", same && HasUserAttribute(*copy));
    // Copy may invalidate or rebuild a derived 3D cache. Its coefficients and
    // allocation identity are deliberately not requirements on the definition.
    return passed;
}

bool WrapperArchive(const char* outputDirectory, bool ascii)
{
    const auto directory = std::filesystem::path(outputDirectory);
    std::filesystem::create_directories(directory);
    SmContext sourceContext, copyContext;
    auto source = WrapperDefinition(sourceContext);
    SmCurve* rawCopy = nullptr;
    const auto status = source->Copy(copyContext, rawCopy);
    auto* copy = dynamic_cast<SmCrvOnSurf*>(rawCopy);
    bool passed = Check("wrapper_composed_copy_succeeds", status == SM_SUCCESS && copy);
    const bool independent = copy && copy != source.get() && copy->GetUVCurve() != source->GetUVCurve() &&
                             copy->GetBaseSurface() != source->GetBaseSurface();
    passed &= Check("wrapper_composed_copy_owns_independent_definitions", independent);
    if (!independent)
    {
        // Keep this control safe on the pre-fix DLL: it returns a new UV but
        // the original owned wrapper. Disarm that duplicate owner before any
        // assertion can return and let the source release the wrapper once.
        if (copy && copy != source.get())
        {
            const bool sharedUV = copy->GetUVCurve() == source->GetUVCurve();
            copy->SetOwnerFlag(sharedUV ? 0 : 1);
            const_cast<SmCurve*>(source->GetUVCurve())->SetOwner(source.get());
            const_cast<SmSurface*>(source->GetBaseSurface())->SetOwner(source.get());
            delete copy;
        }
        else if (rawCopy != source.get())
        {
            delete rawCopy;
        }
        return false;
    }
    std::unique_ptr<SmCurve> copyOwner(rawCopy);
    passed &= Check("wrapper_composed_copy_preserves_uv_basis", SameCurveBasis(*source->GetUVCurve(), *copy->GetUVCurve()));
    passed &= Check("wrapper_composed_copy_preserves_exact_parameters_and_derivatives", WrapperParametersAndDerivatives(*copy));
    source.reset();
    passed &= Check("wrapper_composed_copy_survives_source_destruction", WrapperParametersAndDerivatives(*copy) && HasUserAttribute(*copy));
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    curves.Add(copy);
    const auto format = ascii ? SM_ASCII : SM_BINARY;
    const auto path = directory / (ascii ? "wrapper_copy.smt" : "wrapper_copy.smb");
    const bool written = SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, format, TRUE, FALSE) == SM_SUCCESS;
    passed &= Check("wrapper_archive_writes_native_definition", written);
    if (!written)
    {
        return false;
    }
    Part read;
    const bool readOkay = SmNativeArchive::ReadPartFromFile(read.context, path.c_str(), read.curves, read.surfaces, read.trees, read.breps, format) ==
                              SM_SUCCESS &&
                          read.curves.GetSize() == 1 && !read.surfaces.GetSize() && !read.trees.GetSize() && !read.breps.GetSize();
    passed &= Check("wrapper_archive_reads_complete_standalone_inventory", readOkay);
    if (!readOkay)
    {
        return false;
    }
    const auto* readCurve = dynamic_cast<const SmCrvOnSurf*>(read.curves[0]);
    passed &= Check("wrapper_archive_preserves_composed_type", readCurve != nullptr);
    if (!readCurve)
    {
        return false;
    }
    passed &= Check(
        "wrapper_archive_preserves_uv_basis_and_parameters",
        SameCurveBasis(*copy->GetUVCurve(), *readCurve->GetUVCurve(), ascii ? 1e-12 : 0.)
    );
    passed &= Check("wrapper_archive_preserves_exact_wrapper_parameters_and_derivatives", WrapperParametersAndDerivatives(*readCurve));
    passed &= Check("wrapper_archive_preserves_attributes", HasUserAttribute(*readCurve));
    const auto rewrite = directory / (ascii ? "wrapper_copy_rewrite.smt" : "wrapper_copy_rewrite.smb");
    const bool rewritten = SmBrepData::WritePartToFile(rewrite.c_str(), read.curves, read.surfaces, read.trees, read.breps, format, TRUE, FALSE) ==
                           SM_SUCCESS;
    passed &= Check("wrapper_archive_preserves_nested_definition_bytes", rewritten && SameBytes(path, rewrite));
    copyOwner.reset();
    passed &= Check("wrapper_archive_read_survives_copied_definition_destruction", WrapperParametersAndDerivatives(*readCurve));
    return passed;
}
} // namespace curve_copy_controls
