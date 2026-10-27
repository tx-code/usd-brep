// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportOrientationRepair.h"

#include "StepImportConicalApexSession.h"
#include "StepImportExecution.h"
#include "StepImportNativeData.h"
#include "StepImportSurfaceProof.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmCone.h>
#include <SmCrvOnSurf.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmEllipse.h>
#include <SmFace.h>
#include <SmLine.h>
#include <SmLoop.h>
#include <SmPlane.h>
#include <SmSTEPSurface.h>
#include <SmSphere.h>
#include <SmTol.h>
#include <SmTorus.h>
#include <SmVertex.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>
namespace step_import::detail
{
namespace
{
[[nodiscard]] Result<void> Need(bool value, const char* key, std::optional<std::size_t> record = {})
{
    if (!value)
    {
        return tl::unexpected(Failure{ Status::Indeterminate, key, "Explicit face-orientation repair evidence is unestablished", record, "Repair" });
    }
    return {};
}
[[nodiscard]] Result<void> Same(double a, double b)
{
    if (auto businessCheck = Need(std::isfinite(a) && std::isfinite(b) && a == b, "RepairChangedDefinitionOrTolerance"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return {};
}
[[nodiscard]] Result<void> SameFrame(const SmAxis2Placement& a, const SmAxis2Placement& b)
{
    if (auto businessCheck = Same(a.GetOriginRef().x, b.GetOriginRef().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetOriginRef().y, b.GetOriginRef().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetOriginRef().z, b.GetOriginRef().z); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetXAxisRef().x, b.GetXAxisRef().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetXAxisRef().y, b.GetXAxisRef().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetXAxisRef().z, b.GetXAxisRef().z); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetYAxisRef().x, b.GetYAxisRef().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetYAxisRef().y, b.GetYAxisRef().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetYAxisRef().z, b.GetYAxisRef().z); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return {};
}
[[nodiscard]] Result<void> SameSurface(const SmSurface& a, const SmSurface& b);
[[nodiscard]] Result<void> SameCurve(const SmCurve& a, const SmCurve& b)
{
    if (auto businessCheck = Need(a.GetType() == b.GetType() && a.GetDim() == b.GetDim() && a.GetDegree() == b.GetDegree(), "RepairChangedCurveClass");
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (const auto* first = dynamic_cast<const SmCrvOnSurf*>(&a))
    {
        const auto* second = dynamic_cast<const SmCrvOnSurf*>(&b);
        if (auto checked = Need(
                second && first->GetUVCurve() && second->GetUVCurve() && first->GetBaseSurface() && second->GetBaseSurface(),
                "RepairChangedComposedDefinition"
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        // A composed defining curve is S(Cuv(t)), never its cached 3D fit.
        // Compare the whole UV definition, owned carrier definition, work
        // precision and both stored parameter domains. This does not authorize
        // any repair or geometric replacement; callers still qualify ownership.
        if (auto checked = SameCurve(*first->GetUVCurve(), *second->GetUVCurve()); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto checked = SameSurface(*first->GetBaseSurface(), *second->GetBaseSurface()); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const auto x = first->GetUVDomain(), y = second->GetUVDomain();
        for (const auto pair : { std::pair{ x.GetMin().x, y.GetMin().x },
                                 std::pair{ x.GetMin().y, y.GetMin().y },
                                 std::pair{ x.GetMax().x, y.GetMax().x },
                                 std::pair{ x.GetMax().y, y.GetMax().y },
                                 std::pair{ a.GetNaturalInterval().GetMin(), b.GetNaturalInterval().GetMin() },
                                 std::pair{ a.GetNaturalInterval().GetMax(), b.GetNaturalInterval().GetMax() },
                                 std::pair{ a.GetSTEPInterval().GetMin(), b.GetSTEPInterval().GetMin() },
                                 std::pair{ a.GetSTEPInterval().GetMax(), b.GetSTEPInterval().GetMax() } })
        {
            if (auto checked = Same(pair.first, pair.second); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
        double before = 0, after = 0;
        if (auto checked = Need(
                first->GetSupportWorkTolerance(before) == SM_SUCCESS && second->GetSupportWorkTolerance(after) == SM_SUCCESS &&
                    first->OwnsBaseSurface() == second->OwnsBaseSurface(),
                "RepairChangedComposedWorkPrecision"
            );
            !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        return Same(before, after);
    }
    const auto first = dynamic_cast<const SmBSplineCurve*>(&a), second = dynamic_cast<const SmBSplineCurve*>(&b);
    if (auto businessCheck = Need(first && second, "RepairCurveIdentityScope"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    ULONG ac = 0, bc = 0;
    double *ap = nullptr, *bp = nullptr;
    if (auto businessCheck = Need(
            first->GetControlPointsPointer(ac, ap) == SM_SUCCESS && second->GetControlPointsPointer(bc, bp) == SM_SUCCESS && ac == bc,
            "RepairChangedCurveNet"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    for (ULONG i = 0; i < ac * 4; ++i)
    {
        if (auto businessCheck = Same(ap[i], bp[i]); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    SmTArray<double> ak, bk;
    if (auto businessCheck = Need(
            first->GetKnotsAll(ak) == SM_SUCCESS && second->GetKnotsAll(bk) == SM_SUCCESS && ak.GetSize() == bk.GetSize(),
            "RepairChangedCurveKnots"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    for (ULONG i = 0; i < ak.GetSize(); ++i)
    {
        if (auto businessCheck = Same(ak[i], bk[i]); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    if (auto businessCheck = Same(a.GetNaturalInterval().GetMin(), b.GetNaturalInterval().GetMin()); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetNaturalInterval().GetMax(), b.GetNaturalInterval().GetMax()); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetSTEPInterval().GetMin(), b.GetSTEPInterval().GetMin()); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(a.GetSTEPInterval().GetMax(), b.GetSTEPInterval().GetMax()); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (const auto line = dynamic_cast<const SmLine*>(&a))
    {
        const auto other = dynamic_cast<const SmLine*>(&b);
        if (auto businessCheck = Need(other != nullptr, "RepairChangedAnalyticClass"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLinePoint().x, other->GetLinePoint().x); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLinePoint().y, other->GetLinePoint().y); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLinePoint().z, other->GetLinePoint().z); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLineVector().x, other->GetLineVector().x); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLineVector().y, other->GetLineVector().y); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLineVector().z, other->GetLineVector().z); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(line->GetLineScale(), other->GetLineScale()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Need(line->GetInsideOut() == other->GetInsideOut(), "RepairChangedAnalyticSense"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    if (const auto ellipse = dynamic_cast<const SmEllipse*>(&a))
    {
        const auto other = dynamic_cast<const SmEllipse*>(&b);
        if (auto businessCheck = Need(other != nullptr && ellipse->GetInsideOut() == other->GetInsideOut(), "RepairChangedAnalyticSense");
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = SameFrame(ellipse->GetPosition(), other->GetPosition()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(ellipse->GetXRadius(), other->GetXRadius()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(ellipse->GetYRadius(), other->GetYRadius()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    return {};
}
[[nodiscard]] Result<void> SameSurface(const SmSurface& a, const SmSurface& b)
{
    if (auto businessCheck = Need(a.GetType() == b.GetType(), "RepairChangedSurfaceClass"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (const auto* first = dynamic_cast<const SmSTEPSurface*>(&a))
    {
        const auto* second = dynamic_cast<const SmSTEPSurface*>(&b);
        if (auto checked = Need(second && first->GetBaseSurface() && second->GetBaseSurface(), "RepairChangedStepSurface"); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        // Preserve the STEP evaluator wrapper as well as its canonical surface;
        // equal displayed shapes cannot stand in for either stored definition.
        const auto an = a.GetNaturalUVDomain(), bn = b.GetNaturalUVDomain(), as = a.GetSTEPUVDomain(), bs = b.GetSTEPUVDomain();
        for (const auto pair : { std::pair{ an.GetMin().x, bn.GetMin().x },
                                 std::pair{ an.GetMin().y, bn.GetMin().y },
                                 std::pair{ an.GetMax().x, bn.GetMax().x },
                                 std::pair{ an.GetMax().y, bn.GetMax().y },
                                 std::pair{ as.GetMin().x, bs.GetMin().x },
                                 std::pair{ as.GetMin().y, bs.GetMin().y },
                                 std::pair{ as.GetMax().x, bs.GetMax().x },
                                 std::pair{ as.GetMax().y, bs.GetMax().y } })
        {
            if (auto checked = Same(pair.first, pair.second); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
        }
        return SameSurface(*first->GetBaseSurface(), *second->GetBaseSurface());
    }
    const auto first = dynamic_cast<const SmBSplineSurface*>(&a), second = dynamic_cast<const SmBSplineSurface*>(&b);
    if (auto businessCheck = Need(
            first && second && first->GetDegree(SM_SP_U) == second->GetDegree(SM_SP_U) && first->GetDegree(SM_SP_V) == second->GetDegree(SM_SP_V),
            "RepairSurfaceIdentityScope"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    ULONG au = 0, av = 0, bu = 0, bv = 0;
    double *ap = nullptr, *bp = nullptr;
    if (auto businessCheck = Need(
            first->GetControlPointsPointer(au, av, ap) == SM_SUCCESS && second->GetControlPointsPointer(bu, bv, bp) == SM_SUCCESS && au == bu &&
                av == bv,
            "RepairChangedSurfaceNet"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    for (ULONG i = 0; i < au * av * 4; ++i)
    {
        if (auto businessCheck = Same(ap[i], bp[i]); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    double *ak = nullptr, *al = nullptr, *bk = nullptr, *bl = nullptr;
    if (auto businessCheck = Need(
            first->GetKnotsPointers(au, av, ak, al) == SM_SUCCESS && second->GetKnotsPointers(bu, bv, bk, bl) == SM_SUCCESS && au == bu && av == bv,
            "RepairChangedSurfaceKnots"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    for (ULONG i = 0; i < au; ++i)
    {
        if (auto businessCheck = Same(ak[i], bk[i]); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    for (ULONG i = 0; i < av; ++i)
    {
        if (auto businessCheck = Same(al[i], bl[i]); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    const auto ad = a.GetNaturalUVDomain(), bd = b.GetNaturalUVDomain();
    if (auto businessCheck = Same(ad.GetMin().x, bd.GetMin().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(ad.GetMax().x, bd.GetMax().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(ad.GetMin().y, bd.GetMin().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(ad.GetMax().y, bd.GetMax().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    const auto as = a.GetSTEPUVDomain(), bs = b.GetSTEPUVDomain();
    if (auto businessCheck = Same(as.GetMin().x, bs.GetMin().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(as.GetMin().y, bs.GetMin().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(as.GetMax().x, bs.GetMax().x); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = Same(as.GetMax().y, bs.GetMax().y); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (const auto plane = dynamic_cast<const SmPlane*>(&a))
    {
        const auto other = dynamic_cast<const SmPlane*>(&b);
        if (auto businessCheck = Need(other != nullptr, "RepairChangedAnalyticClass"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = SameFrame(plane->GetPosition(), other->GetPosition()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(plane->GetUVScale().x, other->GetUVScale().x); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(plane->GetUVScale().y, other->GetUVScale().y); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    const auto comparePolar = [&](const auto* polar, const auto* other) -> Result<void>
    {
        if (!polar)
        {
            return {};
        }
        if (auto businessCheck = Need(other != nullptr, "RepairChangedAnalyticClass"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        SmAxis2Placement x, y;
        double r1 = 0, r2 = 0, s1 = 0, s2 = 0;
        SmBoolean swapX = FALSE, swapY = FALSE, reverseX = FALSE, reverseY = FALSE;
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<decltype(polar)>>, SmSphere>)
        {
            if (auto businessCheck = Need(
                    polar->GetCanonical(x, r1, &swapX, &reverseX) == SM_SUCCESS && other->GetCanonical(y, r2, &swapY, &reverseY) == SM_SUCCESS,
                    "RepairAnalyticDefinitionUnavailable"
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        else
        {
            if (auto businessCheck = Need(
                    polar->GetCanonical(x, r1, s1, &swapX, &reverseX) == SM_SUCCESS &&
                        other->GetCanonical(y, r2, s2, &swapY, &reverseY) == SM_SUCCESS,
                    "RepairAnalyticDefinitionUnavailable"
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto businessCheck = Same(s1, s2); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        if (auto businessCheck = SameFrame(x, y); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(r1, r2); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Need(swapX == swapY && reverseX == reverseY, "RepairChangedAnalyticSense"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        return {};
    };
    if (auto businessCheck = comparePolar(dynamic_cast<const SmCone*>(&a), dynamic_cast<const SmCone*>(&b)); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = comparePolar(dynamic_cast<const SmSphere*>(&a), dynamic_cast<const SmSphere*>(&b)); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    if (auto businessCheck = comparePolar(dynamic_cast<const SmTorus*>(&a), dynamic_cast<const SmTorus*>(&b)); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return {};
}
[[nodiscard]] Result<void> CompareDefinitions(
    const ModelData& before,
    const ModelData& after,
    FaceOrientationRepairHistory& history,
    Report& report,
    Execution& execution
)
{
    if (auto businessCheck = Need(
            before.source == after.source && before.vertices.size() == after.vertices.size() && before.edges.size() == after.edges.size() &&
                before.faces.size() == after.faces.size(),
            "RepairChangedSelectedInventory"
        );
        !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    const auto compare =
        [&](const auto& oldSources, const auto& newSources, const auto& oldObjects, const auto& newObjects, const auto& inspect) -> Result<void>
    {
        if (auto businessCheck = Need(
                oldSources.size() == oldObjects.size() && newSources.size() == newObjects.size(),
                "RepairMissingSourceInventory"
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        std::map<std::size_t, std::size_t> targets;
        for (std::size_t i = 0; i < newSources.size(); ++i)
        {
            if (auto businessCheck = Need(targets.emplace(newSources[i], i).second, "RepairRepeatedSourceInventory"); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        for (std::size_t i = 0; i < oldSources.size(); ++i)
        {
            const auto found = targets.find(oldSources[i]);
            if (auto businessCheck = Need(found != targets.end(), "RepairLostSourceIdentity"); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto operation = execution.ChargeNative(report, oldSources[i]); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            const auto a = oldObjects[i], b = newObjects[found->second];
            if (auto inspected = inspect(a, b); !inspected)
            {
                return tl::unexpected(std::move(inspected.error()));
            }
            if (auto businessCheck = Same(SmTol::GetZoneTol3d(a), SmTol::GetZoneTol3d(b)); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        return {};
    };
    if (auto proof = compare(
            before.repairVertexSources,
            after.repairVertexSources,
            before.vertices,
            after.vertices,
            [](const auto a, const auto b) -> Result<void>
            {
                const auto x = a->GetPoint(), y = b->GetPoint();
                if (auto businessCheck = Same(x.x, y.x); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                if (auto businessCheck = Same(x.y, y.y); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                if (auto businessCheck = Same(x.z, y.z); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                return {};
            }
        );
        !proof)
    {
        return tl::unexpected(std::move(proof.error()));
    }
    if (auto proof = compare(
            before.repairEdgeSources,
            after.repairEdgeSources,
            before.edges,
            after.edges,
            [](const auto a, const auto b) -> Result<void>
            {
                if (auto businessCheck = Same(a->GetInterval().GetMin(), b->GetInterval().GetMin()); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                if (auto businessCheck = Same(a->GetInterval().GetMax(), b->GetInterval().GetMax()); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                if (auto businessCheck = SameCurve(*a->GetCurve(), *b->GetCurve()); !businessCheck)
                {
                    return tl::unexpected(std::move(businessCheck.error()));
                }
                return {};
            }
        );
        !proof)
    {
        return tl::unexpected(std::move(proof.error()));
    }
    for (std::size_t i = 0; i < before.faces.size(); ++i)
    {
        if (auto operation = execution.ChargeNative(report, before.repairVertexSources.front()); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const auto& a = before.faces[i];
        const auto& b = after.faces[i];
        if (auto businessCheck = Need(a.faceId == b.faceId, "RepairChangedFaceIdentity"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = SameSurface(*a.face->GetSurface(), *b.face->GetSurface()); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Same(SmTol::GetZoneTol3d(a.face), SmTol::GetZoneTol3d(b.face)); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
    }
    if (auto businessCheck = Same(SmTol::GetZoneTol3d(before.brep.get()), SmTol::GetZoneTol3d(after.brep.get())); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    history.verticesCompared = before.vertices.size();
    history.edgesCompared = before.edges.size();
    history.surfacesCompared = before.faces.size();
    history.definitionsIdentical = history.workingTolerancesIdentical = true;
    return {};
}
} // namespace

[[nodiscard]] Result<void> RequireSameNativeCurveDefinition(const SmCurve& before, const SmCurve& after)
{
    if (auto businessCheck = SameCurve(before, after); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return {};
}
[[nodiscard]] Result<void> RequireSameNativeSurfaceDefinition(const SmSurface& before, const SmSurface& after)
{
    if (auto businessCheck = SameSurface(before, after); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    return {};
}
[[nodiscard]] Result<void> ValidateRepairTargets(const step_semantics::Outcome& checked, const Options& options, Execution& execution)
{
    if (auto* apex = execution.ConicalApexRepair())
    {
        if (auto bound = apex->ValidateTargets(checked, execution); !bound)
        {
            return tl::unexpected(std::move(bound.error()));
        }
    }
    if (!options.faceOrientationRepair)
    {
        return {};
    }
    using Body = std::pair<std::shared_ptr<const step_semantics::Snapshot>, step_semantics::Part::Shape>;
    using Key = std::pair<std::string, std::string>;
    std::map<Key, Body> bodies;
    const auto include = [&](const std::shared_ptr<const step_semantics::Snapshot>& part) -> Result<void>
    {
        const auto& roles = part->PartDefinition().roles;
        const auto role = std::find_if(
            roles.begin(),
            roles.end(),
            [](const auto& r)
            {
                return r.name == "definition";
            }
        );
        if (auto businessCheck = Need(role != roles.end(), "RepairDefinitionIdentityMissing"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto& records = part->Source().Records();
        for (const auto& shape : part->PartDefinition().shapes)
        {
            if (!bodies.emplace(Key{ records[role->recordIndex].id, records[shape.shape].id }, Body{ part, shape }).second)
            {
                return tl::unexpected(
                    Failure{ Status::Unsupported,
                             "RepairBodyScopeAmbiguous",
                             "Repair body identity is not unique in the selected request",
                             shape.shape,
                             "Repair" }
                );
            }
        }
        return {};
    };
    if (checked.document)
    {
        for (std::size_t i = 0; i < checked.document->PartCount(); ++i)
        {
            if (auto included = include(checked.document->Part(i)); !included)
            {
                return tl::unexpected(std::move(included.error()));
            }
        }
    }
    else
    {
        if (auto included = include(checked.snapshot); !included)
        {
            return tl::unexpected(std::move(included.error()));
        }
    }
    std::map<Key, std::map<std::string, std::size_t>> faces;
    for (const auto& edit : options.faceOrientationRepair->edits)
    {
        if (auto operation = execution.Check(Phase::SemanticsChecked); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        const Key key{ edit.definitionId, edit.shapeId };
        const auto body = bodies.find(key);
        if (body == bodies.end())
        {
            return tl::unexpected(Failure{ Status::Unsupported, "RepairTargetOutsideSelectedRequest", "Repair target is not selected", {}, "Repair" });
        }
        const auto& source = body->second.first;
        if (body->second.second.role != "MaterialSolid")
        {
            return tl::unexpected(
                Failure{ Status::Unsupported,
                         "RepairRoleUnsupported",
                         "Orientation repair cannot upgrade a source surface role",
                         body->second.second.shape,
                         "Repair" }
            );
        }
        auto map = faces.find(key);
        if (map == faces.end())
        {
            std::map<std::string, std::size_t> members;
            for (const auto face : source->Field(body->second.second.shell, "cfs_faces").ReferenceTargets())
            {
                members.emplace(source->Source().Records()[face].id, face);
            }
            map = faces.emplace(key, std::move(members)).first;
        }
        const auto found = map->second.find(edit.faceId);
        if (found == map->second.end())
        {
            return tl::unexpected(Failure{ Status::Unsupported, "RepairTargetOutsideSelectedRequest", "Repair face is not selected", {}, "Repair" });
        }
        const auto face = found->second;
        if ((source->Field(face, "same_sense").Value().text == "T") != edit.expectedSourceSameSense)
        {
            return tl::unexpected(
                Failure{ Status::Unsupported, "RepairSourceSenseMismatch", "Repair expectation differs from immutable source", face, "Repair" }
            );
        }
        const auto bounds = source->Field(face, "bounds").ReferenceTargets();
        if (bounds.size() != 1 || !source->IsA(bounds[0], "FACE_OUTER_BOUND") ||
            !source->IsA(source->Field(face, "face_geometry").ReferenceTarget(), "B_SPLINE_SURFACE_WITH_KNOTS"))
        {
            return tl::unexpected(
                Failure{ Status::Unsupported,
                         "RepairFaceProfileUnsupported",
                         "Repair requires a single explicit outer loop on a B-spline support",
                         face,
                         "Repair" }
            );
        }
    }
    return {};
}

[[nodiscard]] Result<NativeCandidate> FinishConstructionAttempt(ConstructionResult<NativeCandidate> attempt)
{
    if (attempt)
    {
        return std::move(*attempt);
    }
    if (auto failure = std::get_if<Failure>(&attempt.error()))
    {
        return tl::unexpected(std::move(*failure));
    }
    if (auto failure = std::get_if<ConicalApexFailureCandidate>(&attempt.error()))
    {
        failure->model.reset();
        return tl::unexpected(std::move(failure->failure));
    }
    auto witness = std::get<OrientationFailureCandidate>(std::move(attempt.error()));
    // Ordinary construction cannot publish a retained strict-attempt witness.
    // Destroy it before the returned plain failure can reach report serialization.
    witness.model.reset();
    return tl::unexpected(std::move(witness.failure));
}
[[nodiscard]] Result<std::shared_ptr<ModelData>> BuildWithOrientationRepair(
    std::shared_ptr<const step_semantics::Snapshot> source,
    std::optional<step_semantics::Part::Shape> binding,
    const Options& options,
    Report& report,
    Execution& execution,
    const GeneralRepairBuild& build
)
{
    if (!options.faceOrientationRepair)
    {
        return FinishConstructionAttempt(build(report, false, {}));
    }
    const auto& request = *options.faceOrientationRepair;
    const auto& part = source->PartDefinition();
    const auto role = std::find_if(
        part.roles.begin(),
        part.roles.end(),
        [](const auto& r)
        {
            return r.name == "definition";
        }
    );
    if (auto businessCheck = Need(role != part.roles.end(), "RepairDefinitionIdentityMissing"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    const auto& records = source->Source().Records();
    const auto definition = records.at(role->recordIndex).id;
    const auto shape = binding ? *binding : part.shapes.at(0);
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < request.edits.size(); ++i)
    {
        if (request.edits[i].definitionId == definition && request.edits[i].shapeId == records.at(shape.shape).id)
        {
            indices.push_back(i);
        }
    }
    if (indices.empty())
    {
        return FinishConstructionAttempt(build(report, false, {}));
    }
    const auto sourceFaces = source->Field(shape.shell, "cfs_faces").ReferenceTargets();
    FaceSenseOverrides overrides;
    for (const auto index : indices)
    {
        const auto& edit = request.edits[index];
        const auto face = std::find_if(
            sourceFaces.begin(),
            sourceFaces.end(),
            [&](auto r)
            {
                return records.at(r).id == edit.faceId;
            }
        );
        if (auto businessCheck = Need(face != sourceFaces.end(), "RepairFaceOutsideSelectedBody"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        if (auto businessCheck = Need(
                (source->Field(*face, "same_sense").Value().text == "T") == edit.expectedSourceSameSense,
                "RepairSourceSenseMismatch",
                *face
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto bounds = source->Field(*face, "bounds").ReferenceTargets();
        if (auto businessCheck = Need(
                bounds.size() == 1 && source->IsA(bounds[0], "FACE_OUTER_BOUND"),
                "RepairRequiresSingleExplicitOuterBound",
                *face
            );
            !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto surface = source->Field(*face, "face_geometry").ReferenceTarget();
        if (auto businessCheck = Need(source->IsA(surface, "B_SPLINE_SURFACE_WITH_KNOTS"), "RepairSurfaceProfileUnsupported", *face); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        overrides.emplace_back(*face, edit.targetSameSense);
    }
    auto original = std::make_shared<Report>();
    original->generalBrep = true;
    original->encoding = report.encoding;
    original->sourceEvidenceScope = "RepairParent";
    original->source = report.source;
    original->sourceSha256 = report.sourceSha256;
    original->sourceSchema = report.sourceSchema;
    original->numericalIntegration = report.numericalIntegration;
    report.originalAttempt = original;
    report.repairStage = "StrictCandidate";
    OrientationFailureCandidate witness;
    const auto firstCall = execution.NativeCalls();
    auto strict = build(*original, true, {});
    if (strict)
    {
        original->candidateDiscarded = true;
        report.originalAttemptStatus = HasAppliedSourceInterpretation(*source) ? Status::AcceptedUnderInterpretation : Status::Accepted;
        return tl::unexpected(
            Failure{ Status::Unsupported,
                     "RepairNotRequired",
                     "The requested repair cannot alter an already accepted candidate",
                     shape.shape,
                     "Repair" }
        );
    }
    if (auto failed = std::get_if<OrientationFailureCandidate>(&strict.error()))
    {
        witness = std::move(*failed);
        report.originalAttemptStatus = witness.failure.status;
        original->candidateDiscarded = true;
        AddFailureDiagnostic(*original, witness.failure);
    }
    else
    {
        auto failure = std::get<Failure>(std::move(strict.error()));
        if (failure.nativeKind == NativeFailureKind::ConstructionOperation || failure.nativeKind == NativeFailureKind::SourceValueConversion)
        {
            // These operation refusals used to leave through an exception.
            // Retain the partial observation as it stands: no completed strict
            // status, diagnostic or discard receipt was added by this layer.
            // Contract/execution failures below keep their existing observation.
            return tl::unexpected(std::move(failure));
        }
        original->candidateDiscarded = original->nativeCalls > 0;
        report.originalAttemptStatus = failure.status;
        AddFailureDiagnostic(*original, failure);
        return tl::unexpected(std::move(failure));
    }
    report.repairStage = "EvidenceBefore";
    if (auto operation = execution.Check(Phase::BeforeRepairProof); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    FaceOrientationRepairHistory history;
    history.sourceSha256 = report.sourceSha256;
    history.originalDiagnostics = original->diagnostics;
    history.sourceInterpretationApplied = HasAppliedSourceInterpretation(*source);
    if (auto businessCheck = Need(witness.targetFaces.size() == indices.size(), "RepairPlanDoesNotMatchFailureSet"); !businessCheck)
    {
        return tl::unexpected(std::move(businessCheck.error()));
    }
    const double numerator = std::nextafter(static_cast<double>(options.quality.derivedUvMm.Numerator()), 0.);
    const double denominator = std::nextafter(static_cast<double>(options.quality.derivedUvMm.Denominator()), std::numeric_limits<double>::infinity());
    const double allowed = std::nextafter(numerator / denominator, 0.);
    const auto proveLifts =
        [&](SmFace* native, const SmBSplineSurface* surface, std::size_t record, FaceOrientationRepairAction& action) -> Result<void>
    {
        const auto checkpoint = [&]
        {
            return execution.ChargeNative(report, record);
        };
        SmTArray<SmEdgeuse*> uses;
        native->GetUpwardEdgeuses(uses);
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            const auto interval = uses[i]->GetEdge()->GetInterval();
            const auto uv = uses[i]->GetUVTrimCurve();
            if (auto businessCheck = Need(uv != nullptr, "RepairMissingDerivedTrim", record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (!execution.RepairProofRemaining())
            {
                return tl::unexpected(
                    Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Cumulative repair proof work exhausted", record, "Repair" }
                );
            }
            auto liftResult = ProveCurveLift(
                *uses[i]->GetEdge()->GetCurve(),
                *uv,
                *surface,
                interval.GetMin(),
                interval.GetMax(),
                allowed,
                request.maximumProofControls,
                execution.RepairProofRemaining(),
                checkpoint
            );
            if (!liftResult)
            {
                return tl::unexpected(std::move(liftResult.error()));
            }
            const auto lift = std::move(*liftResult);
            if (auto operation = execution.SpendRepairProof(lift.productsUsed); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (lift.workLimit)
            {
                return tl::unexpected(
                    Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Repair lift proof exhausted its work", record, "Repair" }
                );
            }
            if (auto businessCheck = Need(lift.established, "RepairContinuousLiftUnestablished", record); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            action.continuousLiftBoundMm = std::max(action.continuousLiftBoundMm, *lift.maximumMm);
            action.liftCells += lift.cells;
        }
        return {};
    };
    for (const auto index : indices)
    {
        const auto& edit = request.edits[index];
        const auto found = std::find_if(
            witness.targetFaces.begin(),
            witness.targetFaces.end(),
            [&](auto i)
            {
                return witness.model->faces.at(i).faceId == edit.faceId;
            }
        );
        if (auto businessCheck = Need(found != witness.targetFaces.end(), "RepairPlanDoesNotMatchFailureSet"); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto record = std::find_if(
            sourceFaces.begin(),
            sourceFaces.end(),
            [&](auto i)
            {
                return records.at(i).id == edit.faceId;
            }
        );
        auto native = witness.model->faces.at(*found).face;
        const auto surface = dynamic_cast<const SmBSplineSurface*>(native->GetSurface());
        SmTArray<SmLoop*> nativeLoops;
        native->GetLoops(nativeLoops);
        if (auto businessCheck = Need(surface && nativeLoops.GetSize() == 1, "RepairNativeFaceProfileUnsupported", *record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        const auto checkpoint = [&]
        {
            return execution.ChargeNative(report, *record);
        };
        if (!execution.RepairProofRemaining())
        {
            return tl::unexpected(
                Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Cumulative repair proof work exhausted", *record, "Repair" }
            );
        }
        auto uniqueResult = ProveSurfaceInjectivity(*surface, request.maximumProofControls, execution.RepairProofRemaining(), checkpoint);
        if (!uniqueResult)
        {
            return tl::unexpected(std::move(uniqueResult.error()));
        }
        const auto unique = std::move(*uniqueResult);
        if (auto operation = execution.SpendRepairProof(unique.productsUsed); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (unique.workLimit)
        {
            return tl::unexpected(
                Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Repair injectivity proof exhausted its workspace", *record, "Repair" }
            );
        }
        if (auto businessCheck = Need(unique.established, "RepairSupportInjectivityUnestablished", *record); !businessCheck)
        {
            return tl::unexpected(std::move(businessCheck.error()));
        }
        FaceOrientationRepairAction action;
        action.edit = edit;
        action.targetFace = *found;
        action.projection = unique.projection;
        action.supportDomain = unique.domain;
        action.firstDiagonalLower = *unique.firstDiagonalLower;
        action.secondDiagonalLower = *unique.secondDiagonalLower;
        action.symmetricDeterminantLower = *unique.symmetricDeterminantLower;
        if (auto proof = proveLifts(native, surface, *record, action); !proof)
        {
            return tl::unexpected(std::move(proof.error()));
        }
        history.actions.push_back(action);
    }
    if (auto operation = execution.ClaimRepairEdits(indices); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    report.repairStage = "Rebuild";
    if (auto operation = execution.Check(Phase::BeforeRepair); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    report.repairEditsAttempted = indices.size();
    auto candidateResult = FinishConstructionAttempt(build(report, false, overrides));
    if (!candidateResult)
    {
        return tl::unexpected(std::move(candidateResult.error()));
    }
    auto candidate = std::move(*candidateResult);
    if (auto operation = CompareDefinitions(*witness.model, *candidate, history, report, execution); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    report.repairStage = "EvidenceAfter";
    if (auto operation = execution.Check(Phase::BeforeRepairProof); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    for (auto& action : history.actions)
    {
        const auto record = std::find_if(
            sourceFaces.begin(),
            sourceFaces.end(),
            [&](auto r)
            {
                return records.at(r).id == action.edit.faceId;
            }
        );
        auto face = candidate->faces.at(action.targetFace).face;
        if (auto proof = proveLifts(face, dynamic_cast<const SmBSplineSurface*>(face->GetSurface()), *record, action); !proof)
        {
            return tl::unexpected(std::move(proof.error()));
        }
    }
    witness.model.reset(); // Failed graph/context must be gone before readiness/publication callbacks.
    report.repairs = history;
    candidate->repairs = std::move(history);
    report.repairStage = "Completed";
    if (!execution.IsRootReport(report))
    {
        report.nativeCalls = execution.NativeCalls() - firstCall;
    }
    if (auto operation = execution.Check(Phase::RepairReady); !operation)
    {
        return tl::unexpected(std::move(operation.error()));
    }
    return candidate;
}
} // namespace step_import::detail
