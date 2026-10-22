// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmApiHeal.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmFace.h>
#include <SmHealData.h>
#include <SmTrackTopologyChanges.h>
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
constexpr SM_TYPE failingSurfaceType = 0x7f181;
class FailingSurface final : public SmBSplineSurface
{
public:
    explicit FailingSurface(const SmBSplineSurface& surface) : SmBSplineSurface(surface) {}
    unsigned splitCalls = 0;
    SmStatus SplitAt(const SmContext&, double, SmSurfParamType, SmSurface*& left, SmSurface*& right) override
    {
        ++splitCalls;
        left = nullptr;
        right = nullptr;
        return SM_ERR_NOT_CONVERGING;
    }
    SM_COMMON(FailingSurface, SmBSplineSurface, failingSurfaceType);
};
SmBoolean FailingSurface::IsKindOf(SM_TYPE type) const
{
    return type == failingSurfaceType || SmBSplineSurface::IsKindOf(type);
}
void FailingSurface::Dump() const
{
    SmBSplineSurface::Dump(FALSE);
}
constexpr SM_TYPE failingCurveType = 0x7f182;
class FailingCurve final : public SmCrvOnSurf
{
public:
    explicit FailingCurve(const SmCrvOnSurf& curve) : SmCrvOnSurf(curve) {}
    mutable unsigned copyCalls = 0;
    SmStatus Copy(const SmContext&, SmCurve*& copy) const override
    {
        ++copyCalls;
        copy = nullptr;
        return SM_ERR_NOT_CONVERGING;
    }
    SM_COMMON(FailingCurve, SmCrvOnSurf, failingCurveType);
};
SmBoolean FailingCurve::IsKindOf(SM_TYPE type) const
{
    return type == failingCurveType || SmCrvOnSurf::IsKindOf(type);
}
void FailingCurve::Dump() const
{
    SmCrvOnSurf::Dump();
}

// A real quarter-cylinder boundary crossing the seam of its full periodic
// support. Only the public surface-splitting operation fails; classification,
// gap measurement, seam planning and all healer dispatch remain real.
std::unique_ptr<SmBrep> CrossingSeam(SmContext& context, bool rejectSplit, FailingSurface*& failing,
    bool swapUV = false)
{
    auto brep = std::unique_ptr<SmBrep>(new (context) SmBrep());
    SmBSplineSurface* surface = nullptr;
    Good(SmBSplineSurface::CreateConePatch(context, SmAxis2Placement(), 1., 1.,
        45., 135., 1., SM_CO_QUADRATIC, surface));
    if (swapUV)
    {
        Good(surface->SwapUV());
    }
    SmFace* face = nullptr;
    Good(brep->CreateFaceFromSurface(surface, surface->GetNaturalUVDomain(), face));
    SmAxis2Placement frame;
    frame.SetCanonical(SmPoint3d(0, 0, 0), SmVector3d(0, 1, 0), SmVector3d(-1, 0, 0));
    Good(SmBSplineSurface::CreateConePatch(context, frame, 1., 1.,
        0., 360., 1., SM_CO_QUADRATIC, surface));
    if (swapUV)
    {
        Good(surface->SwapUV());
    }
    std::unique_ptr<SmBSplineSurface> support(surface);
    if (rejectSplit)
    {
        failing = new (context) FailingSurface(*surface);
        surface = failing;
    }
    else
    {
        surface = support.release();
    }
    Good(brep->ReplaceSurface(face, surface, FALSE, nullptr, FALSE));
    face->SetIsRectangularTrim(FALSE);
    SmCurveClassification classU, classV;
    SmSurfParamType crossed, missing, near;
    Require(face->HasSeamProblem(classU, classV, crossed, missing, near)
        && crossed == (swapUV ? SM_SP_V : SM_SP_U), "Fixture must cross its periodic support seam");
    return brep;
}

void CheckRefreshFailure()
{
    SmContext context;
    FailingSurface* unused = nullptr;
    auto brep = CrossingSeam(context, false, unused);
    SmHealData healer;
    SmTArray<SmVertex*> vertices;
    SmTArray<SmEdge*> edges;
    SmTArray<SmFace*> noFaces;
    // Complete a session with explicit empty targets, then add a real edited
    // face. This exercises the consumer of mutation notifications and its
    // catch-up sequence, without synthesizing events or editing property lists.
    Good(healer.HealBrep(brep.get(), SM_HO_ALL, &vertices, &edges, &noFaces));
    SmTArray<SmFace*> faces;
    brep->GetFaces(faces);
    Require(faces.GetSize() == 1, "Refresh fixture face count");
    auto* failing = new (context) FailingSurface(*static_cast<SmBSplineSurface*>(faces[0]->GetSurface()));
    SmTrackTopologyChanges changes(brep.get(), TRUE);
    Good(brep->ReplaceSurface(faces[0], failing, FALSE, nullptr, FALSE));
    faces[0]->SetIsRectangularTrim(FALSE);
    changes.StopTracking();
    const auto completed = healer.m_eDoneHealOp;
    const auto requested = healer.m_eStopHealOp;
    const SmStatus status = healer.UpdateTargetLists(changes, SM_HO_FIX_MOVESEAM);
    std::printf("refresh split calls=%u status=%d\n", failing->splitCalls, static_cast<int>(status));
    Require(failing->splitCalls == 1, "Refresh must exercise the real failing seam repair");
    Require(status == SM_ERR, "Refresh must propagate its catch-up failure");
    Require(healer.m_eDoneHealOp == completed && healer.m_eStopHealOp == requested,
        "Refresh must restore the outer stage");
    Require(healer.m_lRecursionCount == 0 && healer.m_lOldFaceCnt == 0
        && healer.m_lOldEdgeCnt == 0 && healer.m_lOldVertexCnt == 0, "Refresh must unwind its recursion state");
    Require(!brep->m_bEditingEnabled && context.GetSysNotifyCallback() == nullptr,
        "Refresh failure must release editing and tracking scopes");
}

void CheckSeamInsertion(bool swapUV, bool rejectCopy)
{
    SmContext context;
    FailingSurface* unused = nullptr;
    auto brep = CrossingSeam(context, false, unused, swapUV);
    SmTArray<SmFace*> faces;
    brep->GetFaces(faces);
    SmCurveClassification classU, classV;
    SmSurfParamType crossed, missing, near;
    Require(faces[0]->HasSeamProblem(classU, classV, crossed, missing, near), "Insertion fixture seam");
    auto& seam = swapUV ? classV : classU;
    Require(seam.GetCurve()->IsKindOf(SmCrvOnSurf_TYPE), "Seam curve must be defined on its support");
    FailingCurve* failing = nullptr;
    if (rejectCopy)
    {
        failing = new (context) FailingCurve(*static_cast<const SmCrvOnSurf*>(seam.GetCurve()));
        seam.SetCurve(failing, TRUE);
    }
    const SmStatus status = faces[0]->SplitAtSeam(classU, classV, crossed, missing, near);
    std::printf("insert seam=%s reject copy=%d status=%d\n", swapUV ? "V" : "U", rejectCopy, static_cast<int>(status));
    if (rejectCopy)
    {
        Require(failing->copyCalls == 1, "Real seam insertion must attempt to copy its defining curve");
        Require(status == SM_ERR_NOT_CONVERGING, "Seam insertion must propagate the failed curve copy");
    }
    else
    {
        Good(status);
        // This low-level helper alone does not establish a fully healed model.
        // The full-healer controls below separately exercise Level 2 validity.
        Good(brep->ValidatePointers());
    }
    Require(!brep->m_bEditingEnabled && !context.GetDoingBooleanRef(), "Seam insertion must restore edit scopes");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--insert-control")
        {
            CheckSeamInsertion(false, false);
            CheckSeamInsertion(true, false);
            return 0;
        }
        for (bool rejectSplit : {true, false})
        {
            for (int entry = 0; entry < 3; ++entry)
            {
                for (SmBoolean originalEditing : {FALSE, TRUE})
                {
                    SmContext context;
                    FailingSurface* failing = nullptr;
                    auto brep = CrossingSeam(context, rejectSplit, failing);
                    brep->m_bEditingEnabled = originalEditing;
                    SmStatus status;
                    if (entry == 0)
                    {
                        SmHealData healer;
                        status = healer.HealBrep(brep.get(), SM_HO_FIX_MOVESEAM);
                    }
                    else if (entry == 1)
                    {
                        status = brep->HealBrep(SM_HO_FIX_MOVESEAM);
                    }
                    else
                    {
                        status = SmApiHealBrep(brep.get());
                    }
                    std::printf("reject split=%d entry=%d editing=%d status=%d\n", rejectSplit,
                        entry, originalEditing, static_cast<int>(status));
                    if (rejectSplit)
                    {
                        Require(failing->splitCalls == 1, "Stop after the first failed seam split");
                        // MoveSeam reports SM_ERR when its surface split fails.
                        Require(status == SM_ERR, "Healing must propagate the failed seam repair status");
                    }
                    else
                    {
                        Good(status);
                        Require(brep->AssertValid(nullptr, SM_LEVEL_2, SM_WALK), "Healed control must remain valid");
                        Good(SmApiHealBrep(brep.get()));
                    }
                    Require(brep->m_bEditingEnabled == originalEditing, "Healing must restore editing policy");
                    Require(context.GetSysNotifyCallback() == nullptr, "Healing must release its change tracker");
                    Good(brep->ValidatePointers());
                }
            }
        }
        CheckRefreshFailure();
        for (bool rejectCopy : {true, false})
        {
            CheckSeamInsertion(false, rejectCopy);
            CheckSeamInsertion(true, rejectCopy);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
