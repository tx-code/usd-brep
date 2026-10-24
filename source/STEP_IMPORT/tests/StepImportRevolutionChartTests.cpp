// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "PublicSorReprojectionFixture.h"
#include "StepImportDerivedUv.h"
#include "StepImportRevolutionChart.h"

#include <SmAssertArray.h>
#include <SmCacheMgr.h>
#include <SmEdgeuse.h>
#include <SmNativeArchive.h>
#include <SmVertexuse.h>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <vector>

using namespace step_import;
using namespace step_import::detail;
using public_sor_repro::Require;
namespace
{
using Values = std::vector<double>;
Values Stored(const SmBSplineCurve& c)
{
    ULONG n = 0;
    double* p = nullptr;
    public_sor_repro::RequireStatus(c.GetControlPointsPointer(n, p));
    return { p, p + std::size_t(n) * 4 };
}
void Install(public_sor_repro::Fixture& f)
{
    SmTArray<SmEdgeuse*> uses;
    f.face->GetUpwardEdgeuses(uses);
    for (ULONG i = 0; i < uses.GetSize(); ++i)
    {
        auto trim = ProjectDerivedUv(
            *uses[i],
            .001,
            []() -> Result<void>
            {
                return {};
            },
            true
        );
        Require(trim && trim->status == DerivedUvStatus::Ready && trim->curve, "Public chart projection prepares every boundary");
        auto* adopted = trim->curve.get();
        uses[i]->SetUVTrimCurve(adopted, 0, TRUE);
        Require(adopted->GetOwner() == uses[i] || adopted->GetOwner() == uses[i]->GetMate(), "Public derived owner");
        trim->curve.release();
    }
}
void Finding(SmAssertArray& into, const SmBSplineCurve& curve, ULONG test = 13)
{
    into.AddUniqueDeleteDuplicate(new SmAssertReport(
        &curve,
        test,
        SmBSplineCurve_TYPE,
        &curve,
        SmBSplineCurve_TYPE,
        _T("SmBSplineCurve"),
        nullptr,
        0,
        nullptr,
        SmTol3d(),
        0,
        0,
        _T("public synthetic attribution")
#ifdef SM_DEBUG_CODE
        ,
        _T("public"),
        1
#endif
    ));
}
struct Trim
{
    SmEdgeuse* owner;
    Values raw;
    SmExtent1d interval;
    std::vector<SmPoint3d> lifted;
};
void Run(const std::filesystem::path& output, bool warm, bool rational = false)
{
    SmContext context;
    const SmExtent1d smallU(std::ldexp(2., -16), std::ldexp(6., -16));
    auto f = public_sor_repro::Create(context, true, true, smallU);
    Install(f);
    if (rational)
    {
        // A public synthetic DERIVED net to exercise homogeneous X with unequal
        // weights. This certificate test does not claim complete model acceptance
        // or replace a defining source curve with a fit.
        auto* use = f.edge->GetPrimaryEdgeuse();
        auto* old = use->GetUVTrimCurve();
        gw_CPOINT cp[3];
        for (unsigned j = 0; j < 3; ++j)
        {
            SmPoint3d p;
            public_sor_repro::RequireStatus(old->EvaluatePoint(-3 + 6 * j, p));
            const double w = j == 1 ? 1.3 : 1;
            cp[j] = { p.x * w, p.y * w, 0, w };
        }
        double knots[] = { -3, -3, -3, 9, 9, 9 };
        auto replacement = std::unique_ptr<SmBSplineCurve>(new (context) SmBSplineCurve(2, 2, 5, 2, cp, knots, &context));
        Require(replacement->IsRational(), "Unequal positive rational weights remain rational");
        use->SetUVTrimCurve(replacement.get(), 0, TRUE);
        replacement.release();
    }
    auto* s = static_cast<SmSurfOfRevolution*>(f.face->GetSurface());
    const auto stepDomain = s->GetSTEPUVDomain();
    SmBSplineCurve* gen = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    public_sor_repro::RequireStatus(s->GetCanonical(gen, origin, axis));
    const auto generator = Stored(*gen);
    const auto generatorStep = gen->GetSTEPInterval();
    const auto sourceUv = Stored(*static_cast<const SmBSplineCurve*>(static_cast<SmCrvOnSurf*>(f.edge->GetCurve())->GetUVCurve()));
    const auto tolerance = f.edge->GetTolerance();
    SmTArray<SmEdgeuse*> uses;
    f.face->GetUpwardEdgeuses(uses);
    std::map<SmBSplineCurve*, Trim> original;
    if (warm)
    {
        // Native lazy queries can refine derived trim caches. Finish ALL warm
        // queries before capturing the actual normalization entry state.
        for (ULONG i = 0; i < uses.GetSize(); ++i)
        {
            SmExtent3d box;
            public_sor_repro::RequireStatus(uses[i]->GetUVTrimCurve()->CalculateBoundingBox(uses[i]->GetEdge()->GetInterval(), &box));
            SmPoint2d vertexUv;
            public_sor_repro::RequireStatus(uses[i]->GetVertexuse()->ComputeUVPoint(vertexUv));
        }
    }
    for (ULONG i = 0; i < uses.GetSize(); ++i)
    {
        SmEdgeuse* owner = nullptr;
        auto* uv = uses[i]->GetUVTrimCurve(&owner);
        if (original.contains(uv))
        {
            continue;
        }
        auto interval = owner->GetEdge()->GetInterval();
        Trim t{ owner, Stored(*uv), interval, {} };
        for (unsigned j = 0; j <= 128; ++j)
        {
            SmPoint3d p, lifted;
            public_sor_repro::RequireStatus(uv->EvaluatePoint(interval.Evaluate(double(j) / 128), p));
            public_sor_repro::RequireStatus(s->EvaluatePoint({ p.x, p.y }, lifted));
            t.lifted.push_back(lifted);
        }
        original.emplace(uv, std::move(t));
    }
    SmAssertArray only;
    Finding(only, *uses[0]->GetUVTrimCurve());
    Require(RevolutionChartFaces(only).has_value(), "Only stored derived UV small-radius attribution admits");
    only[0]->m_lReportingType = SmCurve_TYPE;
    Require(!RevolutionChartFaces(only), "Same test number from another reporter refuses");
    only[0]->m_lReportingType = SmBSplineCurve_TYPE;
    SmAssertArray mixed;
    Finding(mixed, *uses[0]->GetUVTrimCurve());
    Finding(mixed, *uses[1]->GetUVTrimCurve(), 14);
    Require(!RevolutionChartFaces(mixed), "Mixed failure set refuses");
    SmAssertArray defining;
    Finding(defining, *static_cast<const SmBSplineCurve*>(static_cast<SmCrvOnSurf*>(f.edge->GetCurve())->GetUVCurve()));
    Require(!RevolutionChartFaces(defining), "Defining source COS UV refuses");
    std::size_t calls = 0;
    if (warm)
    {
        Require(SmCacheMgr::GetOrCreateObjectCache(SM_OC_SURFACE, s) != nullptr, "Warm surface tree exists");
    }
    auto r = ConditionRevolutionChart(
        *f.face,
        100000,
        [&]() -> Result<void>
        {
            ++calls;
            return {};
        }
    );
    Require(r && *r, "One independently selected U chart applies");
    const auto& change = **r;
    Require(change.verifyStoredIdentity && change.verifyStoredIdentity(), "Installed stored-net identity is retained");
    Require(SmCacheMgr::GetObjectCache(SM_OC_SURFACE, s) == nullptr, "Old chart surface tree is removed");
    Require(
        change.conditionAfter * 2 <= change.conditionBefore && change.exponent > 0 && change.trims == original.size(),
        "Metric improves and each owner maps once"
    );
    Require(
        change.after[0] == std::ldexp(change.before[0], change.exponent) && change.before[0] != 0 && change.after[1] == change.before[1] &&
            change.after[3] == change.before[3],
        "Pure nonzero-origin U map, unchanged V"
    );
    for (const auto& [old, t] : original)
    {
        (void)old;
        auto* uv = t.owner->GetUVTrimCurve();
        const auto raw = Stored(*uv);
        Require(t.owner->GetMate()->GetUVTrimCurve() == uv, "Mate shares the once-mapped stored owner curve");
        Require(raw.size() == t.raw.size() && uv->GetNaturalInterval() == t.interval, "No trim fit or edge t change");
        for (std::size_t j = 0; j < raw.size(); ++j)
        {
            Require(raw[j] == (j % 4 == 0 ? std::ldexp(t.raw[j], change.exponent) : t.raw[j]), "Homogeneous X only; Y Z W unchanged");
        }
        for (unsigned j = 0; j <= 128; ++j)
        {
            SmPoint3d p, lifted;
            public_sor_repro::RequireStatus(uv->EvaluatePoint(t.interval.Evaluate(double(j) / 128), p));
            public_sor_repro::RequireStatus(s->EvaluatePoint({ p.x, p.y }, lifted));
            Require(lifted.DistanceBetween(t.lifted[j]) <= 1e-10, "Independent actual-t lifted observations unchanged");
        }
    }
    Require(
        s->GetSTEPUVDomain() == stepDomain && Stored(*gen) == generator && gen->GetSTEPInterval() == generatorStep &&
            Stored(*static_cast<const SmBSplineCurve*>(static_cast<SmCrvOnSurf*>(f.edge->GetCurve())->GetUVCurve())) == sourceUv &&
            f.edge->GetTolerance() == tolerance,
        "Source definition, UV, generator STEP and work tolerance unchanged"
    );
    for (const double angle : { 0., 45., 89.999999, 90., 90.000001, 180., 270., 359.999999 })
    {
        const SmPoint2d step(angle, 2);
        SmPoint3d before, after;
        SmPoint2d oldUv, newUv;
        public_sor_repro::RequireStatus(change.original->EvaluateSTEPPoint(step, before));
        public_sor_repro::RequireStatus(s->EvaluateSTEPPoint(step, after));
        public_sor_repro::RequireStatus(change.original->ConvertUVFromSTEPToNURBS(step, oldUv));
        public_sor_repro::RequireStatus(s->ConvertUVFromSTEPToNURBS(step, newUv));
        Require(
            before.DistanceBetween(after) <= 1e-10 && std::abs(newUv.x - std::ldexp(oldUv.x, change.exponent)) <= 1e-10 &&
                std::abs(newUv.y - oldUv.y) <= 1e-10,
            "Cold/warm polar converter and knot-side STEP observations retain correspondence"
        );
    }
    Require(change.verifyStoredIdentity(), "Read-only consumers retain whole chart identity");
    if (!rational)
    {
        // The unequal-weight synthetic derived countertest above is only an
        // affine certificate. The genuine projected public BRep additionally
        // exercises validators, independent copies and both native codecs.
        SmAssertArray pointers, deep;
        Require(
            f.brep->ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize() && f.brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
                !deep.GetSize(),
            "Whole conditioned public graph passes pointer/LEVEL2"
        );
        Require(change.verifyStoredIdentity(), "Whole native validation retains affine witness");
        SmContext copyContext;
        auto copy = std::unique_ptr<SmBrep>(new (copyContext) SmBrep(*f.brep, nullptr, nullptr, TRUE, FALSE));
        for (const auto encoding : { SM_BINARY, SM_ASCII })
        {
            const auto path = output / (encoding == SM_BINARY ? "chart.smb" : "chart.sma");
            SmTArray<SmCurve*> curves;
            SmTArray<SmSurface*> surfaces;
            SmTArray<long> trees;
            SmTArray<SmBrep*> bodies;
            bodies.Add(copy.get());
            public_sor_repro::RequireStatus(SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, bodies, encoding, TRUE, FALSE));
            SmContext readContext;
            SmTArray<SmCurve*> readCurves;
            SmTArray<SmSurface*> readSurfaces;
            SmTArray<long> readTrees;
            SmTArray<SmBrep*> readBodies;
            SmObjsDelete<SmCurve*> dc(&readCurves);
            SmObjsDelete<SmSurface*> ds(&readSurfaces);
            SmObjsDelete<SmBrep*> db(&readBodies);
            public_sor_repro::RequireStatus(
                SmNativeArchive::ReadPartFromFile(readContext, path.c_str(), readCurves, readSurfaces, readTrees, readBodies, encoding)
            );
            Require(readBodies.GetSize() == 1, "Complete conditioned graph archive");
            SmTArray<SmFace*> readFaces;
            readBodies[0]->GetFaces(readFaces);
            Require(
                readFaces.GetSize() == 1 && readFaces[0]->GetSurface()->GetType() == SmSurfOfRevolution_TYPE &&
                    readFaces[0]->GetSurface()->GetNaturalUVDomain() == s->GetNaturalUVDomain() &&
                    readFaces[0]->GetSurface()->GetSTEPUVDomain() == stepDomain,
                "Copy/archive retain native chart and source STEP role"
            );
            SmTArray<SmEdge*> beforeEdges, afterEdges;
            copy->GetEdges(beforeEdges);
            readBodies[0]->GetEdges(afterEdges);
            Require(beforeEdges.GetSize() == afterEdges.GetSize(), "Archive preserves complete edge inventory");
            for (ULONG edge = 0; edge < beforeEdges.GetSize(); ++edge)
            {
                Require(
                    beforeEdges[edge]->GetInterval() == afterEdges[edge]->GetInterval() &&
                        afterEdges[edge]->GetCurve()->GetType() == SmCrvOnSurf_TYPE,
                    "Archive keeps defining composition and edge t"
                );
                for (unsigned sample = 0; sample <= 128; ++sample)
                {
                    SmPoint3d a, b;
                    const auto t = beforeEdges[edge]->GetInterval().Evaluate(double(sample) / 128);
                    public_sor_repro::RequireStatus(beforeEdges[edge]->GetCurve()->EvaluatePoint(t, a));
                    public_sor_repro::RequireStatus(afterEdges[edge]->GetCurve()->EvaluatePoint(t, b));
                    Require(a.DistanceBetween(b) <= 1e-10, "Independent copy/archive source composition observations agree");
                }
            }
        }
        Require(change.verifyStoredIdentity(), "Copy/archive cannot alter owning chart witness");
    }
    // Every cancellation discards its own unpublished graph. This helper does
    // not promise rollback after application starts; publication owns that rule.
    for (std::size_t stop = 1; stop <= calls; ++stop)
    {
        auto other = public_sor_repro::Create(context, true, true, smallU);
        Install(other);
        std::size_t n = 0;
        auto cancelled = ConditionRevolutionChart(
            *other.face,
            100000,
            [&]() -> Result<void>
            {
                if (++n == stop)
                {
                    return tl::unexpected(Failure{ Status::Cancelled, "ChartCancel", "public", 7, "Caller" });
                }
                return {};
            }
        );
        Require(!cancelled && cancelled.error().status == Status::Cancelled && cancelled.error().record == 7, "Cancellation retains caller identity");
    }
}
void Adverse()
{
    const double tiny = std::numeric_limits<double>::denorm_min(), normal = std::numeric_limits<double>::min();
    Require(ExactRevolutionChartScale(normal, normal / 2, -1), "Exactly representable subnormal admits");
    Require(!ExactRevolutionChartScale(tiny, 0., -1) && !ExactRevolutionChartScale(3 * tiny, 2 * tiny, -1), "Lost subnormal bits refuse");
    Require(
        !ExactRevolutionChartScale(std::numeric_limits<double>::max(), INFINITY, 1) && !ExactRevolutionChartScale(-0., +0., 1),
        "Overflow and sign loss refuse"
    );
    SmContext context;
    auto balanced = public_sor_repro::Create(context, true, true, SmExtent1d(2, 6));
    Install(balanced);
    const auto old = balanced.face->GetUVDomain();
    auto absent = ConditionRevolutionChart(
        *balanced.face,
        100000,
        []() -> Result<void>
        {
            return {};
        }
    );
    Require(absent && !*absent && balanced.face->GetUVDomain() == old, "Balanced chart retains original candidate");
    const SmExtent1d smallU(std::ldexp(2., -16), std::ldexp(6., -16));
    auto limited = public_sor_repro::Create(context, true, true, smallU);
    Install(limited);
    const auto oldLimited = limited.face->GetUVDomain();
    auto capped = ConditionRevolutionChart(
        *limited.face,
        1,
        []() -> Result<void>
        {
            return {};
        }
    );
    Require(
        !capped && capped.error().status == Status::ResourceLimit && limited.face->GetUVDomain() == oldLimited,
        "Proof allocation bound refuses before mutation"
    );
    auto swapped = public_sor_repro::Create(context, true, true, smallU);
    public_sor_repro::RequireStatus(swapped.face->GetSurface()->SwapUV());
    const auto swappedDomain = swapped.face->GetSurface()->GetNaturalUVDomain();
    auto wrongAxis = ConditionRevolutionChart(
        *swapped.face,
        100000,
        []() -> Result<void>
        {
            return {};
        }
    );
    Require(
        wrongAxis && !*wrongAxis && swapped.face->GetSurface()->GetNaturalUVDomain() == swappedDomain,
        "Swapped rotation axis refuses without edits"
    );
    for (bool wrap : { false, true })
    {
        // Deliberately borrowed source dependencies, with VALID edgeuse owner
        // links. These graphs are private counterexamples, never published.
        auto f = public_sor_repro::Create(context, true, true, smallU);
        Install(f);
        auto* uv = f.edge->GetPrimaryEdgeuse()->GetUVTrimCurve();
        auto wrapper = std::unique_ptr<SmSTEPSurface>(new (context) SmSTEPSurface(*f.face->GetSurface(), FALSE));
        wrapper->SetContext(&context);
        auto borrowed = std::unique_ptr<SmCurve>(new (context) SmCrvOnSurf(*uv, wrap ? *wrapper : *f.face->GetSurface(), nullptr, 0, &context));
        f.edge->SetCurve(borrowed.get(), TRUE, FALSE, FALSE);
        borrowed->SetOwner(f.edge);
        borrowed.release();
        const auto raw = Stored(*uv);
        const auto domain = f.face->GetUVDomain();
        auto refused = ConditionRevolutionChart(
            *f.face,
            100000,
            []() -> Result<void>
            {
                return {};
            }
        );
        Require(
            refused && !*refused && Stored(*uv) == raw && f.face->GetUVDomain() == domain,
            "Borrowed defining UV/support aliases refuse before mutation"
        );
        f.brep.reset();
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Pass public chart output directory");
        const auto output = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(output);
        Run(output, false);
        Run(output, true);
        Run(output, true, true);
        Adverse();
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
