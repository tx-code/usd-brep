// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmHealData.h>
#include <cstdio>
#include <memory>
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

void Good(SmStatus status)
{
    Require(status == SM_SUCCESS, "Fixture operation failed");
}

constexpr SM_TYPE rejectingSurfaceType = 0x7f183;

// Exercise the SDK's virtual solver error contract. Geometry evaluation and
// topology remain real. Projection retries and the copied-surface fallback
// must see the same failure; an ordinary NURBS copy would mask it.
class RejectingProjection final : public SmBSplineSurface
{
public:

    RejectingProjection(const SmBSplineSurface& surface, unsigned& calls, SmStatus failure)
        : SmBSplineSurface(surface), m_calls(calls), m_failure(failure)
    {
    }

    SmStatus Copy(const SmContext& context, SmSurface*& copy) const override
    {
        copy = new (context) RejectingProjection(*this);
        return SM_SUCCESS;
    }

    SmStatus GlobalPointSolve(
        const SmExtent2d&,
        SmSolverOperationType,
        const SmPoint3d&,
        SmZoneTol3d,
        const double*,
        SmSolutionRequestedType,
        SmSolutionArray&
    ) const override
    {
        ++m_calls;
        return m_failure;
    }

    SM_COMMON(RejectingProjection, SmBSplineSurface, rejectingSurfaceType);

private:

    unsigned& m_calls;
    SmStatus m_failure;
};

SmBoolean RejectingProjection::IsKindOf(SM_TYPE type) const
{
    return type == rejectingSurfaceType || SmBSplineSurface::IsKindOf(type);
}

void RejectingProjection::Dump() const
{
    SmBSplineSurface::Dump(FALSE);
}

std::unique_ptr<SmBrep> Plane(SmContext& context, unsigned& calls, SmStatus failure)
{
    auto brep = std::unique_ptr<SmBrep>(new (context) SmBrep());
    SmBSplineSurface* surface = nullptr;
    Good(SmBSplineSurface::CreateBilinearSurface(context, { 0, 0, 0 }, { 10, 0, 0 }, { 0, 20, 0 }, { 10, 20, 0 }, surface));
    SmFace* face = nullptr;
    Good(brep->CreateFaceFromSurface(surface, surface->GetNaturalUVDomain(), face));
    if (failure != SM_SUCCESS)
    {
        auto* rejecting = new (context) RejectingProjection(*surface, calls, failure);
        Good(brep->ReplaceSurface(face, rejecting, FALSE, nullptr, FALSE));
    }
    Good(face->RemoveUVTrimCurves());
    return brep;
}

SmStatus CreateTrims(SmBrep& brep)
{
    SmTArray<SmFace*> faces;
    brep.GetFaces(faces);
    Require(faces.GetSize() == 1, "Plane face count must stay unchanged");
    double meanSurface = 0, maxSurface = 0, meanVertex = 0, maxVertex = 0, meanUV = 0, maxUV = 0;
    return faces[0]->CreateUVTrimCurves(TRUE, nullptr, nullptr, meanSurface, maxSurface, meanVertex, maxVertex, meanUV, maxUV);
}

void Check(bool requested, SmStatus failure)
{
    // Establish the face-level result on fresh input, without warming or
    // mutating the geometry subsequently passed to the full healer.
    SmContext referenceContext;
    unsigned referenceCalls = 0;
    auto reference = Plane(referenceContext, referenceCalls, failure);
    Require(CreateTrims(*reference) == failure, "Reference face must produce the chosen UV-trim status");
    Require(failure == SM_SUCCESS || referenceCalls != 0, "Reference must reach the failing solver");

    if (failure != SM_SUCCESS)
    {
        SmContext context;
        unsigned calls = 0;
        auto brep = Plane(context, calls, failure);
        auto healer = std::make_unique<SmHealData>();
        // Use the public stage controls to retain the target before loop
        // classification, which can otherwise defer this unsupported face.
        // This is a stage failure regression, not full-pipeline fault coverage.
        Good(healer->HealBrep(brep.get(), SM_HO_FIX_MOVESEAM));
        Require(
            healer->m_sTgtFaceProps.GetSize() == 1 && healer->m_sNotYetFaceProps.GetSize() == 0,
            "UV-trim stage must have a live target, not an excluded unsupported face"
        );
        const auto callsBefore = calls;
        const SmStatus status = healer->Make_UVTrimCurves(brep.get(), requested, SM_HO_MAKE_UVTRIMCURVES);
        std::printf(
            "UV stage requested=%d failure=%d status=%d solver calls=%u\n",
            requested,
            static_cast<int>(failure),
            static_cast<int>(status),
            calls - callsBefore
        );
        Require(status == (requested ? failure : SM_SUCCESS), "UV-trim stage must preserve the face failure");
        Require(requested ? calls > callsBefore : calls == callsBefore, "UV-trim generation must honor the option");
        Require(
            !brep->m_bEditingEnabled && context.GetSysNotifyCallback() == nullptr,
            "UV-trim stage must leave editing and tracking scopes released"
        );
        Good(brep->ValidatePointers());
        Require(CreateTrims(*brep) == failure, "Stage status must not hide a supposedly repaired projection");
        return;
    }

    // Normal full-entry controls cover both option values and edit policies.
    for (bool nativeBrepEntry : { false, true })
    {
        for (SmBoolean originalEditing : { FALSE, TRUE })
        {
            SmContext context;
            unsigned calls = 0;
            auto brep = Plane(context, calls, failure);
            brep->m_bEditingEnabled = originalEditing;
            // SmHealData contains large inline arrays. Keep test sessions off
            // the stack so nested Debug healing fits the default thread stack.
            auto healer = std::make_unique<SmHealData>();
            const SmStatus status = nativeBrepEntry ? brep->HealBrep(SM_HO_ALL, nullptr, nullptr, nullptr, requested) :
                                                      healer->HealBrep(brep.get(), SM_HO_ALL, nullptr, nullptr, nullptr, requested);
            std::printf(
                "UV requested=%d failure=%d Brep entry=%d editing=%d status=%d solver calls=%u\n",
                requested,
                static_cast<int>(failure),
                nativeBrepEntry,
                originalEditing,
                static_cast<int>(status),
                calls
            );
            Require(status == (requested ? failure : SM_SUCCESS), "Healing must preserve the requested UV-trim result");
            Require(brep->m_bEditingEnabled == originalEditing, "Healing must restore editing policy");
            Require(context.GetSysNotifyCallback() == nullptr, "Healing must release its change tracker");
            Good(brep->ValidatePointers());
            if (requested)
            {
                SmTArray<SmFace*> faces;
                brep->GetFaces(faces);
                Require(faces.GetSize() == 1, "Normal healing must retain the plane");
                SmTArray<SmEdgeuse*> edgeuses;
                faces[0]->GetUpwardEdgeuses(edgeuses);
                Require(edgeuses.GetSize() == 4, "Normal plane must retain all boundary uses");
                for (ULONG i = 0; i < edgeuses.GetSize(); ++i)
                {
                    Require(edgeuses[i]->GetUVTrimCurve() != nullptr, "Requested trims must exist before validation can create derived curves");
                }
            }
            Require(brep->AssertValid(nullptr, SM_LEVEL_2, SM_WALK), "Normal healed plane must remain valid");
            Good(brep->HealBrep(SM_HO_ALL, nullptr, nullptr, nullptr, requested));
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Expected failure, disabled or control");
        const std::string_view mode(argv[1]);
        if (mode == "failure")
        {
            Check(true, SM_ERR_NOT_CONVERGING);
            Check(true, SM_ERR_INVALID_INPUT);
        }
        else if (mode == "disabled")
        {
            Check(false, SM_ERR_NOT_CONVERGING);
        }
        else
        {
            Require(mode == "control", "Unknown mode");
            Check(true, SM_SUCCESS);
            Check(false, SM_SUCCESS);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
