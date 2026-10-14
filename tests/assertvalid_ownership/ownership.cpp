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
        require(argc == 2, "use --aliases or --memory");
        if (std::string(argv[1]) == "--aliases")
        {
            aliases();
        }
#ifdef _WIN32
        else if (std::string(argv[1]) == "--memory")
        {
            memory();
        }
#endif
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
