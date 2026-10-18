// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmApiGeneral.h>
#include <SmApiPlanarRegions.h>
#include <SmApiPrimitives.h>
#include <SmApiTrimmedSurfaces.h>
#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <SmLine.h>
#include <SmPlanarCurveTransfer.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#if defined(_WIN32) && defined(_DEBUG)
#    include <crtdbg.h>
#endif

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
class CountedLine : public SmLine
{
public:

    static int live;
    CountedLine(const SmContext& context, const SmPoint3d& a, const SmPoint3d& b) : SmLine(a, b, 3, &context)
    {
        ++live;
    }
    ~CountedLine() override
    {
        --live;
    }
    SmStatus Copy(const SmContext& context, SmCurve*& result) const override
    {
        SmPoint3d a, b;
        if (EvaluatePoint(GetNaturalInterval().GetMin(), a) != SM_SUCCESS || EvaluatePoint(GetNaturalInterval().GetMax(), b) != SM_SUCCESS)
        {
            return SM_ERR;
        }
        result = new (context) CountedLine(context, a, b);
        return SM_SUCCESS;
    }
};
int CountedLine::live = 0;
SmTArray<SmCurve*> Square(const SmContext& context, double z = 0)
{
    SmPoint3d points[] = { { 0, 0, z }, { 10, 0, z }, { 10, 10, z }, { 0, 10, z } };
    SmTArray<SmCurve*> curves;
    for (int i = 0; i < 4; ++i)
    {
        curves.Add(new (context) CountedLine(context, points[i], points[(i + 1) % 4]));
    }
    return curves;
}
void Alive(const SmTArray<SmCurve*>& curves)
{
    for (ULONG i = 0; i < curves.GetSize(); ++i)
    {
        SmPoint3d point;
        Require(
            curves[i]->GetOwner() == nullptr && curves[i]->EvaluatePoint(0.5, point) == SM_SUCCESS,
            "Returned inputs must remain unowned and evaluable"
        );
    }
}
void Delete(SmTArray<SmCurve*>& curves)
{
    for (ULONG i = 0; i < curves.GetSize(); ++i)
    {
        delete curves[i];
    }
    curves.ReSet();
}
void Valid(SmBrep& brep)
{
    SmAssertArray pointers;
    brep.ValidatePointers(&pointers);
    Require(pointers.GetSize() == 0, "Published planar topology pointer validity");
    SmAssertArray deep;
    brep.AssertValid(&deep, SM_LEVEL_2, SM_WALK);
    Require(deep.GetSize() == 0, "Published planar topology level-2 validity");
}
void Transfers()
{
    SmContext context;
    for (long stop = 0; stop <= 4; ++stop)
    {
        auto curves = Square(context);
        SmPlanarCurveTransfer transfer(curves);
        std::unique_ptr<SmBrep> brep(new (context) SmBrep());
        SmTArray<SmFace*> faces;
        SmTArray<SmBoolean> flags;
        flags.SetSize(4);
        flags.SetAll(FALSE);
        SmPlanarCurveTransfer::FailBeforeLinkAfter(stop == 4 ? -1 : stop);
        SmPlanarCurveTransfer::FailAfterConstruction(stop == 4);
        Require(transfer.Create(*brep, nullptr, 0.01, faces, &flags) != SM_SUCCESS, "Injected construction failure is observed");
        int transferred = 0;
        for (ULONG i = 0; i < curves.GetSize(); ++i)
        {
            transferred += transfer.Transferred(i);
        }
        Require(transferred == stop, "Only linked recipient edges retain ownership after unlinked failure cleanup");
        transfer.DetachInputs();
        for (ULONG i = 0; i < flags.GetSize(); ++i)
        {
            Require(!flags[i], "Returned curves clear transfer facts");
        }
        brep.reset();
        Alive(curves);
        Require(CountedLine::live == 4, "Failed candidate does not delete retained input curves");
        Delete(curves);
        Require(CountedLine::live == 0, "Each retained input is released exactly once");
    }
    SmPlanarCurveTransfer::FailBeforeLinkAfter(-1);
    SmPlanarCurveTransfer::FailAfterConstruction(false);
    auto curves = Square(context);
    SmPlanarCurveTransfer transfer(curves);
    std::unique_ptr<SmBrep> brep(new (context) SmBrep());
    SmTArray<SmFace*> faces;
    Require(transfer.Create(*brep, nullptr, 0.01, faces) == SM_SUCCESS, "Successful planar construction");
    for (ULONG i = 0; i < curves.GetSize(); ++i)
    {
        Require(transfer.Transferred(i), "Success has actual per-input transfers");
    }
    Valid(*brep);
    brep.reset();
    Require(CountedLine::live == 0, "Successful candidate owns and releases all inputs");
}
void Adapters()
{
    const auto& context = *SmApiGetOrCreateContext();
    for (long stop : { 0L, 1L, 3L, 4L })
    {
        for (int adapter = 0; adapter < 4; ++adapter)
        {
            auto curves = Square(context);
            SmBrep* result = nullptr;
            SmPlanarCurveTransfer::FailBeforeLinkAfter(stop == 4 ? -1 : stop);
            SmPlanarCurveTransfer::FailAfterConstruction(stop == 4);
            if (adapter == 0)
            {
                Require(SmApiCreatePlanarFaces(curves, result) != SM_SUCCESS && !result, "Direct planar failure has no model");
            }
            else if (adapter == 1)
            {
                SmPlanarRegionReport report;
                Require(
                    SmApiBuildPlanarRegions(curves, { 0, 0, 0 }, { 0, 0, 1 }, {}, result, report) != SmPlanarRegionStatus::Success && !result,
                    "Private planar-region clone failure has no model"
                );
            }
            else if (adapter == 2)
            {
                Require(
                    SmApiCreateOffsetProfile(curves, 1, 1, FALSE, FALSE, result) != SM_SUCCESS && !result,
                    "Offset partial construction fails without consuming original inputs"
                );
            }
            else
            {
                auto upper = Square(context, 10);
                SmTArray<const SmCurve*> profiles;
                for (ULONG i = 0; i < curves.GetSize(); ++i)
                {
                    profiles.Add(curves[i]);
                }
                for (ULONG i = 0; i < upper.GetSize(); ++i)
                {
                    profiles.Add(upper[i]);
                }
                SmTArray<ULONG> counts;
                counts.Add(4);
                counts.Add(4);
                const auto status = SmApiCreateSkinPrimitive(counts, profiles, 1, TRUE, result, nullptr, nullptr, nullptr);
                // A failure with zero transfers may use the existing open-profile
                // fallback. Once any curve transferred it must propagate failure.
                if (stop > 0)
                {
                    Require(status != SM_SUCCESS && !result, "Skin cannot reinterpret a partial transfer as an open profile");
                }
                delete result;
                Alive(upper);
                Delete(upper);
            }
            Alive(curves);
            Delete(curves);
            Require(CountedLine::live == 0, "Adapters preserve original inputs on failure");
        }
    }
    SmPlanarCurveTransfer::FailBeforeLinkAfter(-1);
    SmPlanarCurveTransfer::FailAfterConstruction(false);
}
void Exceptions()
{
    const auto& context = *SmApiGetOrCreateContext();
    for (int adapter = 0; adapter < 4; ++adapter)
    {
        auto curves = Square(context);
        auto upper = Square(context, 10);
        SmBrep* result = nullptr;
        SmPlanarCurveTransfer::FailBeforeLinkAfter(1, true);
        bool threw = false;
        try
        {
            if (adapter == 0)
            {
                SmApiCreatePlanarFaces(curves, result);
            }
            else if (adapter == 1)
            {
                SmPlanarRegionReport report;
                SmApiBuildPlanarRegions(curves, { 0, 0, 0 }, { 0, 0, 1 }, {}, result, report);
            }
            else if (adapter == 2)
            {
                SmApiCreateOffsetProfile(curves, 1, 1, FALSE, FALSE, result);
            }
            else
            {
                SmTArray<const SmCurve*> profiles;
                for (ULONG i = 0; i < curves.GetSize(); ++i)
                {
                    profiles.Add(curves[i]);
                }
                for (ULONG i = 0; i < upper.GetSize(); ++i)
                {
                    profiles.Add(upper[i]);
                }
                SmTArray<ULONG> counts;
                counts.Add(4);
                counts.Add(4);
                SmApiCreateSkinPrimitive(counts, profiles, 1, TRUE, result, nullptr, nullptr, nullptr);
            }
        }
        catch (const std::bad_alloc&)
        {
            threw = true;
        }
        Require(threw && !result, "Allocation failure after partial transfer unwinds without publishing");
        Alive(curves);
        Alive(upper);
        Delete(curves);
        Delete(upper);
        Require(CountedLine::live == 0, "Exception cleanup releases private clones exactly once");
    }
    SmPlanarCurveTransfer::FailBeforeLinkAfter(-1);
}
void RejectedInputs()
{
    SmContext context;
    auto curves = Square(context);
    curves.Add(curves[0]);
    SmPlanarCurveTransfer duplicate(curves);
    std::unique_ptr<SmBrep> brep(new (context) SmBrep());
    SmTArray<SmFace*> faces;
    Require(
        duplicate.Create(*brep, nullptr, 0.01, faces) != SM_SUCCESS && !duplicate.AnyTransferred(),
        "Duplicate identity fails before any transfer"
    );
    curves.SetSize(4);
    Alive(curves);
    Delete(curves);
}
void RegionWithHole()
{
    const auto& context = *SmApiGetOrCreateContext();
    auto curves = Square(context);
    SmPoint3d hole[] = { { 2, 2, 0 }, { 4, 2, 0 }, { 4, 4, 0 }, { 2, 4, 0 } };
    for (int i = 0; i < 4; ++i)
    {
        curves.Add(new (context) CountedLine(context, hole[i], hole[(i + 1) % 4]));
    }
    SmPlanarRegionReport report;
    SmBrep* result = nullptr;
    Require(
        SmApiBuildPlanarRegions(curves, { 0, 0, 0 }, { 0, 0, 1 }, {}, result, report) == SmPlanarRegionStatus::Success && result,
        "Planar region with a hole succeeds"
    );
    Require(report.regions == 1 && report.holes == 1, "Region and hole semantics survive transfer consolidation");
    Valid(*result);
    Alive(curves);
    delete result;
    Require(CountedLine::live == 8, "Region result releases private clones, retaining originals");
    Delete(curves);
    Require(CountedLine::live == 0, "All region inputs and clones released once");
}
void DegenerateLoop()
{
    SmContext context;
    auto curves = Square(context);
    SmPoint3d a(20, 0, 0), b(30, 0, 0), c(40, 0, 0);
    curves.Add(new (context) CountedLine(context, a, b));
    curves.Add(new (context) CountedLine(context, b, c));
    curves.Add(new (context) CountedLine(context, c, a));
    SmPlanarCurveTransfer transfer(curves);
    std::unique_ptr<SmBrep> brep(new (context) SmBrep());
    SmTArray<SmFace*> faces;
    Require(transfer.Create(*brep, nullptr, 0.01, faces) != SM_SUCCESS, "A zero-area extra loop cannot silently become a successful consuming result");
    transfer.DetachInputs();
    brep.reset();
    Alive(curves);
    Delete(curves);
    Require(CountedLine::live == 0, "Degenerate-loop rejection preserves ownership");
}
void Retention()
{
#if defined(_WIN32) && defined(_DEBUG)
    // The executable and both DLLs use /MDd. Warm native caches, then observe
    // repeated status/exception failures with the shared debug CRT allocator.
    for (int i = 0; i < 8; ++i)
    {
        Transfers();
        Exceptions();
    }
    _CrtMemState before{}, held{}, after{};
    _CrtMemCheckpoint(&before);
    {
        SmContext context;
        auto curves = Square(context);
        _CrtMemCheckpoint(&held);
        Require(held.lCounts[_NORMAL_BLOCK] > before.lCounts[_NORMAL_BLOCK], "CRT observer detects retained native allocations");
        Delete(curves);
    }
    _CrtMemCheckpoint(&before);
    for (int i = 0; i < 32; ++i)
    {
        Transfers();
        Exceptions();
    }
    _CrtMemCheckpoint(&after);
    const auto beforeBytes = before.lSizes[_NORMAL_BLOCK] + before.lSizes[_CLIENT_BLOCK];
    const auto afterBytes = after.lSizes[_NORMAL_BLOCK] + after.lSizes[_CLIENT_BLOCK];
    const auto beforeBlocks = before.lCounts[_NORMAL_BLOCK] + before.lCounts[_CLIENT_BLOCK];
    const auto afterBlocks = after.lCounts[_NORMAL_BLOCK] + after.lCounts[_CLIENT_BLOCK];
    std::cout << "retained bytes " << beforeBytes << " -> " << afterBytes << ", blocks " << beforeBlocks << " -> " << afterBlocks << '\n';
    Require(afterBytes <= beforeBytes + 4096 && afterBlocks <= beforeBlocks + 8, "Repeated partial-transfer failures retain no native allocations");
#endif
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "resources")
        {
            Retention();
        }
        else if (argc == 2 && std::string(argv[1]) == "degenerate")
        {
            DegenerateLoop();
        }
        else
        {
            Transfers();
            Adapters();
            Exceptions();
            RejectedInputs();
            RegionWithHole();
        }
        std::cout << "Planar curve transfer contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
