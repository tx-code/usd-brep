// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSupportDomain.h"
#include "StepImportSurfaceProof.h"

#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <array>
#include <cfenv>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#if defined(_M_X64) || defined(__SSE2__)
#    include <xmmintrin.h>
#endif

namespace
{
void Require(bool value, const char* text)
{
    if (!value)
    {
        throw std::runtime_error(text);
    }
}
std::unique_ptr<SmBSplineSurface> Plane(SmContext& context, bool rational, double z = 0)
{
    SmTArray<SmPoint3d> cp;
    for (unsigned u = 0; u < 2; ++u)
    {
        for (unsigned v = 0; v < 2; ++v)
        {
            cp.Add(SmPoint3d(u, v, z));
        }
    }
    SmTArray<ULONG> mult;
    mult.Add(2);
    mult.Add(2);
    SmTArray<double> knots;
    knots.Add(0);
    knots.Add(1);
    SmTArray<double> weights;
    if (rational)
    {
        for (double w : { 2., 3., 4., 5. })
        {
            weights.Add(w);
        }
    }
    SmBSplineSurface* raw = nullptr;
    const auto status = SmBSplineSurface::CreateCanonical(
        context,
        1,
        1,
        cp,
        SM_SF_UNSPECIFIED,
        mult,
        mult,
        knots,
        knots,
        SM_KT_UNSPECIFIED,
        rational ? &weights : nullptr,
        nullptr,
        raw
    );
    std::unique_ptr<SmBSplineSurface> result(raw);
    Require(status == SM_SUCCESS && result, "surface fixture allocation");
    return result;
}
std::unique_ptr<SmBSplineSurface> Restricted(const SmBSplineSurface& source, SmContext& context)
{
    SmSurface* raw = nullptr;
    const auto status = source.Copy(context, raw);
    std::unique_ptr<SmSurface> owned(raw);
    Require(status == SM_SUCCESS && owned, "surface fixture copy");
    auto spline = dynamic_cast<SmBSplineSurface*>(owned.get());
    Require(spline, "B-spline copy type");
    SmExtent2d requested(.25, .125, .75, .875);
    Require(spline->TrimWithDomain(requested) == SM_SUCCESS, "native exact surface restriction");
    owned.release();
    return std::unique_ptr<SmBSplineSurface>(spline);
}
std::unique_ptr<SmBSplineSurface> MultiplePatches(SmContext& context, bool rational)
{
    // Degree (3,2), a genuine interior U knot and nonplanar control net. The
    // retained rectangle crosses that knot, so equality requires two tensor
    // patches and refinement in both directions rather than four corner tests.
    SmTArray<SmPoint3d> cp;
    SmTArray<double> weights, u, v;
    SmTArray<ULONG> um, vm;
    for (unsigned i = 0; i < 5; ++i)
    {
        for (unsigned j = 0; j < 3; ++j)
        {
            cp.Add(SmPoint3d(i / 4., j / 2., (i * i + i * j + j * j) / 16.));
            weights.Add(2. + (i + j) % 3);
        }
    }
    for (double value : { 0., .5, 1. })
    {
        u.Add(value);
    }
    for (ULONG value : { 4UL, 1UL, 4UL })
    {
        um.Add(value);
    }
    v.Add(0.);
    v.Add(1.);
    vm.Add(3);
    vm.Add(3);
    SmBSplineSurface* raw = nullptr;
    const auto status = SmBSplineSurface::
        CreateCanonical(context, 3, 2, cp, SM_SF_UNSPECIFIED, um, vm, u, v, SM_KT_UNSPECIFIED, rational ? &weights : nullptr, nullptr, raw);
    std::unique_ptr<SmBSplineSurface> owned(raw);
    Require(status == SM_SUCCESS && owned, "multiple tensor patch fixture");
    return owned;
}
} // namespace
int main()
{
    try
    {
        SmContext context;
        for (bool rational : { false, true })
        {
            auto source = Plane(context, rational), target = Restricted(*source, context);
            std::size_t callbacks = 0;
            const auto proof = step_import::detail::ProveSurfaceRestriction(
                *source,
                *target,
                1e-10,
                10000,
                100000,
                [&]
                {
                    ++callbacks;
                }
            );
            Require(
                proof.established && proof.maximumMm && *proof.maximumMm <= 1e-10 && proof.patches == 1,
                "whole retained domain, polynomial and positive rational proof"
            );
            Require(callbacks > 0, "proof work has cancellation checkpoints");
            const auto limited = step_import::detail::ProveSurfaceRestriction(
                *source,
                *target,
                1e-10,
                1,
                100000,
                []
                {
                }
            );
            Require(!limited.established && limited.workLimit, "temporary control bound");
            const auto productLimited = step_import::detail::ProveSurfaceRestriction(
                *source,
                *target,
                1e-10,
                10000,
                1,
                []
                {
                }
            );
            Require(!productLimited.established && productLimited.workLimit, "arithmetic work bound");
            bool cancelled = false;
            try
            {
                step_import::detail::ProveSurfaceRestriction(
                    *source,
                    *target,
                    1e-10,
                    10000,
                    100000,
                    []
                    {
                        throw std::runtime_error("surface-proof-cancel");
                    }
                );
            }
            catch (const std::runtime_error& error)
            {
                cancelled = std::string_view(error.what()) == "surface-proof-cancel";
            }
            Require(cancelled, "caller cancellation is not converted into numeric uncertainty");
            auto multiple = MultiplePatches(context, rational), subset = Restricted(*multiple, context);
            const auto multiProof = step_import::detail::ProveSurfaceRestriction(
                *multiple,
                *subset,
                1e-9,
                10000,
                100000,
                []
                {
                }
            );
            Require(
                multiProof.established && multiProof.patches == 2 && multiProof.maximumMm && *multiProof.maximumMm <= 1e-9,
                "nonplanar cubic/quadratic polynomial and rational common-knot proof"
            );
        }
        auto source = Plane(context, false), shifted = Plane(context, false, .01);
        const auto wrong = step_import::detail::ProveSurfaceRestriction(
            *source,
            *shifted,
            .001,
            10000,
            100000,
            []
            {
            }
        );
        Require(!wrong.established && wrong.maximumMm && *wrong.maximumMm > .001, "different geometry is not equivalent");
        auto warped = Plane(context, true);
        const auto wrongParameter = step_import::detail::ProveSurfaceRestriction(
            *source,
            *warped,
            1e-6,
            10000,
            100000,
            []
            {
            }
        );
        Require(!wrongParameter.established, "same planar image does not establish same-parameter equivalence");
        const auto oldRound = std::fegetround();
        Require(std::fesetround(FE_UPWARD) == 0, "rounding control");
        const auto uncertain = step_import::detail::ProveSurfaceRestriction(
            *source,
            *source,
            .001,
            10000,
            100000,
            []
            {
            }
        );
        Require(std::fesetround(oldRound) == 0, "restore rounding control");
        Require(!uncertain.established, "unsupported rounding mode is not a proof");
        const auto changedDuringCallback = step_import::detail::ProveSurfaceRestriction(
            *source,
            *source,
            .001,
            10000,
            100000,
            []
            {
                std::fesetround(FE_DOWNWARD);
            }
        );
        Require(std::fesetround(oldRound) == 0, "restore callback rounding control");
        Require(!changedDuringCallback.established, "callback cannot invalidate outward rounding assumptions");
#if defined(_M_X64) || defined(__SSE2__)
        const auto oldCsr = _mm_getcsr();
        _mm_setcsr(oldCsr | 0x8040); // FTZ and DAZ are outside the proof contract.
        const auto flushed = step_import::detail::ProveSurfaceRestriction(
            *source,
            *source,
            .001,
            10000,
            100000,
            []
            {
            }
        );
        _mm_setcsr(oldCsr);
        Require(!flushed.established, "flushed subnormal arithmetic is not continuous evidence");
#endif
        {
            std::unique_ptr<SmBrep> brep(new (context) SmBrep());
            auto support = Plane(context, false);
            const auto original = support.get();
            SmFace* face = nullptr;
            const auto status = brep->CreateFaceFromSurface(support.get(), SmExtent2d(.25, .125, .75, .875), face);
            if (support->GetOwner() != nullptr)
            {
                support.release();
            }
            Require(status == SM_SUCCESS && face && face->GetSurface() == original, "bounded native face fixture");
            Require(
                original->GetNaturalUVDomain().GetMin().x == 0 && original->GetNaturalUVDomain().GetMax().x == 1,
                "fixture retains unused supporting domain"
            );
            double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
            Require(face->CreateUVTrimCurves(FALSE, nullptr, nullptr, a, b, c, d, e, f) == SM_SUCCESS, "fixture UV creation");
            SmTArray<SmEdgeuse*> uses;
            face->GetUpwardEdgeuses(uses);
            Require(uses.GetSize() == 4, "rectangular loop fixture");
            const auto oldUv = uses[0]->GetUVTrimCurve();
            const auto oldEdge = uses[0]->GetEdge()->GetCurve();
            const auto skipped = step_import::detail::RestrictUnusedSupportDomain(
                *face,
                false,
                1e-9,
                10000,
                100000,
                []
                {
                }
            );
            Require(
                skipped.status == step_import::detail::SupportDomainStatus::NotEligible && face->GetSurface() == original,
                "unestablished outer-region semantics cannot be cropped"
            );
            bool cancelled = false;
            try
            {
                step_import::detail::RestrictUnusedSupportDomain(
                    *face,
                    true,
                    1e-9,
                    10000,
                    100000,
                    []
                    {
                        throw std::runtime_error("support-cancel");
                    }
                );
            }
            catch (const std::runtime_error& error)
            {
                cancelled = std::string_view(error.what()) == "support-cancel";
            }
            Require(
                cancelled && face->GetSurface() == original && uses[0]->GetUVTrimCurve() == oldUv,
                "cancellation before normalization preserves existing owners"
            );
            for (unsigned stop = 2; stop <= 4; ++stop)
            {
                unsigned calls = 0;
                bool stopped = false;
                try
                {
                    step_import::detail::RestrictUnusedSupportDomain(
                        *face,
                        true,
                        1e-9,
                        10000,
                        100000,
                        [&]
                        {
                            if (++calls == stop)
                            {
                                throw std::runtime_error("support-stage-cancel");
                            }
                        }
                    );
                }
                catch (const std::runtime_error& error)
                {
                    stopped = std::string_view(error.what()) == "support-stage-cancel";
                }
                Require(
                    stopped && face->GetSurface() == original && uses[0]->GetUVTrimCurve() == oldUv,
                    "copy, extraction and proof cancellation leave the old face installed"
                );
            }
            const auto proofLimited = step_import::detail::RestrictUnusedSupportDomain(
                *face,
                true,
                1e-9,
                10000,
                1,
                []
                {
                }
            );
            Require(
                proofLimited.status == step_import::detail::SupportDomainStatus::WorkLimit && face->GetSurface() == original,
                "numeric proof budget cannot publish a partly restricted face"
            );
            const auto limited = step_import::detail::RestrictUnusedSupportDomain(
                *face,
                true,
                1e-9,
                1,
                100000,
                []
                {
                }
            );
            Require(
                limited.status == step_import::detail::SupportDomainStatus::WorkLimit && face->GetSurface() == original,
                "control budget refuses before installation"
            );
            const auto normalized = step_import::detail::RestrictUnusedSupportDomain(
                *face,
                true,
                1e-9,
                10000,
                100000,
                []
                {
                }
            );
            Require(
                normalized.status == step_import::detail::SupportDomainStatus::Normalized && normalized.numeric.established,
                "source-parameter preserving support restriction"
            );
            Require(
                face->GetSurface() != original && face->GetSurface()->GetOwner() == face && uses[0]->GetEdge()->GetCurve() == oldEdge,
                "new support is adopted without replacing defining boundary curves"
            );
            for (ULONG i = 0; i < uses.GetSize(); ++i)
            {
                const auto uv = uses[i]->GetUVTrimCurve();
                Require(uv != nullptr, "UV restored after cache invalidation");
                Require(uv->GetOwner() == uses[i] || uv->GetOwner() == uses[i]->GetMate(), "restored UV pair owner");
            }
        }
        for (unsigned stop = 1; stop <= 4; ++stop)
        {
            // Cancel after adopting the new support and before each restored
            // trim. The unpublished BRep may be partial; its destruction and
            // the module's saved UV owners must still unwind independently.
            std::unique_ptr<SmBrep> brep(new (context) SmBrep());
            auto support = Plane(context, false);
            const auto original = support.get();
            SmFace* face = nullptr;
            const auto status = brep->CreateFaceFromSurface(support.get(), SmExtent2d(.25, .125, .75, .875), face);
            if (support->GetOwner())
            {
                support.release();
            }
            Require(status == SM_SUCCESS && face, "partial-install cancellation fixture");
            double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
            Require(face->CreateUVTrimCurves(FALSE, nullptr, nullptr, a, b, c, d, e, f) == SM_SUCCESS, "partial-install UV fixture");
            unsigned installedCallbacks = 0;
            bool stopped = false;
            try
            {
                step_import::detail::RestrictUnusedSupportDomain(
                    *face,
                    true,
                    1e-9,
                    10000,
                    100000,
                    [&]
                    {
                        if (face->GetSurface() != original && ++installedCallbacks == stop)
                        {
                            throw std::runtime_error("partial-install-cancel");
                        }
                    }
                );
            }
            catch (const std::runtime_error& error)
            {
                stopped = std::string_view(error.what()) == "partial-install-cancel";
            }
            Require(stopped && installedCallbacks == stop, "cancellation propagates after partial native installation");
        }
        std::cout << "Surface restriction interval contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
