// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportOrientationRepair.h"

#include "StepImportExecution.h"
#include "StepImportNativeData.h"
#include "StepImportSurfaceProof.h"

#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmCone.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmEllipse.h>
#include <SmFace.h>
#include <SmLine.h>
#include <SmLoop.h>
#include <SmPlane.h>
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
void Need(bool value, const char* key, std::optional<std::size_t> record = {})
{
    if (!value)
    {
        throw Failure{ Status::Indeterminate, key, "Explicit face-orientation repair evidence is unestablished", record, "Repair" };
    }
}
void Same(double a, double b)
{
    Need(std::isfinite(a) && std::isfinite(b) && a == b, "RepairChangedDefinitionOrTolerance");
}
void SameFrame(const SmAxis2Placement& a, const SmAxis2Placement& b)
{
    Same(a.GetOriginRef().x, b.GetOriginRef().x);
    Same(a.GetOriginRef().y, b.GetOriginRef().y);
    Same(a.GetOriginRef().z, b.GetOriginRef().z);
    Same(a.GetXAxisRef().x, b.GetXAxisRef().x);
    Same(a.GetXAxisRef().y, b.GetXAxisRef().y);
    Same(a.GetXAxisRef().z, b.GetXAxisRef().z);
    Same(a.GetYAxisRef().x, b.GetYAxisRef().x);
    Same(a.GetYAxisRef().y, b.GetYAxisRef().y);
    Same(a.GetYAxisRef().z, b.GetYAxisRef().z);
}
void SameCurve(const SmCurve& a, const SmCurve& b)
{
    Need(a.GetType() == b.GetType() && a.GetDim() == b.GetDim() && a.GetDegree() == b.GetDegree(), "RepairChangedCurveClass");
    const auto first = dynamic_cast<const SmBSplineCurve*>(&a), second = dynamic_cast<const SmBSplineCurve*>(&b);
    Need(first && second, "RepairCurveIdentityScope");
    ULONG ac = 0, bc = 0;
    double *ap = nullptr, *bp = nullptr;
    Need(
        first->GetControlPointsPointer(ac, ap) == SM_SUCCESS && second->GetControlPointsPointer(bc, bp) == SM_SUCCESS && ac == bc,
        "RepairChangedCurveNet"
    );
    for (ULONG i = 0; i < ac * 4; ++i)
    {
        Same(ap[i], bp[i]);
    }
    SmTArray<double> ak, bk;
    Need(first->GetKnotsAll(ak) == SM_SUCCESS && second->GetKnotsAll(bk) == SM_SUCCESS && ak.GetSize() == bk.GetSize(), "RepairChangedCurveKnots");
    for (ULONG i = 0; i < ak.GetSize(); ++i)
    {
        Same(ak[i], bk[i]);
    }
    Same(a.GetNaturalInterval().GetMin(), b.GetNaturalInterval().GetMin());
    Same(a.GetNaturalInterval().GetMax(), b.GetNaturalInterval().GetMax());
    Same(a.GetSTEPInterval().GetMin(), b.GetSTEPInterval().GetMin());
    Same(a.GetSTEPInterval().GetMax(), b.GetSTEPInterval().GetMax());
    if (const auto line = dynamic_cast<const SmLine*>(&a))
    {
        const auto other = dynamic_cast<const SmLine*>(&b);
        Need(other != nullptr, "RepairChangedAnalyticClass");
        Same(line->GetLinePoint().x, other->GetLinePoint().x);
        Same(line->GetLinePoint().y, other->GetLinePoint().y);
        Same(line->GetLinePoint().z, other->GetLinePoint().z);
        Same(line->GetLineVector().x, other->GetLineVector().x);
        Same(line->GetLineVector().y, other->GetLineVector().y);
        Same(line->GetLineVector().z, other->GetLineVector().z);
        Same(line->GetLineScale(), other->GetLineScale());
        Need(line->GetInsideOut() == other->GetInsideOut(), "RepairChangedAnalyticSense");
    }
    if (const auto ellipse = dynamic_cast<const SmEllipse*>(&a))
    {
        const auto other = dynamic_cast<const SmEllipse*>(&b);
        Need(other != nullptr && ellipse->GetInsideOut() == other->GetInsideOut(), "RepairChangedAnalyticSense");
        SameFrame(ellipse->GetPosition(), other->GetPosition());
        Same(ellipse->GetXRadius(), other->GetXRadius());
        Same(ellipse->GetYRadius(), other->GetYRadius());
    }
}
void SameSurface(const SmSurface& a, const SmSurface& b)
{
    Need(a.GetType() == b.GetType(), "RepairChangedSurfaceClass");
    const auto first = dynamic_cast<const SmBSplineSurface*>(&a), second = dynamic_cast<const SmBSplineSurface*>(&b);
    Need(
        first && second && first->GetDegree(SM_SP_U) == second->GetDegree(SM_SP_U) && first->GetDegree(SM_SP_V) == second->GetDegree(SM_SP_V),
        "RepairSurfaceIdentityScope"
    );
    ULONG au = 0, av = 0, bu = 0, bv = 0;
    double *ap = nullptr, *bp = nullptr;
    Need(
        first->GetControlPointsPointer(au, av, ap) == SM_SUCCESS && second->GetControlPointsPointer(bu, bv, bp) == SM_SUCCESS && au == bu && av == bv,
        "RepairChangedSurfaceNet"
    );
    for (ULONG i = 0; i < au * av * 4; ++i)
    {
        Same(ap[i], bp[i]);
    }
    double *ak = nullptr, *al = nullptr, *bk = nullptr, *bl = nullptr;
    Need(
        first->GetKnotsPointers(au, av, ak, al) == SM_SUCCESS && second->GetKnotsPointers(bu, bv, bk, bl) == SM_SUCCESS && au == bu && av == bv,
        "RepairChangedSurfaceKnots"
    );
    for (ULONG i = 0; i < au; ++i)
    {
        Same(ak[i], bk[i]);
    }
    for (ULONG i = 0; i < av; ++i)
    {
        Same(al[i], bl[i]);
    }
    const auto ad = a.GetNaturalUVDomain(), bd = b.GetNaturalUVDomain();
    Same(ad.GetMin().x, bd.GetMin().x);
    Same(ad.GetMax().x, bd.GetMax().x);
    Same(ad.GetMin().y, bd.GetMin().y);
    Same(ad.GetMax().y, bd.GetMax().y);
    const auto as = a.GetSTEPUVDomain(), bs = b.GetSTEPUVDomain();
    Same(as.GetMin().x, bs.GetMin().x);
    Same(as.GetMin().y, bs.GetMin().y);
    Same(as.GetMax().x, bs.GetMax().x);
    Same(as.GetMax().y, bs.GetMax().y);
    if (const auto plane = dynamic_cast<const SmPlane*>(&a))
    {
        const auto other = dynamic_cast<const SmPlane*>(&b);
        Need(other != nullptr, "RepairChangedAnalyticClass");
        SameFrame(plane->GetPosition(), other->GetPosition());
        Same(plane->GetUVScale().x, other->GetUVScale().x);
        Same(plane->GetUVScale().y, other->GetUVScale().y);
    }
    const auto comparePolar = [&](const auto* polar, const auto* other)
    {
        if (!polar)
        {
            return;
        }
        Need(other != nullptr, "RepairChangedAnalyticClass");
        SmAxis2Placement x, y;
        double r1 = 0, r2 = 0, s1 = 0, s2 = 0;
        SmBoolean swapX = FALSE, swapY = FALSE, reverseX = FALSE, reverseY = FALSE;
        if constexpr (std::is_same_v<std::remove_cv_t<std::remove_pointer_t<decltype(polar)>>, SmSphere>)
        {
            Need(
                polar->GetCanonical(x, r1, &swapX, &reverseX) == SM_SUCCESS && other->GetCanonical(y, r2, &swapY, &reverseY) == SM_SUCCESS,
                "RepairAnalyticDefinitionUnavailable"
            );
        }
        else
        {
            Need(
                polar->GetCanonical(x, r1, s1, &swapX, &reverseX) == SM_SUCCESS && other->GetCanonical(y, r2, s2, &swapY, &reverseY) == SM_SUCCESS,
                "RepairAnalyticDefinitionUnavailable"
            );
            Same(s1, s2);
        }
        SameFrame(x, y);
        Same(r1, r2);
        Need(swapX == swapY && reverseX == reverseY, "RepairChangedAnalyticSense");
    };
    comparePolar(dynamic_cast<const SmCone*>(&a), dynamic_cast<const SmCone*>(&b));
    comparePolar(dynamic_cast<const SmSphere*>(&a), dynamic_cast<const SmSphere*>(&b));
    comparePolar(dynamic_cast<const SmTorus*>(&a), dynamic_cast<const SmTorus*>(&b));
}
void CompareDefinitions(const ModelData& before, const ModelData& after, FaceOrientationRepairHistory& history, Report& report, Execution& execution)
{
    Need(
        before.source == after.source && before.vertices.size() == after.vertices.size() && before.edges.size() == after.edges.size() &&
            before.faces.size() == after.faces.size(),
        "RepairChangedSelectedInventory"
    );
    const auto compare = [&](const auto& oldSources, const auto& newSources, const auto& oldObjects, const auto& newObjects, const auto& inspect)
    {
        Need(oldSources.size() == oldObjects.size() && newSources.size() == newObjects.size(), "RepairMissingSourceInventory");
        std::map<std::size_t, std::size_t> targets;
        for (std::size_t i = 0; i < newSources.size(); ++i)
        {
            Need(targets.emplace(newSources[i], i).second, "RepairRepeatedSourceInventory");
        }
        for (std::size_t i = 0; i < oldSources.size(); ++i)
        {
            const auto found = targets.find(oldSources[i]);
            Need(found != targets.end(), "RepairLostSourceIdentity");
            execution.ChargeNative(report, oldSources[i]);
            const auto a = oldObjects[i], b = newObjects[found->second];
            inspect(a, b);
            Same(SmTol::GetZoneTol3d(a), SmTol::GetZoneTol3d(b));
        }
    };
    compare(
        before.repairVertexSources,
        after.repairVertexSources,
        before.vertices,
        after.vertices,
        [](const auto a, const auto b)
        {
            const auto x = a->GetPoint(), y = b->GetPoint();
            Same(x.x, y.x);
            Same(x.y, y.y);
            Same(x.z, y.z);
        }
    );
    compare(
        before.repairEdgeSources,
        after.repairEdgeSources,
        before.edges,
        after.edges,
        [](const auto a, const auto b)
        {
            Same(a->GetInterval().GetMin(), b->GetInterval().GetMin());
            Same(a->GetInterval().GetMax(), b->GetInterval().GetMax());
            SameCurve(*a->GetCurve(), *b->GetCurve());
        }
    );
    for (std::size_t i = 0; i < before.faces.size(); ++i)
    {
        execution.ChargeNative(report, before.repairVertexSources.front());
        const auto& a = before.faces[i];
        const auto& b = after.faces[i];
        Need(a.faceId == b.faceId, "RepairChangedFaceIdentity");
        SameSurface(*a.face->GetSurface(), *b.face->GetSurface());
        Same(SmTol::GetZoneTol3d(a.face), SmTol::GetZoneTol3d(b.face));
    }
    Same(SmTol::GetZoneTol3d(before.brep.get()), SmTol::GetZoneTol3d(after.brep.get()));
    history.verticesCompared = before.vertices.size();
    history.edgesCompared = before.edges.size();
    history.surfacesCompared = before.faces.size();
    history.definitionsIdentical = history.workingTolerancesIdentical = true;
}
} // namespace

void ValidateRepairTargets(const step_semantics::Outcome& checked, const Options& options, Execution& execution)
{
    if (!options.faceOrientationRepair)
    {
        return;
    }
    using Body = std::pair<std::shared_ptr<const step_semantics::Snapshot>, step_semantics::Part::Shape>;
    using Key = std::pair<std::string, std::string>;
    std::map<Key, Body> bodies;
    const auto include = [&](const std::shared_ptr<const step_semantics::Snapshot>& part)
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
        Need(role != roles.end(), "RepairDefinitionIdentityMissing");
        const auto& records = part->Source().Records();
        for (const auto& shape : part->PartDefinition().shapes)
        {
            if (!bodies.emplace(Key{ records[role->recordIndex].id, records[shape.shape].id }, Body{ part, shape }).second)
            {
                throw Failure{ Status::Unsupported,
                               "RepairBodyScopeAmbiguous",
                               "Repair body identity is not unique in the selected request",
                               shape.shape,
                               "Repair" };
            }
        }
    };
    if (checked.document)
    {
        for (std::size_t i = 0; i < checked.document->PartCount(); ++i)
        {
            include(checked.document->Part(i));
        }
    }
    else
    {
        include(checked.snapshot);
    }
    std::map<Key, std::map<std::string, std::size_t>> faces;
    for (const auto& edit : options.faceOrientationRepair->edits)
    {
        execution.Check(Phase::SemanticsChecked);
        const Key key{ edit.definitionId, edit.shapeId };
        const auto body = bodies.find(key);
        if (body == bodies.end())
        {
            throw Failure{ Status::Unsupported, "RepairTargetOutsideSelectedRequest", "Repair target is not selected", {}, "Repair" };
        }
        const auto& source = body->second.first;
        if (body->second.second.role != "MaterialSolid")
        {
            throw Failure{ Status::Unsupported,
                           "RepairRoleUnsupported",
                           "Orientation repair cannot upgrade a source surface role",
                           body->second.second.shape,
                           "Repair" };
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
            throw Failure{ Status::Unsupported, "RepairTargetOutsideSelectedRequest", "Repair face is not selected", {}, "Repair" };
        }
        const auto face = found->second;
        if ((source->Field(face, "same_sense").Value().text == "T") != edit.expectedSourceSameSense)
        {
            throw Failure{ Status::Unsupported, "RepairSourceSenseMismatch", "Repair expectation differs from immutable source", face, "Repair" };
        }
        const auto bounds = source->Field(face, "bounds").ReferenceTargets();
        if (bounds.size() != 1 || !source->IsA(bounds[0], "FACE_OUTER_BOUND") ||
            !source->IsA(source->Field(face, "face_geometry").ReferenceTarget(), "B_SPLINE_SURFACE_WITH_KNOTS"))
        {
            throw Failure{ Status::Unsupported,
                           "RepairFaceProfileUnsupported",
                           "Repair requires a single explicit outer loop on a B-spline support",
                           face,
                           "Repair" };
        }
    }
}

std::shared_ptr<ModelData> BuildWithOrientationRepair(
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
        return build(report, false, {});
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
    Need(role != part.roles.end(), "RepairDefinitionIdentityMissing");
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
        return build(report, false, {});
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
        Need(face != sourceFaces.end(), "RepairFaceOutsideSelectedBody");
        Need((source->Field(*face, "same_sense").Value().text == "T") == edit.expectedSourceSameSense, "RepairSourceSenseMismatch", *face);
        const auto bounds = source->Field(*face, "bounds").ReferenceTargets();
        Need(bounds.size() == 1 && source->IsA(bounds[0], "FACE_OUTER_BOUND"), "RepairRequiresSingleExplicitOuterBound", *face);
        const auto surface = source->Field(*face, "face_geometry").ReferenceTarget();
        Need(source->IsA(surface, "B_SPLINE_SURFACE_WITH_KNOTS"), "RepairSurfaceProfileUnsupported", *face);
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
    try
    {
        auto alreadyValid = build(*original, true, {});
        original->candidateDiscarded = true;
        report.originalAttemptStatus = HasAppliedSourceInterpretation(*source) ? Status::AcceptedUnderInterpretation : Status::Accepted;
        throw Failure{ Status::Unsupported,
                       "RepairNotRequired",
                       "The requested repair cannot alter an already accepted candidate",
                       shape.shape,
                       "Repair" };
    }
    catch (const OrientationFailureCandidate& failed)
    {
        witness = failed;
        report.originalAttemptStatus = failed.failure.status;
        original->candidateDiscarded = true;
        AddFailureDiagnostic(*original, failed.failure);
    }
    catch (const Failure& failed)
    {
        original->candidateDiscarded = original->nativeCalls > 0;
        if (failed.key != "RepairNotRequired")
        {
            report.originalAttemptStatus = failed.status;
            AddFailureDiagnostic(*original, failed);
        }
        throw;
    }
    report.repairStage = "EvidenceBefore";
    execution.Check(Phase::BeforeRepairProof);
    FaceOrientationRepairHistory history;
    history.sourceSha256 = report.sourceSha256;
    history.originalDiagnostics = original->diagnostics;
    history.sourceInterpretationApplied = HasAppliedSourceInterpretation(*source);
    Need(witness.targetFaces.size() == indices.size(), "RepairPlanDoesNotMatchFailureSet");
    const double numerator = std::nextafter(static_cast<double>(options.quality.derivedUvMm.Numerator()), 0.);
    const double denominator = std::nextafter(static_cast<double>(options.quality.derivedUvMm.Denominator()), std::numeric_limits<double>::infinity());
    const double allowed = std::nextafter(numerator / denominator, 0.);
    const auto proveLifts = [&](SmFace* native, const SmBSplineSurface* surface, std::size_t record, FaceOrientationRepairAction& action)
    {
        const auto checkpoint = [&]
        {
            execution.ChargeNative(report, record);
        };
        SmTArray<SmEdgeuse*> uses;
        native->GetUpwardEdgeuses(uses);
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            const auto interval = uses[i]->GetEdge()->GetInterval();
            const auto uv = uses[i]->GetUVTrimCurve();
            Need(uv != nullptr, "RepairMissingDerivedTrim", record);
            if (!execution.RepairProofRemaining())
            {
                throw Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Cumulative repair proof work exhausted", record, "Repair" };
            }
            const auto lift = ProveCurveLift(
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
            execution.SpendRepairProof(lift.productsUsed);
            if (lift.workLimit)
            {
                throw Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Repair lift proof exhausted its work", record, "Repair" };
            }
            Need(lift.established, "RepairContinuousLiftUnestablished", record);
            action.continuousLiftBoundMm = std::max(action.continuousLiftBoundMm, *lift.maximumMm);
            action.liftCells += lift.cells;
        }
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
        Need(found != witness.targetFaces.end(), "RepairPlanDoesNotMatchFailureSet");
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
        Need(surface && nativeLoops.GetSize() == 1, "RepairNativeFaceProfileUnsupported", *record);
        const auto checkpoint = [&]
        {
            execution.ChargeNative(report, *record);
        };
        if (!execution.RepairProofRemaining())
        {
            throw Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Cumulative repair proof work exhausted", *record, "Repair" };
        }
        const auto unique = ProveSurfaceInjectivity(*surface, request.maximumProofControls, execution.RepairProofRemaining(), checkpoint);
        execution.SpendRepairProof(unique.productsUsed);
        if (unique.workLimit)
        {
            throw Failure{ Status::ResourceLimit, "RepairProofWorkLimit", "Repair injectivity proof exhausted its workspace", *record, "Repair" };
        }
        Need(unique.established, "RepairSupportInjectivityUnestablished", *record);
        FaceOrientationRepairAction action;
        action.edit = edit;
        action.targetFace = *found;
        action.projection = unique.projection;
        action.supportDomain = unique.domain;
        action.firstDiagonalLower = *unique.firstDiagonalLower;
        action.secondDiagonalLower = *unique.secondDiagonalLower;
        action.symmetricDeterminantLower = *unique.symmetricDeterminantLower;
        proveLifts(native, surface, *record, action);
        history.actions.push_back(action);
    }
    execution.ClaimRepairEdits(indices);
    report.repairStage = "Rebuild";
    execution.Check(Phase::BeforeRepair);
    report.repairEditsAttempted = indices.size();
    auto candidate = build(report, false, overrides);
    CompareDefinitions(*witness.model, *candidate, history, report, execution);
    report.repairStage = "EvidenceAfter";
    execution.Check(Phase::BeforeRepairProof);
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
        proveLifts(face, dynamic_cast<const SmBSplineSurface*>(face->GetSurface()), *record, action);
    }
    witness.model.reset(); // Failed graph/context must be gone before readiness/publication callbacks.
    report.repairs = history;
    candidate->repairs = std::move(history);
    report.repairStage = "Completed";
    if (!execution.IsRootReport(report))
    {
        report.nativeCalls = execution.NativeCalls() - firstCall;
    }
    execution.Check(Phase::RepairReady);
    return candidate;
}
} // namespace step_import::detail
