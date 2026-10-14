// SPDX-License-Identifier: Apache-2.0
#include <SmApiBrep.h>
#include <SmApiCurves.h>
#include <SmApiGeneral.h>
#include <SmApiPlanarRegions.h>
#include <SmApiPrimitives.h>
#include <SmApiQueries.h>
#include <SmBSplineCurve.h>
#include <SmBrep.h>
#include <SmLine.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#    include <Windows.h>
#endif

void require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
struct Curves
{
    std::vector<std::unique_ptr<SmCurve>> owners;
    SmTArray<SmCurve*> input;
    void add(SmCurve* c)
    {
        owners.emplace_back(c);
        input.Add(c);
    }
    void line(SmPoint3d a, SmPoint3d b)
    {
        SmLine* c = nullptr;
        require(SmApiCreateLineSegment(a, b, c) == SM_SUCCESS, "line creation");
        add(c);
    }
    void rectangle(double x, double y, double width, double height, double scale = 1, double gap = 0, double dz = 0)
    {
        SmPoint3d a(x * scale, y * scale, 0), b((x + width) * scale, y * scale, 0), c((x + width) * scale, (y + height) * scale, 0),
            d(x * scale, (y + height) * scale, 0);
        // Deliberately shuffled and reversed curves.
        line(c, b);
        line(a, SmPoint3d(d[0], d[1], dz));
        line(d, c);
        line(SmPoint3d(a[0] + gap, a[1], 0), b);
    }
    void circle(double radius)
    {
        SmBSplineCurve* c = nullptr;
        require(SmApiCreateCircle(SmPoint3d(0, 0, 0), radius, c) == SM_SUCCESS, "circle creation");
        add(c);
    }
};
int failures = 0, passes = 0;
template <class F>
void test(const char* name, F f)
{
    try
    {
        f();
        ++passes;
        std::cout << "PASS " << name << std::endl;
    }
    catch (const std::exception& e)
    {
        ++failures;
        std::cout << "FAIL " << name << ": " << e.what() << std::endl;
    }
}
SmPlanarRegionReport build(
    Curves& curves,
    std::size_t regions,
    std::size_t holes,
    double expectedArea,
    SmPlanarRegionOptions options = SmPlanarRegionOptions(),
    SmPoint3d origin = SmPoint3d(0, 0, 0),
    SmVector3d normal = SmVector3d(0, 0, 1)
)
{
    SmBrep* raw = nullptr;
    SmPlanarRegionReport report;
    auto status = SmApiBuildPlanarRegions(curves.input, origin, normal, options, raw, report);
    std::unique_ptr<SmBrep> result(raw);
    if (status != SmPlanarRegionStatus::Success)
    {
        throw std::runtime_error("build status=" + std::to_string(int(status)) + " native=" + std::to_string(report.kernelStatus));
    }
    require(report.regions == regions && report.holes == holes, "wrong region/hole counts");
    require(report.loops.size() == regions + holes, "wrong loop count");
    double sampledArea = 0;
    for (const auto& l : report.loops)
    {
        sampledArea += l.signedArea;
        require((l.signedArea > 0) == (l.depth % 2 == 0), "orientation");
    }
    require(std::abs(sampledArea - expectedArea) <= std::max(1e-8, std::abs(expectedArea) * 2e-4), "sampled area");
    if (raw)
    {
        require(raw->AssertValid(nullptr, SM_LEVEL_2, SM_WALK) == TRUE, "native region validity");
        double area = 0;
        require(SmApiBrepComputeArea(raw, 1e-4, area) == SM_SUCCESS, "area query");
        require(std::abs(area - expectedArea) <= std::max(1e-7, std::abs(expectedArea) * 2e-4), "native area");
    }
    return report;
}
#ifdef _WIN32
std::pair<long long, long long> heapUsage()
{
    HANDLE heaps[128];
    const DWORD count = GetProcessHeaps(128, heaps);
    require(count > 0 && count <= 128, "heap enumeration");
    long long bytes = 0, blocks = 0;
    for (DWORD i = 0; i < count; ++i)
    {
        require(HeapLock(heaps[i]) != FALSE, "heap lock");
        PROCESS_HEAP_ENTRY entry = {};
        while (HeapWalk(heaps[i], &entry))
        {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
            {
                bytes += entry.cbData;
                ++blocks;
            }
        }
        DWORD error = GetLastError();
        HeapUnlock(heaps[i]);
        require(error == ERROR_NO_MORE_ITEMS, "heap walk");
    }
    return { bytes, blocks };
}
#endif
void reject(Curves& curves, SmPlanarRegionStatus expected, SmPlanarRegionOptions options = SmPlanarRegionOptions())
{
    SmBrep* raw = nullptr;
    SmPlanarRegionReport report;
    auto status = SmApiBuildPlanarRegions(curves.input, SmPoint3d(0, 0, 0), SmVector3d(0, 0, 1), options, raw, report);
    std::unique_ptr<SmBrep> cleanup(raw);
    if (status != expected)
    {
        throw std::runtime_error("unexpected status=" + std::to_string(int(status)));
    }
    require(raw == nullptr && report.loops.empty(), "partial result escaped");
}
void checkInvalidProjectedRegion()
{
    SmBrep* body = nullptr;
    require(SmApiCreateCylinder({ 0, 0, 0 }, 5, 10, body) == SM_SUCCESS, "cylinder");
    std::unique_ptr<SmBrep> owner(body);
    require(SmApiRotate(body, { 0, 0, 0 }, { 0, 1, 0 }, 90) == SM_SUCCESS, "rotate cylinder");
    SmTArray<SmCurve*> projected;
    require(SmApiProjectBrepOntoPlane(body, { 0, 0, 0 }, { 0, 0, 1 }, projected) == SM_SUCCESS, "project cylinder");
    Curves c;
    for (ULONG i = 0; i < projected.GetSize(); ++i)
    {
        c.add(projected[i]);
    }
    SmBrep* raw = nullptr;
    SmPlanarRegionReport r;
    auto status = SmApiBuildPlanarRegions(c.input, { 0, 0, 0 }, { 0, 0, 1 }, {}, raw, r);
    std::unique_ptr<SmBrep> result(raw);
    require(
        (status == SmPlanarRegionStatus::InvalidRegion && raw == nullptr && r.loops.empty()) ||
            (status == SmPlanarRegionStatus::Success && raw && raw->AssertValid(nullptr, SM_LEVEL_2, SM_WALK)),
        "invalid native region escaped as success"
    );
}

int main()
{
    SmApiCreateContext();
    test(
        "empty",
        []
        {
            Curves c;
            build(c, 0, 0, 0);
        }
    );
    test(
        "shuffled rectangle",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            build(c, 1, 0, 320);
        }
    );
    test(
        "nested four levels",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 20);
            c.rectangle(2, 2, 16, 16);
            c.rectangle(4, 4, 12, 12);
            c.rectangle(6, 6, 8, 8);
            auto r = build(c, 2, 2, 224);
            unsigned deepest = 0;
            for (auto& l : r.loops)
            {
                deepest = std::max(deepest, l.depth);
            }
            require(deepest == 3, "nesting depth");
        }
    );
    test(
        "disjoint",
        []
        {
            Curves c;
            c.rectangle(0, 0, 4, 4);
            c.rectangle(10, 0, 4, 4);
            build(c, 2, 0, 32);
        }
    );
    test(
        "circle annulus",
        []
        {
            Curves c;
            c.circle(5);
            c.circle(2);
            build(c, 1, 1, 21 * 3.141592653589793);
        }
    );
    test(
        "normal reversal",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            build(c, 1, 0, 320, {}, SmPoint3d(0, 0, 0), SmVector3d(0, 0, -5));
        }
    );
    for (double scale : { 0.01, 1.0, 100.0 })
    {
        test(
            ("scale " + std::to_string(scale)).c_str(),
            [scale]
            {
                Curves c;
                c.rectangle(0, 0, 20, 16, scale);
                c.rectangle(6, 4, 8, 8, scale);
                SmPlanarRegionOptions o;
                o.joinTolerance *= scale;
                o.planeTolerance *= scale;
                o.chordTolerance *= scale;
                o.separationTolerance *= scale;
                build(c, 1, 1, 256 * scale * scale, o);
            }
        );
    }
    test(
        "gap below tolerance",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16, 1, 0.5e-6);
            build(c, 1, 0, 320);
        }
    );
    test(
        "gap above tolerance",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20000, 16000, 1, 2e-6);
            reject(c, SmPlanarRegionStatus::OpenBoundary);
        }
    );
    test(
        "open",
        []
        {
            Curves c;
            c.line({ 0, 0, 0 }, { 1, 0, 0 });
            reject(c, SmPlanarRegionStatus::OpenBoundary);
        }
    );
    test(
        "branch",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            c.line({ 0, 0, 0 }, { -1, 0, 0 });
            reject(c, SmPlanarRegionStatus::AmbiguousJoin);
        }
    );
    test(
        "duplicate",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            c.line({ 0, 0, 0 }, { 20, 0, 0 });
            reject(c, SmPlanarRegionStatus::AmbiguousJoin);
        }
    );
    test(
        "crossing",
        []
        {
            Curves c;
            c.rectangle(0, 0, 10, 10);
            c.rectangle(5, 5, 10, 10);
            reject(c, SmPlanarRegionStatus::IntersectingOrCloseBoundaries);
        }
    );
    test(
        "nonplanar",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16, 1, 0, 2e-6);
            reject(c, SmPlanarRegionStatus::NonPlanar);
        }
    );
    test(
        "tilted plane",
        []
        {
            Curves c;
            c.line({ 2, 3, 4 }, { 12, 3, 4 });
            c.line({ 12, 3, 4 }, { 12, 9, 12 });
            c.line({ 12, 9, 12 }, { 2, 9, 12 });
            c.line({ 2, 9, 12 }, { 2, 3, 4 });
            build(c, 1, 0, 100, {}, SmPoint3d(2, 3, 4), SmVector3d(0, -0.8, 0.6));
        }
    );
    test(
        "translated plane",
        []
        {
            Curves c;
            c.rectangle(1000000, 2000000, 20, 16);
            build(c, 1, 0, 320, {}, SmPoint3d(1000000, 2000000, 0));
        }
    );
    for (double sign : { -1.0, 1.0 })
    {
        test(
            ("curved nonplanarity " + std::to_string(sign)).c_str(),
            [sign]
            {
                Curves c;
                SmTArray<SmPoint3d> p;
                p.Add({ 0, 0, 0 });
                p.Add({ 3, 0, sign * 1e-5 });
                p.Add({ 7, 0, -sign * 1e-5 });
                p.Add({ 10, 0, 0 });
                SmTArray<double> knots;
                knots.Add(0);
                knots.Add(1);
                SmTArray<ULONG> mult;
                mult.Add(4);
                mult.Add(4);
                SmBSplineCurve* curve = nullptr;
                require(SmApiCreateCanonicalCurve(p, knots, mult, nullptr, 3, SM_CF_UNSPECIFIED, curve) == SM_SUCCESS, "spline creation");
                c.add(curve);
                c.line({ 10, 0, 0 }, { 10, 10, 0 });
                c.line({ 10, 10, 0 }, { 0, 10, 0 });
                c.line({ 0, 10, 0 }, { 0, 0, 0 });
                reject(c, SmPlanarRegionStatus::NonPlanar);
            }
        );
    }
    test(
        "touching loop interiors",
        []
        {
            Curves c;
            c.rectangle(0, 0, 10, 10);
            c.rectangle(10, 3, 5, 4);
            reject(c, SmPlanarRegionStatus::IntersectingOrCloseBoundaries);
        }
    );
    test(
        "separation guard",
        []
        {
            Curves c;
            c.rectangle(0, 0, 10, 10);
            c.rectangle(10.0001, 3, 5, 4);
            reject(c, SmPlanarRegionStatus::IntersectingOrCloseBoundaries);
        }
    );
    test(
        "empty section",
        []
        {
            SmBrep* body = nullptr;
            require(SmApiCreateBox({ 0, 0, 0 }, 20, 16, 10, body) == SM_SUCCESS, "box");
            std::unique_ptr<SmBrep> owner(body);
            SmBrep* raw = nullptr;
            SmPlanarRegionReport r;
            auto s = SmApiSectionPlanarRegions(body, { 0, 0, 20 }, { 0, 0, 1 }, {}, raw, r);
            std::unique_ptr<SmBrep> out(raw);
            require(s == SmPlanarRegionStatus::Success && raw == nullptr && r.loops.empty(), "empty section");
        }
    );
    test(
        "input lifetime after success and failure",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            SmPoint3d before, after;
            require(c.input[0]->EvaluatePoint(c.input[0]->GetNaturalInterval().GetMin(), before) == SM_SUCCESS, "before evaluate");
            build(c, 1, 0, 320);
            SmPlanarRegionOptions bad;
            bad.maxPairTests = 1;
            reject(c, SmPlanarRegionStatus::LimitExceeded, bad);
            require(
                c.input[0]->EvaluatePoint(c.input[0]->GetNaturalInterval().GetMin(), after) == SM_SUCCESS && before.DistanceBetween(after) == 0,
                "input changed or destroyed"
            );
        }
    );
    test(
        "budget",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            SmPlanarRegionOptions o;
            o.maxPairTests = 1;
            reject(c, SmPlanarRegionStatus::LimitExceeded, o);
        }
    );
    test(
        "invalid tolerance",
        []
        {
            Curves c;
            c.rectangle(0, 0, 20, 16);
            SmPlanarRegionOptions o;
            o.joinTolerance = 0;
            reject(c, SmPlanarRegionStatus::InvalidInput, o);
        }
    );
    test(
        "section box",
        []
        {
            SmBrep* body = nullptr;
            require(SmApiCreateBox({ 0, 0, 0 }, 20, 16, 10, body) == SM_SUCCESS, "box");
            std::unique_ptr<SmBrep> owner(body);
            SmBrep* raw = nullptr;
            SmPlanarRegionReport r;
            auto s = SmApiSectionPlanarRegions(body, { 0, 0, 5 }, { 0, 0, 1 }, {}, raw, r);
            std::unique_ptr<SmBrep> out(raw);
            require(s == SmPlanarRegionStatus::Success && r.regions == 1, "section region");
        }
    );
    test(
        "selected projected box boundary",
        []
        {
            SmBrep* body = nullptr;
            require(SmApiCreateBox({ 0, 0, 0 }, 20, 16, 10, body) == SM_SUCCESS, "box");
            std::unique_ptr<SmBrep> owner(body);
            SmTArray<SmCurve*> projected;
            require(SmApiProjectBrepOntoPlane(body, { 0, 0, 0 }, { 0, 0, 1 }, projected) == SM_SUCCESS, "projection");
            Curves c;
            for (ULONG i = 0; i < projected.GetSize(); ++i)
            {
                c.add(projected[i]);
            }
            build(c, 1, 0, 320);
        }
    );
#ifdef _WIN32
    test(
        "repeated allocation balance",
        []
        {
            const auto job = []()
            {
                Curves c;
                c.rectangle(0, 0, 20, 16);
                c.rectangle(6, 4, 8, 8);
                build(c, 1, 1, 256);
                Curves open;
                open.line({ 0, 0, 0 }, { 1, 0, 0 });
                reject(open, SmPlanarRegionStatus::OpenBoundary);
                checkInvalidProjectedRegion();
            };
            for (int i = 0; i < 30; ++i)
            {
                job();
            }
            const auto before = heapUsage();
            for (int i = 0; i < 200; ++i)
            {
                job();
            }
            const auto after = heapUsage();
            std::cout << "MEM bytes=" << after.first - before.first << " blocks=" << after.second - before.second << std::endl;
            require(after.first - before.first <= 16384 && after.second - before.second <= 64, "region allocations grow");
        }
    );
#endif
    test("reject invalid native projection region", checkInvalidProjectedRegion);
    std::cout << "RESULT passes=" << passes << " failures=" << failures << std::endl;
    return failures ? 1 : 0;
}
