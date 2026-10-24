// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmApiHeal.h>
#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmHealData.h>
#include <SmPlane.h>
#include <SmRegion.h>
#include <SmVertex.h>
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

struct FaultState
{
    bool rejectWireEvaluation = false;
    unsigned failures = 0;
};

constexpr SM_TYPE failingPointType = 0x7f184;

// Face construction and degeneracy classification use real NURBS evaluation.
// Only after the real DeleteFace has made the boundaries wires does the public
// virtual evaluator return an error. Both edges share state outside their
// lifetime: the old healer deletes the curves even after failed squeezing.
class FailingWirePoint final : public SmBSplineCurve
{
public:

    FailingWirePoint(const SmBSplineCurve& curve, FaultState& state) : SmBSplineCurve(curve), m_state(state)
    {
    }

    SmStatus EvaluatePoint(double parameter, SmPoint3d& point) const override
    {
        const SmStatus status = SmBSplineCurve::EvaluatePoint(parameter, point);
        const SmEdge* edge = GetEdge();
        if (status == SM_SUCCESS && m_state.rejectWireEvaluation && edge != nullptr && edge->IsWire())
        {
            ++m_state.failures;
            return SM_ERR_NOT_CONVERGING;
        }
        return status;
    }

    SmStatus Copy(const SmContext& context, SmCurve*& copy) const override
    {
        copy = new (context) FailingWirePoint(*this);
        return SM_SUCCESS;
    }

    SM_COMMON(FailingWirePoint, SmBSplineCurve, failingPointType);

private:

    FaultState& m_state;
};

SmBoolean FailingWirePoint::IsKindOf(SM_TYPE type) const
{
    return type == failingPointType || SmBSplineCurve::IsKindOf(type);
}

void FailingWirePoint::Dump() const
{
    SmBSplineCurve::Dump();
}

enum class Boundary
{
    DoubleLine,
    Vertex,
    VertexWithWire
};

std::unique_ptr<SmBrep> DegeneratePlane(SmContext& context, FaultState& state, Boundary boundary)
{
    SmBrepConstructor builder;
    auto brep = std::unique_ptr<SmBrep>(builder.StartBrep(context));
    Require(brep != nullptr, "Create Brep");
    SmShell* outer = nullptr;
    SmShell* inner = nullptr;
    Good(builder.StartShell(outer, inner));
    SmFace* face = builder.StartFace(SM_OT_SAME);
    Require(face != nullptr, "Create face");
    SmPlane* plane = nullptr;
    Good(SmPlane::CreateCanonical(context, SmAxis2Placement(), plane));
    Good(builder.SetFaceSurface(plane, SmExtent2d(SmPoint2d(-1, -1), SmPoint2d(11, 1))));
    SmVertex* attached = nullptr;
    if (boundary == Boundary::DoubleLine)
    {
        Require(builder.StartLoop(SM_OT_SAME) != nullptr, "Create loop");
        const SmPoint3d points[2] = { { 0, 0, 0 }, { 10, 0, 0 } };
        SmVertex* vertices[2] = { builder.StartVertexOfLoop(points[0]), builder.StartVertexOfLoop(points[1]) };
        Require(vertices[0] != nullptr && vertices[1] != nullptr, "Create loop vertices");
        for (unsigned i = 0; i < 2; ++i)
        {
            Require(builder.StartEdge(SM_OT_SAME, nullptr, vertices[i], vertices[1 - i]) != nullptr, "Create edge");
            SmBSplineCurve* line = nullptr;
            Good(SmBSplineCurve::CreateLineSegment(context, 3, points[i], points[1 - i], line));
            Good(builder.SetEdgeCurve(line, FALSE));
            Good(builder.EndEdge());
        }
        Good(builder.EndLoop());
    }
    else
    {
        Require(builder.StartAndEndSingleVertexLoop(SmPoint3d(0, 0, 0)) != nullptr, "Create vertex loop");
    }
    Good(builder.EndFace());
    Good(builder.EndShell(FALSE));
    Good(builder.EndBrep());
    if (boundary == Boundary::DoubleLine)
    {
        SmTArray<SmEdge*> edges;
        brep->GetEdges(edges);
        Require(edges.GetSize() == 2, "Two independent boundary edges");
        for (ULONG i = 0; i < edges.GetSize(); ++i)
        {
            Require(edges[i]->GetCurve()->IsKindOf(SmBSplineCurve_TYPE), "NURBS line boundary");
            auto* curve = new (context) FailingWirePoint(*static_cast<SmBSplineCurve*>(edges[i]->GetCurve()), state);
            Good(brep->ReplaceCurve(edges[i], curve));
        }
    }
    if (boundary == Boundary::VertexWithWire)
    {
        SmTArray<SmVertex*> vertices;
        face->GetVertices(vertices);
        Require(vertices.GetSize() == 1, "Single attached vertex");
        attached = vertices[0];
        SmBSplineCurve* line = nullptr;
        Good(SmBSplineCurve::CreateLineSegment(context, 3, attached->GetPoint(), SmPoint3d(0, 0, 2), line));
        SmEdge* wire = nullptr;
        SmVertex* end = nullptr;
        brep->m_bEditingEnabled = TRUE;
        Good(
            brep->MakeWireEdgeVertex(brep->GetInfiniteRegion(), attached, line, line->GetNaturalInterval(), SM_OT_SAME, SmPoint3d(0, 0, 2), wire, end)
        );
        brep->m_bEditingEnabled = FALSE;
    }
    Require(face->IsDegenerate(TRUE), "Real zero-area face must be classified as degenerate");
    Require(state.failures == 0, "The failure must not interrupt face classification");
    return brep;
}

SmStatus SqueezeReference(bool reject)
{
    SmContext context;
    FaultState state{ reject };
    auto brep = DegeneratePlane(context, state, Boundary::DoubleLine);
    SmTArray<SmFace*> faces;
    brep->GetFaces(faces);
    brep->m_bEditingEnabled = TRUE;
    const SmStatus status = brep->SqueezeFace(faces[0], nullptr, nullptr, nullptr, nullptr);
    // SqueezeFace finishes its internal cleanup but folds a stitching failure
    // into SM_ERR. This test preserves that delivered status, not a deeper code.
    Require(status == (reject ? SM_ERR : SM_SUCCESS), "Direct squeeze reference result");
    Require(reject ? state.failures != 0 : state.failures == 0, "Direct squeeze must reach the chosen failure");
    Require(brep->GetNumFaces() == 0, "Squeezing is in-place and does not roll the deleted face back");
    return status;
}

void Failure(unsigned entry)
{
    const SmStatus reference = SqueezeReference(true);
    for (SmBoolean originalEditing : { FALSE, TRUE })
    {
        SmContext context;
        FaultState state{ true };
        auto brep = DegeneratePlane(context, state, Boundary::DoubleLine);
        brep->m_bEditingEnabled = originalEditing;
        auto healer = std::make_unique<SmHealData>();
        SmStatus status = SM_ERR;
        if (entry == 0)
        {
            status = healer->HealBrep(brep.get(), SM_HO_FIX_DEGEN_FACES);
        }
        else if (entry == 1)
        {
            status = brep->HealBrep(SM_HO_FIX_DEGEN_FACES);
        }
        else
        {
            status = SmApiHealBrep(brep.get());
        }
        std::printf(
            "degen entry=%u editing=%d status=%d failed evaluations=%u F/E/V=%lu/%lu/%lu\n",
            entry,
            originalEditing,
            static_cast<int>(status),
            state.failures,
            brep->GetNumFaces(),
            brep->GetNumEdges(),
            brep->GetNumVertices()
        );
        Require(state.failures != 0, "Healing must reach the real failing squeeze");
        Require(status == reference, "Healing must propagate SqueezeFace failure");
        Require(brep->m_bEditingEnabled == originalEditing, "Healing must restore editing policy");
        Require(context.GetSysNotifyCallback() == nullptr, "Healing must release its topology tracker");
        Require(!context.GetDoingBooleanRef(), "Healing must restore the Boolean scope");
        if (entry == 0)
        {
            Require(healer->m_eDoneHealOp == SM_HO_CACHE_DEGEN_FACES, "Failed repair must not be recorded as completed");
        }
        // Do not consume old properties after partial topology edits. Resetting
        // the retained session must safely release them, without another repair.
        healer->ReSet();
        healer.reset();
        state.rejectWireEvaluation = false;
        Good(brep->ValidatePointers());
        SmTArray<SmEdge*> remaining;
        brep->GetEdges(remaining);
        Require(remaining.GetSize() == 2, "Stop after SqueezeFace failure, before outer wire cleanup");
        for (ULONG i = 0; i < remaining.GetSize(); ++i)
        {
            Require(remaining[i]->IsWire(), "Deleted face leaves wire boundaries");
            Require(
                remaining[i]->GetInterval().GetMin() == 0 && remaining[i]->GetInterval().GetMax() == 1,
                "The defining curve parameter intervals must remain unchanged"
            );
        }
    }
}

void Control(Boundary boundary)
{
    if (boundary == Boundary::DoubleLine)
    {
        Good(SqueezeReference(false));
    }
    for (SmBoolean originalEditing : { FALSE, TRUE })
    {
        SmContext context;
        FaultState state;
        auto brep = DegeneratePlane(context, state, boundary);
        brep->m_bEditingEnabled = originalEditing;
        const bool connected = boundary == Boundary::VertexWithWire;
        SmTArray<SmEdge*> originalEdges;
        brep->GetEdges(originalEdges);
        Require(!connected || originalEdges.GetSize() == 1, "One connected wire before healing");
        SmEdge* wire = connected ? originalEdges[0] : nullptr;
        SmVertex* start = connected ? wire->GetStartVertex() : nullptr;
        SmVertex* end = connected ? wire->GetEndVertex() : nullptr;
        SmCurve* curve = connected ? wire->GetCurve() : nullptr;
        SmExtent1d interval = connected ? wire->GetInterval() : SmExtent1d();
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            Good(SmApiHealBrep(brep.get()));
            Require(brep->GetNumFaces() == 0, "Successful repair removes the degenerate face");
            Require(brep->GetNumEdges() == (connected ? 1u : 0u), "Clean only the degenerate face's leftover wires");
            Require(brep->GetNumVertices() == (connected ? 2u : 0u), "Keep vertices still used by connected topology");
            Require(
                brep->m_bEditingEnabled == originalEditing && context.GetSysNotifyCallback() == nullptr,
                "Successful healing must restore editing and tracking"
            );
            if (connected)
            {
                SmTArray<SmEdge*> remaining;
                brep->GetEdges(remaining);
                Require(remaining[0] == wire, "Keep the original connected wire");
                Require(
                    wire->GetStartVertex() == start && wire->GetEndVertex() == end && wire->GetCurve() == curve,
                    "Keep the connected wire's defining geometry and endpoint identities"
                );
                Require(
                    start->GetPoint().x == 0 && start->GetPoint().y == 0 && start->GetPoint().z == 0 && end->GetPoint().x == 0 &&
                        end->GetPoint().y == 0 && end->GetPoint().z == 2,
                    "Keep the connected wire's exact endpoint coordinates"
                );
                Require(
                    wire->GetInterval().GetMin() == interval.GetMin() && wire->GetInterval().GetMax() == interval.GetMax(),
                    "Keep the connected wire's parameter interval"
                );
            }
            SmAssertArray reports;
            Good(brep->ValidatePointers(&reports));
            Require(reports.GetSize() == 0, "No pointer or geometry reports on the successful result");
            Require(
                brep->AssertValid(&reports, SM_LEVEL_2, SM_WALK) && reports.GetSize() == 0,
                "Successful result must pass Level 2 validation without reports"
            );
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Expected stage, native, api, control, vertex or attached");
        const std::string_view mode(argv[1]);
        if (mode == "stage")
        {
            Failure(0);
        }
        else if (mode == "native")
        {
            Failure(1);
        }
        else if (mode == "api")
        {
            Failure(2);
        }
        else if (mode == "control")
        {
            Control(Boundary::DoubleLine);
        }
        else if (mode == "vertex")
        {
            Control(Boundary::Vertex);
        }
        else
        {
            Require(mode == "attached", "Unknown mode");
            Control(Boundary::VertexWithWire);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
