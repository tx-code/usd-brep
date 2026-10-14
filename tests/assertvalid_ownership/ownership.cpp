// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// SmCrvOnSurf's inline methods require the complete surface definition first.
// clang-format off
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmBrep.h>
#include <SmSurface.h>
#include <SmCrvOnSurf.h>
#include <SmCurveClass.h>
#include <SmFace.h>
#include <SmLine.h>
// clang-format on
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#    include <Windows.h>
#endif

static void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static std::unique_ptr<SmBrep> makeBody(int kind)
{
    SmBrep* body = nullptr;
    const SmVector3d origin(0, 0, 0);
    const SmApiStatus status = kind == 0 ? SmApiCreateCylinder(origin, 0.75, 3.5, body) :
                               kind == 1 ? SmApiCreateSphere(origin, 5.0, body) :
                                           SmApiCreateTorus(origin, 5.0, 1.0, body);
    require(status == SM_SUCCESS && body != nullptr, "primitive creation failed");
    return std::unique_ptr<SmBrep>(body);
}

static void checkAlias(const SmCurveClassification& classification, const SmSurface* surface, unsigned& checked)
{
    if (classification.GetCurve() == nullptr)
    {
        return;
    }
    const auto* seam = dynamic_cast<const SmCrvOnSurf*>(classification.GetCurve());
    require(seam != nullptr, "expected a seam curve on surface");
    require(classification.GetUVCurve1() == seam->GetUVCurve(), "UV alias does not belong to the copied seam curve");
    const auto interval = seam->GetNaturalInterval();
    for (double fraction : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        const double parameter = interval.GetMin() + fraction * (interval.GetMax() - interval.GetMin());
        SmPoint3d uv;
        SmPoint3d point;
        SmPoint3d surfacePoint;
        require(classification.GetUVCurve1()->EvaluatePoint(parameter, uv) == SM_SUCCESS, "UV evaluation failed");
        require(seam->EvaluatePoint(parameter, point) == SM_SUCCESS, "seam evaluation failed");
        require(surface->EvaluatePoint(SmPoint2d(uv[0], uv[1]), surfacePoint) == SM_SUCCESS, "face surface evaluation failed");
        require(point.DistanceBetween(surfacePoint) < 1.0e-8, "UV alias no longer describes the seam on the face");
        require(
            std::isfinite(uv[0]) && std::isfinite(uv[1]) && std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2]),
            "non-finite seam geometry"
        );
    }
    ++checked;
}

static void aliases()
{
    unsigned checked = 0;
    for (int kind = 0; kind < 3; ++kind)
    {
        auto body = makeBody(kind);
        SmTArray<SmFace*> faces;
        body->GetFaces(faces);
        for (ULONG i = 0; i < faces.GetSize(); ++i)
        {
            SmCurveClassification u;
            SmCurveClassification v;
            for (int repeat = 0; repeat < 3; ++repeat)
            {
                SmSurfParamType crossed, missing, nearMiss;
                faces[i]->HasSeamProblem(u, v, crossed, missing, nearMiss);
                // HasSeamProblem's local FaceProps has already been destroyed.
                checkAlias(u, faces[i]->GetSurface(), checked);
                checkAlias(v, faces[i]->GetSurface(), checked);
            }
        }
        require(body->AssertValid(nullptr, SM_LEVEL_2, SM_WALK) == TRUE, "primitive became invalid");
    }
    require(checked >= 12, "U/V seam paths were not exercised");
    std::cout << "PASS seam aliases and repeated assignment: " << checked << '\n';
}

// A source with two UV/surface aliases and nonempty classification storage.
static void initClassification(SmCurveClassification& result, SmSurface& surface, ULONG ownership)
{
    auto* context = SmApiGetOrCreateContext();
    SmPoint2d start(0.1, 0.1), end(0.4, 0.4);
    SmLine* line = nullptr;
    require(SmLine::CreateLineSegment(*context, 2, start, end, line) == SM_SUCCESS, "UV line creation failed");
    std::unique_ptr<SmLine> input(line);
    SmCrvOnSurf* curve = nullptr;
    if (ownership == 3)
    {
        SmSurface* ownedSurface = nullptr;
        require(surface.Copy(*context, ownedSurface) == SM_SUCCESS, "surface creation failed");
        curve = new (*context) SmCrvOnSurf(*input.release(), *ownedSurface, nullptr, 3, context);
    }
    else
    {
        curve = new (*context) SmCrvOnSurf(*line, surface, nullptr, ownership, context);
    }
    result.SetContext(context);
    result.SetCurve(curve, TRUE);
    result.SetUVCurve1(curve->GetUVCurve());
    result.SetUVCurve2(curve->GetUVCurve());
    result.SetSurface1(curve->GetBaseSurface());
    result.SetSurface2(curve->GetBaseSurface());
    result.SetSize(2);
}

static SmPoint3d midpoint(const SmCurve* curve)
{
    const auto interval = curve->GetNaturalInterval();
    SmPoint3d point;
    require(curve->EvaluatePoint((interval.GetMin() + interval.GetMax()) * 0.5, point) == SM_SUCCESS, "copy curve evaluation failed");
    return point;
}

static void checkCopy(const SmCurveClassification& result, const SmPoint3d& expected)
{
    const auto* curve = dynamic_cast<const SmCrvOnSurf*>(result.GetCurve());
    require(curve != nullptr, "copy lost curve type");
    require(result.GetAnyUVCurve1() == curve->GetUVCurve(), "copy retained source UV1 alias");
    require(result.GetAnyUVCurve2() == curve->GetUVCurve(), "copy retained source UV2 alias");
    require(result.GetSurface1() == curve->GetBaseSurface(), "copy retained source surface1 alias");
    require(result.GetSurface2() == curve->GetBaseSurface(), "copy retained source surface2 alias");
    require(midpoint(curve).DistanceBetween(expected) < 1e-10, "copy changed geometry");
    for (ULONG i = 0; i < result.GetSize(); ++i)
    {
        require(result[i].GetCurveClassification() == &result, "interval points to source classification");
    }
}

static void copyContract(const std::string& mode)
{
    require(
        mode == "--copy-owned" || mode == "--copy-borrowed" || mode == "--copy-external" || mode == "--copy-context" || mode == "--copy-empty" ||
            mode == "--copy-self-uv" || mode == "--copy-borrowed-surface" || mode == "--copy-borrowed-children" || mode == "--copy-transferred" ||
            mode == "--copy-borrowed-intervals",
        "unknown copy test mode"
    );
    auto body = makeBody(2);
    SmTArray<SmFace*> faces;
    body->GetFaces(faces);
    auto& surface = *const_cast<SmSurface*>(faces[0]->GetSurface());
    if (mode == "--copy-empty")
    {
        SmCurveClassification empty;
        SmCurveClassification copy(empty);
        SmCurveClassification target;
        initClassification(target, surface, 2);
        target = empty;
        require(
            copy.GetCurve() == nullptr && copy.GetSize() == 0 && target.GetCurve() == nullptr && target.GetSize() == 0,
            "empty copy retained data"
        );
        return;
    }
    SmCurveClassification source;
    initClassification(source, surface, mode == "--copy-borrowed-surface" ? 1 : mode == "--copy-transferred" ? 3 : 2);
    const SmPoint3d expected = midpoint(source.GetCurve());
    if (mode == "--copy-borrowed-intervals")
    {
        SmCurveClassification assigned;
        std::unique_ptr<SmCurveClassification> constructed;
        {
            SmCurveInterval storage[20];
            SmCurveClassification borrowed;
            const auto interval = source.GetCurve()->GetNaturalInterval();
            borrowed.ReSet(source.GetCurve(), &interval, nullptr, SM_ZONE_TOL_3D, 20, storage);
            require(borrowed.GetSize() > 0, "borrowed intervals are empty");
            require(&borrowed[0] == &storage[0], "test did not borrow interval storage");
            assigned = borrowed;
            constructed.reset(new (*SmApiGetOrCreateContext()) SmCurveClassification(borrowed));
            require(&assigned[0] != &storage[0] && &(*constructed)[0] != &storage[0], "interval storage was borrowed by copy");
        }
        for (auto* copy : { &assigned, constructed.get() })
        {
            require((*copy)[0].GetCurveClassification() == copy, "copied interval has source back-pointer");
            require(midpoint(copy->GetCurve()).DistanceBetween(expected) < 1e-10, "borrowed interval copy changed curve");
        }
        return;
    }
    if (mode == "--copy-context")
    {
        source.SetContext(nullptr); // Curve context must supply the clone allocation context.
    }
    if (mode == "--copy-borrowed")
    {
        SmCurveClassification borrowed;
        borrowed.SetCurve(source.GetCurve(), FALSE);
        borrowed.SetUVCurve1(source.GetAnyUVCurve1());
        borrowed.SetUVCurve2(source.GetAnyUVCurve2());
        SmCurveClassification copy(borrowed);
        SmCurveClassification target;
        initClassification(target, surface, 2);
        target = borrowed;
        require(copy.GetCurve() == source.GetCurve() && target.GetCurve() == source.GetCurve(), "borrowed curve was cloned");
        require(copy.GetAnyUVCurve1() == source.GetAnyUVCurve1() && target.GetAnyUVCurve2() == source.GetAnyUVCurve2(), "borrowed aliases changed");
        return;
    }
    if (mode == "--copy-borrowed-children")
    {
        const auto* original = dynamic_cast<const SmCrvOnSurf*>(source.GetCurve());
        auto* curve = new (*SmApiGetOrCreateContext()) SmCrvOnSurf(
            *const_cast<SmCurve*>(original->GetUVCurve()),
            *const_cast<SmSurface*>(original->GetBaseSurface()),
            nullptr,
            0,
            SmApiGetOrCreateContext()
        );
        SmCurveClassification borrower;
        borrower.SetCurve(curve, TRUE); // Own the container, borrow both children.
        borrower.SetUVCurve1(curve->GetUVCurve());
        borrower.SetUVCurve2(curve->GetUVCurve());
        borrower.SetSurface1(curve->GetBaseSurface());
        borrower.SetSurface2(curve->GetBaseSurface());
        SmCurveClassification copy(borrower);
        SmCurveClassification target;
        target = borrower;
        borrower = SmCurveClassification();
        checkCopy(copy, expected);
        checkCopy(target, expected);
        require(
            copy.GetAnyUVCurve1() == original->GetUVCurve() && target.GetSurface1() == original->GetBaseSurface(),
            "borrowed children were cloned"
        );
        return;
    }
    if (mode == "--copy-external")
    {
        SmCurveClassification external;
        initClassification(external, surface, 2);
        source.SetUVCurve1(external.GetAnyUVCurve1());
        source.SetUVCurve2(nullptr);
        source.SetSurface1(&surface);
        source.SetSurface2(nullptr);
        SmCurveClassification copy(source);
        SmCurveClassification target;
        target = source;
        for (const auto* result : { &copy, &target })
        {
            require(result->GetAnyUVCurve1() == external.GetAnyUVCurve1() && result->GetAnyUVCurve2() == nullptr, "external/null UV was rebound");
            require(result->GetSurface1() == &surface && result->GetSurface2() == nullptr, "external/null surface was rebound");
        }
        return;
    }
    if (mode == "--copy-self-uv")
    {
        source.SetUVCurve1(source.GetCurve());
        source.SetUVCurve2(source.GetCurve());
        SmCurveClassification copy(source);
        SmCurveClassification target;
        target = source;
        require(copy.GetAnyUVCurve1() == copy.GetCurve() && target.GetAnyUVCurve2() == target.GetCurve(), "self curve alias was not rebound");
        return;
    }
    source.SetIsCurveCurrent(FALSE); // Stored aliases must be preserved even when public UV getters hide them.
    const auto* sourceCurve = dynamic_cast<const SmCrvOnSurf*>(source.GetCurve());
    source.SetUVCurve1(sourceCurve->GetUVCurve());
    source.SetUVCurve2(sourceCurve->GetUVCurve());
    SmCurveClassification copy(source);
    SmCurveClassification target;
    initClassification(target, surface, 2);
    for (int i = 0; i < 3; ++i)
    {
        target = source;
        checkCopy(target, expected);
    }
    checkCopy(copy, expected);
    require(copy.GetCurve() != source.GetCurve() && target.GetCurve() != source.GetCurve(), "owned curve was borrowed");
    require(copy.GetUVCurve1() == nullptr && target.GetUVCurve2() == nullptr, "currentness changed");
    if (mode == "--copy-borrowed-surface")
    {
        require(copy.GetSurface1() == &surface && target.GetSurface2() == &surface, "borrowed surface was cloned");
    }
    const auto* identity = target.GetCurve();
    target = target;
    require(target.GetCurve() == identity, "self assignment changed ownership");
    source = SmCurveClassification(); // Destroy source geometry before evaluating the results again.
    checkCopy(copy, expected);
    checkCopy(target, expected);
}

#ifdef _WIN32
struct HeapUsage
{
    unsigned long long bytes = 0;
    unsigned long long blocks = 0;
};

static HeapUsage heapUsage()
{
    HeapUsage usage;
    HANDLE heaps[128];
    const DWORD count = GetProcessHeaps(128, heaps);
    require(count > 0 && count <= 128, "unable to enumerate all process heaps");
    for (DWORD i = 0; i < count; ++i)
    {
        require(HeapLock(heaps[i]) != FALSE, "HeapLock failed");
        PROCESS_HEAP_ENTRY entry = {};
        while (HeapWalk(heaps[i], &entry))
        {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
            {
                usage.bytes += entry.cbData;
                ++usage.blocks;
            }
        }
        const DWORD error = GetLastError();
        HeapUnlock(heaps[i]);
        require(error == ERROR_NO_MORE_ITEMS, "HeapWalk failed");
    }
    return usage;
}

static void copyMemory()
{
    auto body = makeBody(2);
    SmTArray<SmFace*> faces;
    body->GetFaces(faces);
    for (ULONG ownership : { 1UL, 2UL })
    {
        SmCurveClassification source;
        initClassification(source, *const_cast<SmSurface*>(faces[0]->GetSurface()), ownership);
        const auto job = [&source]()
        {
            SmCurveClassification copy(source);
            SmCurveClassification target;
            target = source;
            target = source;
        };
        for (int i = 0; i < 50; ++i)
        {
            job();
        }
        const auto before = heapUsage();
        for (int i = 0; i < 200; ++i)
        {
            job();
        }
        const auto after = heapUsage();
        const auto bytes = static_cast<long long>(after.bytes) - static_cast<long long>(before.bytes);
        const auto blocks = static_cast<long long>(after.blocks) - static_cast<long long>(before.blocks);
        std::cout << "copy ownership=" << ownership << " bytes=" << bytes << " blocks=" << blocks << std::endl;
        require(bytes <= 16384 && blocks <= 64, "outstanding allocations grow after classification copies");
    }
}

static void memory()
{
    std::cout << "Allocation balance, after per-job destruction\n";
    for (int kind = 0; kind < 3; ++kind)
    {
        const auto job = [kind]()
        {
            auto body = makeBody(kind);
            require(body->AssertValid(nullptr, SM_LEVEL_2, SM_WALK) == TRUE, "invalid primitive");
        };
        for (int i = 0; i < 50; ++i)
        {
            job();
        }
        const HeapUsage before = heapUsage();
        for (int i = 0; i < 200; ++i)
        {
            job();
        }
        const HeapUsage after = heapUsage();
        const auto bytes = static_cast<long long>(after.bytes) - static_cast<long long>(before.bytes);
        const auto blocks = static_cast<long long>(after.blocks) - static_cast<long long>(before.blocks);
        std::cout << "kind=" << kind << " bytes=" << bytes << " blocks=" << blocks << std::endl;
        // Allow small one-time CRT allocations, not the deterministic per-check leak.
        require(bytes <= 16384 && blocks <= 64, "outstanding allocations grow after AssertValid");
    }
}
#endif

int main(int argc, char** argv)
{
    try
    {
#ifdef _WIN32
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        wchar_t module[MAX_PATH];
        if (GetModuleFileNameW(GetModuleHandleW(L"SMLib.dll"), module, MAX_PATH))
        {
            std::wcout << L"SMLib: " << module << std::endl;
        }
#endif
        SmApiCreateContext();
        require(argc == 2, "use --aliases, --memory or --copy-<case>");
        const std::string mode(argv[1]);
        if (std::string(argv[1]) == "--aliases")
        {
            aliases();
        }
#ifdef _WIN32
        else if (std::string(argv[1]) == "--memory")
        {
            memory();
        }
        else if (mode == "--copy-memory")
        {
            copyMemory();
        }
#endif
        else if (mode.rfind("--copy-", 0) == 0)
        {
            copyContract(mode);
            std::cout << "PASS " << mode << '\n';
        }
        else
        {
            throw std::runtime_error("unknown test mode");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << std::endl;
        return 1;
    }
}
