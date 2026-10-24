// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmBSplineSurface.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmCylinder.h>
#include <SmLine.h>
#include <SmMessages.h>
#include <SmPlane.h>
#include <SmSTEPSurface.h>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <typeinfo>

namespace curve_copy_controls
{
bool Ownership(unsigned flag);
bool Definition(bool warm);
bool WrapperArchive(const char* outputDirectory, bool ascii);
} // namespace curve_copy_controls

namespace
{
struct Counts
{
    unsigned calls = 0;
    unsigned copies = 0;
    unsigned originalsDeleted = 0;
    unsigned copiesDeleted = 0;
    SmCurve* liveCurveCandidate = nullptr;
    SmSurface* liveSurfaceCandidate = nullptr;
};

// Counters observe the nested object's independent lifetime. Evaluation stays
// on the real bounded analytic cylinder; no approximate geometry is substituted.
class CountedCylinder final : public SmCylinder
{
    Counts* m_counts;
    bool m_original;

public:

    CountedCylinder(const SmContext& context, Counts& counts)
        : SmCylinder(SmPoint3d(1., 2., 3.), SmVector3d(1., 0., 0.), SmVector3d(0., 1., 0.), 3., 0., 180., 4., FALSE, FALSE, FALSE, &context),
          m_counts(&counts),
          m_original(true)
    {
    }
    CountedCylinder(const CountedCylinder& source) : SmCylinder(source), m_counts(source.m_counts), m_original(false)
    {
        ++m_counts->copies;
        m_counts->liveSurfaceCandidate = this;
    }
    ~CountedCylinder() override
    {
        if (m_counts->liveSurfaceCandidate == this)
        {
            m_counts->liveSurfaceCandidate = nullptr;
        }
        if (m_original)
        {
            ++m_counts->originalsDeleted;
        }
        else
        {
            ++m_counts->copiesDeleted;
        }
    }
    SmStatus Copy(const SmContext& context, SmSurface*& output) const override
    {
        ++m_counts->calls;
        output = new (context) CountedCylinder(*this);
        return output ? SM_SUCCESS : SM_ERR;
    }
};

#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
SmStatus VirtualSurfaceCopy(const SmSurface& source, const SmContext& context, SmSurface*& output)
{
    // Keep dispatch through the existing SmSurface virtual slot. Old-DLL runs
    // use baseline declarations; noinline alone cannot prevent a new-header
    // consumer from importing the wrapper's new Copy symbol (see README).
    return source.Copy(context, output);
}

bool Check(const char* name, bool value)
{
    std::cout << name << ": " << (value ? "true" : "false") << '\n';
    return value;
}

bool StepSurfaceCopy(bool ownsBase = true)
{
    SmContext sourceContext, copyContext;
    Counts counts;
    std::unique_ptr<CountedCylinder> cylinder(new (sourceContext) CountedCylinder(sourceContext, counts));
    std::unique_ptr<SmSTEPSurface> source(new (sourceContext) SmSTEPSurface(*cylinder, ownsBase ? TRUE : FALSE));
    if (ownsBase)
    {
        cylinder.release();
    }
    SmSurface* rawCopy = nullptr;
    const auto status = VirtualSurfaceCopy(*source, copyContext, rawCopy);
    const bool succeeds = status == SM_SUCCESS && rawCopy;
    const bool independentWrapper = rawCopy && rawCopy != source.get();
    const bool independentBase = counts.copies == 1;
    const bool expectedType = rawCopy && rawCopy->GetType() == SmSTEPSurface_TYPE;
    const bool expectedContext = rawCopy && rawCopy->GetContext() == &copyContext;
    SmPoint3d expected, actual;
    const bool sourceEvaluates = source->EvaluatePoint(SmPoint2d(37., 2.), expected) == SM_SUCCESS;
    bool sameParameterEvaluation = false;
    bool evaluatesAfterSourceDestruction = false;
    bool borrowedBaseSurvivesWrapper = ownsBase;
    if (independentWrapper && independentBase)
    {
        std::unique_ptr<SmSurface> copy(rawCopy);
        sameParameterEvaluation = sourceEvaluates && copy->EvaluatePoint(SmPoint2d(37., 2.), actual) == SM_SUCCESS &&
                                  expected.DistanceBetween(actual) <= 1e-12;
        source.reset();
        if (!ownsBase)
        {
            borrowedBaseSurvivesWrapper = counts.originalsDeleted == 0;
            cylinder.reset();
        }
        evaluatesAfterSourceDestruction = counts.originalsDeleted == 1 && copy->EvaluatePoint(SmPoint2d(37., 2.), actual) == SM_SUCCESS &&
                                          expected.DistanceBetween(actual) <= 1e-12;
    }
    else
    {
        // The baseline returns an error and null. A malformed shallow wrapper
        // candidate cannot be safely destroyed through its public interface;
        // avoid executing a double delete while recording this failed contract.
        source.reset();
        borrowedBaseSurvivesWrapper = ownsBase || counts.originalsDeleted == 0;
        cylinder.reset();
    }
    bool passed = true;
    passed &= Check("step_surface_copy_succeeds", succeeds);
    passed &= Check("step_surface_copy_preserves_type", expectedType);
    passed &= Check("step_surface_copy_uses_requested_context", expectedContext);
    passed &= Check("step_surface_copy_owns_independent_base", independentWrapper && independentBase);
    passed &= Check("step_surface_copy_preserves_same_parameter_evaluation", sameParameterEvaluation);
    passed &= Check("step_surface_copy_survives_source_destruction", evaluatesAfterSourceDestruction);
    passed &= Check("step_surface_copy_keeps_borrowed_source_base_alive", borrowedBaseSurvivesWrapper);
    passed &= Check("step_surface_copy_releases_each_base_once", counts.originalsDeleted == 1 && counts.copiesDeleted == 1);
    return passed;
}

enum class CopyBehavior
{
    Success,
    Error,
    CandidateError,
    Alias,
    Null,
    WrongContext,
    WrongType,
    CandidateThrow
};

class CountedLine final : public SmLine
{
    Counts* m_counts;
    bool m_original;
    CopyBehavior m_behavior;

public:

    CountedLine(const SmContext& context, Counts& counts, CopyBehavior behavior = CopyBehavior::Success)
        : SmLine(SmPoint3d(0.25, 0.5, 0.), SmVector3d(1., 0.5, 0.), SmExtent1d(-2., 3.), 0.125, 2, &context),
          m_counts(&counts),
          m_original(true),
          m_behavior(behavior)
    {
    }
    CountedLine(const CountedLine& source) : SmLine(source), m_counts(source.m_counts), m_original(false), m_behavior(source.m_behavior)
    {
        ++m_counts->copies;
        m_counts->liveCurveCandidate = this;
    }
    ~CountedLine() override
    {
        if (m_counts->liveCurveCandidate == this)
        {
            m_counts->liveCurveCandidate = nullptr;
        }
        if (m_original)
        {
            ++m_counts->originalsDeleted;
        }
        else
        {
            ++m_counts->copiesDeleted;
        }
    }
    SmStatus Copy(const SmContext& context, SmCurve*& output) const override
    {
        ++m_counts->calls;
        if (m_behavior == CopyBehavior::Error)
        {
            return SM_ERR_NOT_CONVERGING;
        }
        if (m_behavior == CopyBehavior::Alias)
        {
            output = const_cast<CountedLine*>(this);
            return SM_SUCCESS;
        }
        output = new (context) CountedLine(*this);
        if (m_behavior == CopyBehavior::CandidateError)
        {
            return SM_ERR_NOT_CONVERGING;
        }
        if (m_behavior == CopyBehavior::CandidateThrow)
        {
            throw std::bad_alloc();
        }
        return output ? SM_SUCCESS : SM_ERR;
    }
};

class CandidateSpline final : public SmBSplineSurface
{
    Counts* m_counts;

public:

    CandidateSpline(const SmBSplineSurface& source, Counts& counts) : SmBSplineSurface(source), m_counts(&counts)
    {
        ++m_counts->copies;
        m_counts->liveSurfaceCandidate = this;
    }
    ~CandidateSpline() override
    {
        if (m_counts->liveSurfaceCandidate == this)
        {
            m_counts->liveSurfaceCandidate = nullptr;
        }
        ++m_counts->copiesDeleted;
    }
};

class FailingPlane final : public SmPlane
{
    Counts* m_counts;
    bool m_original;
    CopyBehavior m_behavior;

public:

    FailingPlane(const SmContext& context, Counts& counts, CopyBehavior behavior = CopyBehavior::Error)
        : SmPlane(SmPoint3d(1., 2., 3.), SmVector3d(1., 0., 0.), SmVector3d(0., 1., 0.), SmVector2d(2., 3.), SmExtent2d(-2., -2., 4., 4.), &context),
          m_counts(&counts),
          m_original(true),
          m_behavior(behavior)
    {
    }
    FailingPlane(const FailingPlane& source) : SmPlane(source), m_counts(source.m_counts), m_original(false), m_behavior(source.m_behavior)
    {
        ++m_counts->copies;
        m_counts->liveSurfaceCandidate = this;
    }
    ~FailingPlane() override
    {
        if (m_counts->liveSurfaceCandidate == this)
        {
            m_counts->liveSurfaceCandidate = nullptr;
        }
        if (m_original)
        {
            ++m_counts->originalsDeleted;
        }
        else
        {
            ++m_counts->copiesDeleted;
        }
    }
    SmStatus Copy(const SmContext& context, SmSurface*& output) const override
    {
        ++m_counts->calls;
        // Error outputs need not be assigned by the child. This also keeps the
        // old constructor's aliased output nonnull until test-only cleanup.
        if (m_behavior == CopyBehavior::Error)
        {
            return SM_ERR_NOT_CONVERGING;
        }
        if (m_behavior == CopyBehavior::Alias)
        {
            output = const_cast<FailingPlane*>(this);
            return SM_SUCCESS;
        }
        if (m_behavior == CopyBehavior::Null)
        {
            output = nullptr;
            return SM_SUCCESS;
        }
        if (m_behavior == CopyBehavior::WrongContext)
        {
            output = new (*GetContext()) FailingPlane(*this);
            return SM_SUCCESS;
        }
        if (m_behavior == CopyBehavior::WrongType)
        {
            output = new (context) CandidateSpline(*this, *m_counts);
            return SM_SUCCESS;
        }
        output = new (context) FailingPlane(*this);
        if (m_behavior == CopyBehavior::CandidateError)
        {
            return SM_ERR_NOT_CONVERGING;
        }
        if (m_behavior == CopyBehavior::CandidateThrow)
        {
            throw std::bad_alloc();
        }
        return output ? SM_SUCCESS : SM_ERR;
    }
};

void CleanReturnedCopy(SmCurve* rawCopy, SmCrvOnSurf& source)
{
    auto* copy = dynamic_cast<SmCrvOnSurf*>(rawCopy);
    if (!copy)
    {
        if (rawCopy != &source)
        {
            delete rawCopy;
        }
        return;
    }
    if (copy == &source)
    {
        return;
    }
    const bool sharedUV = copy->GetUVCurve() == source.GetUVCurve();
    const bool sharedSurface = copy->GetBaseSurface() == source.GetBaseSurface();
    if (sharedUV && !sharedSurface)
    {
        // There is no surface-only ownership flag. Detach both definitions
        // and explicitly release the independent test candidate surface.
        auto* independentSurface = const_cast<SmSurface*>(copy->GetBaseSurface());
        copy->SetOwnerFlag(0);
        if (independentSurface)
        {
            independentSurface->SetOwner(nullptr);
        }
        delete independentSurface;
    }
    else if (sharedSurface)
    {
        copy->SetOwnerFlag(sharedUV ? 0 : 1);
    }
    if (sharedUV)
    {
        const_cast<SmCurve*>(source.GetUVCurve())->SetOwner(&source);
    }
    if (sharedSurface)
    {
        const_cast<SmSurface*>(source.GetBaseSurface())->SetOwner(&source);
    }
    delete copy;
}

bool ChildError(bool failUV, CopyBehavior behavior)
{
    SmContext sourceContext, copyContext;
    Counts uvCounts, surfaceCounts;
    auto* uv = new (sourceContext) CountedLine(sourceContext, uvCounts, failUV ? behavior : CopyBehavior::Success);
    auto* surface = new (sourceContext) FailingPlane(sourceContext, surfaceCounts, failUV ? CopyBehavior::Success : behavior);
    std::unique_ptr<SmCrvOnSurf> source(new (sourceContext) SmCrvOnSurf(*uv, *surface, nullptr, 3));
    SmCurve* rawCopy = nullptr;
    const auto status = source->Copy(copyContext, rawCopy);
    const bool statusPropagated = behavior == CopyBehavior::Alias ? status != SM_SUCCESS : status == SM_ERR_NOT_CONVERGING;
    const bool outputUnpublished = rawCopy == nullptr;
    const bool sourceOwnersUnchanged = uv->GetOwner() == source.get() && surface->GetOwner() == source.get();
    const bool pendingCandidatesReleased = uvCounts.copies == uvCounts.copiesDeleted && surfaceCounts.copies == surfaceCounts.copiesDeleted;
    const bool noCopyAfterUVFailure = !failUV || surfaceCounts.calls == 0;
    CleanReturnedCopy(rawCopy, *source);
    SmPoint3d point;
    const bool sourceStillEvaluates = source->EvaluatePoint(0., point) == SM_SUCCESS;
    source.reset();
    bool passed = true;
    passed &= Check("child_copy_status_propagated_or_protocol_error_rejected", statusPropagated);
    passed &= Check("child_copy_output_unpublished", outputUnpublished);
    passed &= Check("child_copy_source_owners_unchanged", sourceOwnersUnchanged);
    passed &= Check("child_copy_candidates_released_before_return", pendingCandidatesReleased);
    passed &= Check("child_copy_does_not_continue_after_uv_error", noCopyAfterUVFailure);
    passed &= Check("child_copy_source_remains_usable", sourceStillEvaluates);
    passed &= Check("child_copy_each_original_released_once", uvCounts.originalsDeleted == 1 && surfaceCounts.originalsDeleted == 1);
    passed &= Check(
        "child_copy_each_candidate_released_once",
        uvCounts.copies == uvCounts.copiesDeleted && surfaceCounts.copies == surfaceCounts.copiesDeleted
    );
    return passed;
}

void ReclaimOrphanCandidates(Counts& uvCounts, Counts& surfaceCounts)
{
    // The old public copy constructor does not unwind its raw child members.
    // Record that failure first, then reclaim only candidates created by these
    // fixtures. Clearing their owner avoids retaining a destroyed partial COS.
    if (uvCounts.liveCurveCandidate)
    {
        auto* candidate = uvCounts.liveCurveCandidate;
        candidate->SetOwner(nullptr);
        delete candidate;
    }
    if (surfaceCounts.liveSurfaceCandidate)
    {
        auto* candidate = surfaceCounts.liveSurfaceCandidate;
        candidate->SetOwner(nullptr);
        delete candidate;
    }
}

bool ChildThrow(bool failUV)
{
    SmContext sourceContext, copyContext;
    Counts uvCounts, surfaceCounts;
    auto* uv = new (sourceContext) CountedLine(sourceContext, uvCounts, failUV ? CopyBehavior::CandidateThrow : CopyBehavior::Success);
    auto* surface = new (sourceContext) FailingPlane(sourceContext, surfaceCounts, failUV ? CopyBehavior::Success : CopyBehavior::CandidateThrow);
    std::unique_ptr<SmCrvOnSurf> source(new (sourceContext) SmCrvOnSurf(*uv, *surface, nullptr, 3));
    SmCurve* rawCopy = nullptr;
    bool exactException = false;
    try
    {
        source->Copy(copyContext, rawCopy);
    }
    catch (const std::bad_alloc& error)
    {
        exactException = typeid(error) == typeid(std::bad_alloc);
    }
    catch (...)
    {
    }
    const bool outputUnpublished = rawCopy == nullptr;
    const bool ownersUnchanged = uv->GetOwner() == source.get() && surface->GetOwner() == source.get();
    const bool candidatesReleased = uvCounts.copies == uvCounts.copiesDeleted && surfaceCounts.copies == surfaceCounts.copiesDeleted;
    const bool candidatesCreated = uvCounts.copies == 1 && surfaceCounts.copies == (failUV ? 0u : 1u);
    const bool injectionReached = (failUV ? uvCounts.calls : surfaceCounts.calls) == 1;
    const bool noFurtherCopy = !failUV || surfaceCounts.calls == 0;
    CleanReturnedCopy(rawCopy, *source);
    ReclaimOrphanCandidates(uvCounts, surfaceCounts);
    SmPoint3d point;
    const bool sourceUsable = source->EvaluatePoint(0., point) == SM_SUCCESS;
    source.reset();
    bool passed = true;
    passed &= Check("child_throw_injected_copy_called_once", injectionReached);
    passed &= Check("child_throw_preserves_exact_bad_alloc_type", exactException);
    passed &= Check("child_throw_output_unpublished", outputUnpublished);
    passed &= Check("child_throw_source_owners_unchanged", ownersUnchanged);
    passed &= Check("child_throw_candidates_created_as_injected", candidatesCreated);
    passed &= Check("child_throw_candidates_released_before_catch", candidatesReleased);
    passed &= Check("child_throw_does_not_continue_after_uv_throw", noFurtherCopy);
    passed &= Check("child_throw_source_remains_usable", sourceUsable);
    passed &= Check("child_throw_each_original_released_once", uvCounts.originalsDeleted == 1 && surfaceCounts.originalsDeleted == 1);
    passed &= Check(
        "child_throw_each_candidate_released_once",
        uvCounts.copies == uvCounts.copiesDeleted && surfaceCounts.copies == surfaceCounts.copiesDeleted
    );
    return passed;
}

bool WrapperBaseFailure(CopyBehavior behavior)
{
    SmContext sourceContext, copyContext;
    Counts counts;
    auto* base = new (sourceContext) FailingPlane(sourceContext, counts, behavior);
    std::unique_ptr<SmSTEPSurface> source(new (sourceContext) SmSTEPSurface(*base, TRUE));
    base->SetOwner(source.get());
    const auto* originalOwner = base->GetOwner();
    SmSurface* rawCopy = nullptr;
    SmStatus status = SM_SUCCESS;
    bool exactException = false;
    bool otherException = false;
    try
    {
        status = VirtualSurfaceCopy(*source, copyContext, rawCopy);
    }
    catch (const std::bad_alloc& error)
    {
        exactException = typeid(error) == typeid(std::bad_alloc);
        otherException = !exactException;
    }
    catch (...)
    {
        otherException = true;
    }
    const bool throwing = behavior == CopyBehavior::CandidateThrow;
    const bool returnedError = behavior == CopyBehavior::Error || behavior == CopyBehavior::CandidateError;
    const bool statusOrExceptionPreserved = throwing ? exactException && !otherException :
                                                       !exactException && !otherException &&
                                                           (returnedError ? status == SM_ERR_NOT_CONVERGING : status != SM_SUCCESS);
    const bool injectionReached = counts.calls == 1;
    const bool outputUnpublished = rawCopy == nullptr;
    const bool sourceAlive = counts.originalsDeleted == 0;
    const bool sourceOwnerUnchanged = sourceAlive && base->GetOwner() == originalOwner;
    const bool candidatesReleased = counts.copies == counts.copiesDeleted;
    const unsigned expectedCandidates = behavior == CopyBehavior::CandidateError || throwing || behavior == CopyBehavior::WrongContext ||
                                                behavior == CopyBehavior::WrongType ?
                                            1u :
                                            0u;
    const bool candidatesCreated = counts.copies == expectedCandidates;
    // A protocol failure must not publish a wrapper. Only a tracked independent
    // leaf candidate can be reclaimed here without guessing nested ownership.
    if (rawCopy && rawCopy == counts.liveSurfaceCandidate)
    {
        delete rawCopy;
    }
    Counts unusedUV;
    ReclaimOrphanCandidates(unusedUV, counts);
    SmPoint3d point;
    const bool sourceUsable = sourceAlive && source->EvaluatePoint(SmPoint2d(0.5, 0.5), point) == SM_SUCCESS;
    if (sourceAlive)
    {
        base->SetOwner(const_cast<SmObject*>(originalOwner));
        source.reset();
    }
    else
    {
        // If a broken alias guard deleted the original, releasing this wrapper
        // prevents a second deletion while the failed lifetime check is reported.
        source.release();
    }
    bool passed = true;
    passed &= Check("wrapper_base_copy_called_once", injectionReached);
    passed &= Check("wrapper_base_original_error_or_exception_preserved", statusOrExceptionPreserved);
    passed &= Check("wrapper_base_output_unpublished", outputUnpublished);
    passed &= Check("wrapper_base_source_owner_unchanged", sourceOwnerUnchanged);
    passed &= Check("wrapper_base_candidates_created_as_injected", candidatesCreated);
    passed &= Check("wrapper_base_candidates_released_before_return_or_catch", candidatesReleased);
    passed &= Check("wrapper_base_source_remains_usable", sourceUsable);
    passed &= Check("wrapper_base_each_original_released_once", counts.originalsDeleted == 1);
    passed &= Check("wrapper_base_each_candidate_released_once", counts.copies == counts.copiesDeleted);
    return passed;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3)
    {
        return 2;
    }
    const std::string mode(argv[1]);
    if (mode == "step_surface")
    {
        return StepSurfaceCopy() ? 0 : 1;
    }
    if (mode == "step_surface_borrowed")
    {
        return StepSurfaceCopy(false) ? 0 : 1;
    }
    if (mode == "wrapper_base_error")
    {
        return WrapperBaseFailure(CopyBehavior::Error) ? 0 : 1;
    }
    if (mode == "wrapper_base_candidate_error")
    {
        return WrapperBaseFailure(CopyBehavior::CandidateError) ? 0 : 1;
    }
    if (mode == "wrapper_base_null")
    {
        return WrapperBaseFailure(CopyBehavior::Null) ? 0 : 1;
    }
    if (mode == "wrapper_base_alias")
    {
        return WrapperBaseFailure(CopyBehavior::Alias) ? 0 : 1;
    }
    if (mode == "wrapper_base_wrong_context")
    {
        return WrapperBaseFailure(CopyBehavior::WrongContext) ? 0 : 1;
    }
    if (mode == "wrapper_base_wrong_type")
    {
        return WrapperBaseFailure(CopyBehavior::WrongType) ? 0 : 1;
    }
    if (mode == "wrapper_base_throw")
    {
        return WrapperBaseFailure(CopyBehavior::CandidateThrow) ? 0 : 1;
    }
    if (mode == "surface_error")
    {
        return ChildError(false, CopyBehavior::Error) ? 0 : 1;
    }
    if (mode == "uv_error")
    {
        return ChildError(true, CopyBehavior::Error) ? 0 : 1;
    }
    if (mode == "surface_candidate_error")
    {
        return ChildError(false, CopyBehavior::CandidateError) ? 0 : 1;
    }
    if (mode == "uv_candidate_error")
    {
        return ChildError(true, CopyBehavior::CandidateError) ? 0 : 1;
    }
    if (mode == "surface_alias")
    {
        return ChildError(false, CopyBehavior::Alias) ? 0 : 1;
    }
    if (mode == "uv_alias")
    {
        return ChildError(true, CopyBehavior::Alias) ? 0 : 1;
    }
    if (mode == "surface_throw")
    {
        return ChildThrow(false) ? 0 : 1;
    }
    if (mode == "uv_throw")
    {
        return ChildThrow(true) ? 0 : 1;
    }
    if (mode == "ownership_0")
    {
        return curve_copy_controls::Ownership(0) ? 0 : 1;
    }
    if (mode == "ownership_1")
    {
        return curve_copy_controls::Ownership(1) ? 0 : 1;
    }
    if (mode == "ownership_2")
    {
        return curve_copy_controls::Ownership(2) ? 0 : 1;
    }
    if (mode == "ownership_3")
    {
        return curve_copy_controls::Ownership(3) ? 0 : 1;
    }
    if (mode == "definition_cold")
    {
        return curve_copy_controls::Definition(false) ? 0 : 1;
    }
    if (mode == "definition_warm")
    {
        return curve_copy_controls::Definition(true) ? 0 : 1;
    }
    if (argc == 3 && mode == "wrapper_archive_binary")
    {
        return curve_copy_controls::WrapperArchive(argv[2], false) ? 0 : 1;
    }
    if (argc == 3 && mode == "wrapper_archive_ascii")
    {
        return curve_copy_controls::WrapperArchive(argv[2], true) ? 0 : 1;
    }
    return 2;
}
