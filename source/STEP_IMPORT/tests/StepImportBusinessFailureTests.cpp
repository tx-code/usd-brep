// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylindricalBandRepair.h"
#include "StepImportExecution.h"
#include "StepImportNativeBuild.h"
#include "StepImportOrientationRepair.h"
#include "StepImportPlan.h"
#include "StepImportUnits.h"

#include <SmContext.h>
#include <SmLine.h>
#include <SmPlane.h>
#include <cfenv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace step_import;
using namespace step_import::detail;
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void PlanarBusinessErrors(const Plan& plan, bool late)
{
    auto options = Options::PlanarFixture();
    if (late)
    {
        options.quality.definitionMm = Fraction(1, 1000000000000000LL);
    }
    Report report;
    Execution execution(options, report);
    if (late)
    {
        const auto result = Build(plan, options, report, execution);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == "NativeDefinitionErrorBudget" &&
                result.error().record == plan.faces.front().face && result.error().stage == "D5",
            "conservative proof refusal returns its source-bound business failure"
        );
        Require(
            report.nativeCalls > 0 && report.candidateDiscarded && report.definitionBoundMm.value_or(0) > 0 && report.pointerReports == 0 &&
                report.deepReports == 0,
            "returned proof refusal retains validation and numerical observations and discards its candidate"
        );
    }
    else
    {
        struct RestoreRounding
        {
            int value = std::fegetround();
            ~RestoreRounding()
            {
                std::fesetround(value);
            }
        } restore;
        Require(std::fesetround(FE_UPWARD) == 0, "unsupported native rounding fixture");
        const auto result = Build(plan, options, report, execution);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == "UnsupportedNativeNumericEnvironment" &&
                result.error().record == plan.record && result.error().stage == "D5",
            "native numeric-environment admission returns its business failure"
        );
        Require(
            report.nativeCalls == 0 && report.checkpoints == 0 && !report.candidateDiscarded,
            "early refusal invents neither work nor a held candidate"
        );
    }
}
void PlanarCorrespondenceErrors(Plan plan)
{
    // Deliberately contradict only the selected-use evidence. Native defining
    // geometry and the immutable qualified source remain unchanged.
    auto& use = plan.faces.front().loops.front().selectedUses.front();
    use.forward = !use.forward;
    const auto options = Options::PlanarFixture();
    Report report;
    Execution execution(options, report);
    const auto result = Build(plan, options, report, execution);
    Require(
        !result && result.error().status == Status::PostcheckFailure && result.error().key == "NativeDirectedSourcePathMismatch" &&
            result.error().record == plan.record && result.error().stage == "D5",
        "directed native/source disagreement returns its postcheck business failure"
    );
    Require(
        report.candidateDiscarded && report.nativeCalls > 0 && report.pointerReports == 0 && report.deepReports == 0 && report.loops.empty() &&
            !report.definitionBoundMm,
        "correspondence refusal retains completed validators and prevents later proof or report rows"
    );
}
void GeneralBusinessErrors(const Plan& plan)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    Report report;
    Execution execution(options, report);
    const auto result = BuildGeneral(plan.source, options, report, execution);
    Require(
        !result && result.error().status == Status::PostcheckFailure && result.error().key == "GeneralRequiresMaterialSolid" &&
            result.error().record && plan.source->Source().Records()[*result.error().record].id == "21" && result.error().stage == "GeneralBrep",
        "a source surface cannot become a material solid through a thrown business failure"
    );
    Require(report.nativeCalls == 0 && !report.candidateDiscarded, "general profile refusal returns before native work");
}
void UnitBusinessErrors(const Plan& plan)
{
    const auto unit = plan.source->UnitContext().planeAngleUnit;
    const auto result = ResolveLengthScale(
        *plan.source,
        unit,
        []() -> Result<void>
        {
            return {};
        }
    );
    Require(
        !result && result.error().status == Status::Unsupported && result.error().key == "GeneralConversionQuantityUnsupported" &&
            result.error().record == unit && result.error().stage == "GeneralBrep",
        "exact physical scaling refuses an angle root as a business result without conflating dimensions"
    );
}
void WitnessOwnership(const Plan& plan)
{
    const auto options = Options::PlanarFixture();
    Report report;
    Execution execution(options, report);
    auto built = Build(plan, options, report, execution);
    Require(built && *built, "owning error-channel control uses a real qualified native candidate");
    const std::weak_ptr<ModelData> owner = *built;
    // This checks the owning channel, not eligibility for geometric repair.
    ConstructionResult<NativeCandidate> attempt = tl::unexpected(
        OrientationFailureCandidate{ Failure{ Status::PostcheckFailure,
                                              "WitnessBoundary",
                                              "retained evidence",
                                              plan.record,
                                              "GeneralBrep",
                                              NativeFailureKind::FaceLoopOrientation },
                                     std::move(*built),
                                     { 0 } }
    );
    Require(!owner.expired(), "strict-attempt error owns its unpublished candidate");
    const auto result = FinishConstructionAttempt(std::move(attempt));
    Require(
        !result && result.error().key == "WitnessBoundary" && result.error().record == plan.record &&
            result.error().nativeKind == NativeFailureKind::FaceLoopOrientation && owner.expired(),
        "ordinary failure conversion releases native witness ownership before report delivery"
    );
}
void DefinitionEvidence()
{
    SmContext context;
    SmLine *first = nullptr, *second = nullptr;
    Require(
        SmLine::CreateLineSegment(context, 3, SmPoint3d(0, 0, 0), SmPoint3d(2, 0, 0), first) == SM_SUCCESS && first,
        "first native curve definition"
    );
    std::unique_ptr<SmLine> before(first);
    Require(
        SmLine::CreateLineSegment(context, 3, SmPoint3d(0, 0, 0), SmPoint3d(3, 0, 0), second) == SM_SUCCESS && second,
        "different native curve definition"
    );
    std::unique_ptr<SmLine> after(second);
    const auto identical = RequireSameNativeCurveDefinition(*before, *before);
    const auto changed = RequireSameNativeCurveDefinition(*before, *after);
    Require(
        identical && !changed && changed.error().status == Status::Indeterminate && changed.error().key == "RepairChangedDefinitionOrTolerance" &&
            changed.error().stage == "Repair",
        "exact coefficient comparison returns mismatch without repairing or weakening equality"
    );
    SmPlane plane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(0, 0, 1, 1), &context);
    SmPlane shifted(SmPoint3d(0, 0, 1), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(0, 0, 1, 1), &context);
    const auto sameSupport = RequireSameNativeSurfaceDefinition(plane, plane);
    const auto changedSupport = RequireSameNativeSurfaceDefinition(plane, shifted);
    Require(
        sameSupport && !changedSupport && changedSupport.error().status == Status::Indeterminate &&
            changedSupport.error().key == "RepairChangedDefinitionOrTolerance",
        "support definition mismatch remains distinct from a proved identical native surface"
    );
}
void PlanningEvidence(step_planar::Outcome checked)
{
    // Duplicate a disposable evidence row, not the immutable source. A single
    // source face cannot establish the two-polygon shared-sheet certificate.
    checked.evidence.faces.push_back(checked.evidence.faces.front());
    const auto planned = MakePlan(checked);
    Require(
        !planned && planned.error().status == Status::Unsupported && planned.error().key == "UnsupportedNativeOpenSheetScope" &&
            planned.error().record && checked.evidence.source->Source().Records()[*planned.error().record].id == "21" &&
            planned.error().stage == "D5",
        "unestablished native sheet proof returns its own business failure without a plan"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2 || argc == 3, "public planar fixture and optional --late required");
        std::ifstream input(argv[1], std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        const auto options = Options::PlanarFixture();
        const auto raw = step_source::InspectBytes(text, options.sourceLimits);
        const auto semantics = step_semantics::Check(raw, options.semanticLimits);
        const auto checked = step_planar::Check(semantics, { options.quality.id, options.quality.consistencyMm }, options.planarLimits);
        Require(checked.status == step_planar::Status::Checked, "public fixture qualifies before native construction");
        const auto planned = MakePlan(checked);
        Require(bool(planned), "qualified public fixture has a native plan");
        const auto& plan = *planned;
        if (argc == 3 && std::string_view(argv[2]) == "--planning")
        {
            PlanningEvidence(checked);
        }
        else if (argc == 3 && std::string_view(argv[2]) == "--definitions")
        {
            DefinitionEvidence();
        }
        else if (argc == 3 && std::string_view(argv[2]) == "--witness")
        {
            WitnessOwnership(plan);
        }
        else if (argc == 3 && std::string_view(argv[2]) == "--general")
        {
            GeneralBusinessErrors(plan);
        }
        else if (argc == 3 && std::string_view(argv[2]) == "--units")
        {
            UnitBusinessErrors(plan);
        }
        else if (argc == 3 && std::string_view(argv[2]) == "--mapping")
        {
            PlanarCorrespondenceErrors(plan);
        }
        else
        {
            PlanarBusinessErrors(plan, argc == 3);
        }
        return 0;
    }
    catch (const Failure& error)
    {
        std::cerr << "Business Failure escaped the result seam: " << error.key << '\n';
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
