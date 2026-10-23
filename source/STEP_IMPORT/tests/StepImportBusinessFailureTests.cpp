// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportCylindricalBandRepair.h"
#include "StepImportExecution.h"
#include "StepImportGeometryConstruction.h"
#include "StepImportNativeBuild.h"
#include "StepImportOrientationRepair.h"
#include "StepImportPlan.h"
#include "StepImportTrimMeasurement.h"
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
void FrameBusinessErrors(const std::filesystem::path& fixtures)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    const auto read = [&](const char* fixture)
    {
        std::ifstream stream(fixtures / fixture, std::ios::binary);
        Require(bool(stream), "synthetic native frame fixture opens");
        return std::string{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    };
    struct FrameCase
    {
        const char *fixture, *before, *after;
        std::size_t calls;
        bool validated;
    };
    const FrameCase cases[]{
        { "conic_bounds_bulged_box.step", "#200=DIRECTION('',(1.,0.,0.));", "#200=DIRECTION('',(1.7E308,1.7E308,0.));", 14, false },
        { "conic_bounds_cylinder.step", "#105=DIRECTION('',(1.,0.,0.));", "#105=DIRECTION('',(1.7E308,1.7E308,0.));", 2, false },
        { "conic_bounds_cylinder.step", "#104=DIRECTION('',(0.,0.,1.));", "#104=DIRECTION('',(0.,1.7E308,1.7E308));", 2, false },
        { "conic_bounds_cylinder.step", "#105=DIRECTION('',(1.,0.,0.));", "#105=DIRECTION('',(1.E-320,0.,1.));", 2, false },
        { "conic_bounds_bulged_box.step", "#501=DIRECTION('',(0.,0.,-1.));", "#501=DIRECTION('',(0.,1.7E308,1.7E308));", 69, false },
        { "linked_wireframe.step", "#917=DIRECTION('',(1.,0.,0.));", "#917=DIRECTION('',(1.7E308,1.7E308,0.));", 236, true },
        { "linked_wireframe.step", "#907=DIRECTION('',(1.,0.,0.));", "#907=DIRECTION('',(1.7E308,1.7E308,0.));", 240, true },
        { "supporting_geometry.step", "#917=DIRECTION('',(1.,0.,0.));", "#917=DIRECTION('',(1.7E308,1.7E308,0.));", 239, true },
        { "supporting_geometry.step", "#907=DIRECTION('',(1.,0.,0.));", "#907=DIRECTION('',(1.7E308,1.7E308,0.));", 236, true },
        { "supporting_geometry.step", "#973=DIRECTION('',(1.,0.,0.));", "#973=DIRECTION('',(1.7E308,1.7E308,0.));", 237, true },
    };
    for (const auto& item : cases)
    {
        auto text = read(item.fixture);
        const auto at = text.find(item.before);
        Require(at != std::string::npos, "synthetic native frame direction exists");
        // Source ratios remain finite and mathematically nonzero. Either their
        // binary64 norm overflows or a near-parallel projection cannot establish
        // a finite basis. These are native-evidence refusals, not source repairs.
        text.replace(at, std::char_traits<char>::length(item.before), item.after);
        const auto raw = step_source::InspectBytes(text, options.sourceLimits);
        const auto qualified = step_semantics::Check(raw, options.semanticLimits, options.sourcePolicy, step_semantics::GeometryScope::GeneralBrep);
        Require(bool(qualified.snapshot), "finite source ratios qualify before native frame conversion");
        Report report;
        Execution execution(options, report);
        const auto result = BuildGeneral(qualified.snapshot, options, report, execution);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == "GeneralDegenerateFrame" &&
                result.error().message == "Native derived evidence is unestablished" && !result.error().record && result.error().stage == "D5",
            "unestablished native direction returns its original numerical refusal"
        );
        Require(
            report.nativeCalls == item.calls && report.candidateDiscarded &&
                report.pointerReports == (item.validated ? std::optional<std::size_t>(0) : std::nullopt) &&
                report.deepReports == (item.validated ? std::optional<std::size_t>(0) : std::nullopt),
            "direction refusal keeps charged work and validation evidence while releasing its unpublished candidate"
        );
        if (std::string_view(item.fixture) == "conic_bounds_bulged_box.step" && item.calls == 14)
        {
            FaceOrientationRepairRequest request;
            request.sourceSha256 = raw.sha256;
            request.maximumFaceEdits = 1;
            request.maximumProofControls = 10000;
            request.maximumProofProducts = 1000000;
            request.edits.push_back({ "7", "21", "517", true, false });
            auto repairOptions = options;
            repairOptions.faceOrientationRepair = request;
            Report repairReport;
            Execution repairExecution(repairOptions, repairReport);
            const auto strict = BuildGeneral(qualified.snapshot, repairOptions, repairReport, repairExecution);
            Require(
                !strict && strict.error().status == Status::Indeterminate && strict.error().key == "GeneralDegenerateFrame" &&
                    repairReport.candidateDiscarded && repairReport.nativeCalls == 14 && repairReport.originalAttempt &&
                    repairReport.originalAttempt->nativeCalls == 14 && !repairReport.originalAttemptStatus &&
                    repairReport.originalAttempt->diagnostics.empty() && !repairReport.originalAttempt->candidateDiscarded &&
                    repairReport.repairEditsAttempted == 0 && repairReport.repairStage == "StrictCandidate",
                "frame refusal preserves the unfinished strict observation and never starts an orientation repair"
            );
        }
        const auto publicResult = ImportBytes(text, options);
        Require(
            publicResult.status == Status::Indeterminate && !publicResult.model && publicResult.report.candidateDiscarded &&
                publicResult.report.nativeCalls == item.calls && publicResult.report.source && publicResult.report.source->Bytes() == text &&
                publicResult.report.diagnostics.size() == 1 && publicResult.report.diagnostics[0].source.key == "GeneralDegenerateFrame" &&
                publicResult.report.diagnostics[0].source.recordId.empty(),
            "public import retains the original source and numerical refusal without publishing a partial model"
        );
    }
    auto scaled = read("conic_bounds_bulged_box.step");
    const std::string line = "#200=DIRECTION('',(1.,0.,0.));";
    scaled.replace(scaled.find(line), line.size(), "#200=DIRECTION('',(4.,0.,0.));");
    const auto normalized = ImportBytes(scaled, options);
    Require(
        normalized.status == Status::Accepted && normalized.model && normalized.model->Source()->Source().Bytes() == scaled,
        "finite source direction magnitude is normalized without changing length or parameter semantics"
    );
    auto defaults = read("conic_bounds_cylinder.step");
    const std::string placement = "#106=AXIS2_PLACEMENT_3D('',#100,#104,#105);";
    defaults.replace(defaults.find(placement), placement.size(), "#106=AXIS2_PLACEMENT_3D('',#100,$,$);");
    const auto defaulted = ImportBytes(defaults, options);
    Require(defaulted.status == Status::Accepted && defaulted.model, "unset axes retain the established STEP frame defaults");
}
void ScalarBusinessErrors(const std::filesystem::path& fixtures)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    struct ConversionCase
    {
        const char* fixture;
        std::vector<std::pair<std::string, std::string>> edits;
        const char* key;
        std::size_t calls;
    };
    const ConversionCase cases[]{
        { "conic_bounds_bulged_box.step",
          { { "#100=CARTESIAN_POINT('',(2.,-3.,5.));", "#100=CARTESIAN_POINT('',(1.E309,-3.,5.));" } },
          "GeneralNumericRangeUnestablished",
          0 },
        { "conic_bounds_bulged_box.step",
          { { "#100=CARTESIAN_POINT('',(2.,-3.,5.));", "#100=CARTESIAN_POINT('',(1.E-400,-3.,5.));" } },
          "GeneralNumericRangeUnestablished",
          0 },
        { "conic_bounds_bulged_box.step",
          { { "#100=CARTESIAN_POINT('',(2.,-3.,5.));", "#100=CARTESIAN_POINT('',(1.E308,-3.,5.));" },
            { "SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT($,.METRE.)" } },
          "GeneralLengthRangeUnestablished",
          0 },
        { "conic_bounds_bulged_box.step",
          { { "#100=CARTESIAN_POINT('',(2.,-3.,5.));", "#100=CARTESIAN_POINT('',(2.,-3.));" } },
          "GeneralRequires3DGeometry",
          0 },
        { "conic_bounds_bulged_box.step",
          { { "#200=DIRECTION('',(1.,0.,0.));", "#200=DIRECTION('',(1.E309,0.,0.));" } },
          "GeneralNumericRangeUnestablished",
          14 },
        { "conic_bounds_bulged_box.step", { { "#200=DIRECTION('',(1.,0.,0.));", "#200=DIRECTION('',(1.,0.));" } }, "GeneralRequires3DGeometry", 14 },
        { "conic_bounds_bulged_box.step",
          { { "#201=VECTOR('',#200,10.);", "#201=VECTOR('',#200,1.E309);" } },
          "GeneralNumericRangeUnestablished",
          14 },
        { "conic_bounds_cylinder.step", { { "#110=CIRCLE('',#106,3.);", "#110=CIRCLE('',#106,1.E309);" } }, "GeneralNumericRangeUnestablished", 2 },
        { "conic_bounds_bulged_box.step",
          { { "(3,3),(3,3),(0.,1.),(0.,1.),.UNSPECIFIED.", "(3,3),(3,3),(0.,1.E309),(0.,1.),.UNSPECIFIED." } },
          "GeneralNumericRangeUnestablished",
          91 },
        { "linked_wireframe.step",
          { { "#911=CARTESIAN_POINT('',(7.,8.,9.));", "#911=CARTESIAN_POINT('',(1.E309,8.,9.));" } },
          "GeneralNumericRangeUnestablished",
          236 },
        { "linked_wireframe.step",
          { { "#911=CARTESIAN_POINT('',(7.,8.,9.));", "#911=CARTESIAN_POINT('',(7.,8.));" } },
          "GeneralRequires3DGeometry",
          236 },
        { "linked_wireframe.step", { { "PARAMETER_VALUE(1.5707963267948966)", "PARAMETER_VALUE(1.E309)" } }, "GeneralNumericRangeUnestablished", 240 },
        { "supporting_geometry.step",
          { { "#911=CARTESIAN_POINT('',(7.,8.,9.));", "#911=CARTESIAN_POINT('',(1.E309,8.,9.));" } },
          "GeneralNumericRangeUnestablished",
          239 },
        { "supporting_geometry.step",
          { { "#971=CARTESIAN_POINT('',(30.,40.,50.));", "#971=CARTESIAN_POINT('',(1.E309,40.,50.));" } },
          "GeneralNumericRangeUnestablished",
          237 },
        { "conic_bounds_bulged_box.step",
          { { "#200=DIRECTION('',(1.,0.,0.));\n", "" },
            { "#201=VECTOR('',#200,10.);\n", "" },
            { "LINE('',#100,#201)",
              "(BOUNDED_CURVE() B_SPLINE_CURVE(1,(#100,#101),.POLYLINE_FORM.,.F.,.F.) B_SPLINE_CURVE_WITH_KNOTS((2,2),(0.,1.E309),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((2.,5.)) REPRESENTATION_ITEM('weighted line'))" } },
          "GeneralNumericRangeUnestablished",
          14 },
        { "conic_bounds_bulged_box.step",
          { { "#200=DIRECTION('',(1.,0.,0.));\n", "" },
            { "#201=VECTOR('',#200,10.);\n", "" },
            { "LINE('',#100,#201)",
              "(BOUNDED_CURVE() B_SPLINE_CURVE(1,(#100,#101),.POLYLINE_FORM.,.F.,.F.) B_SPLINE_CURVE_WITH_KNOTS((2,2),(0.,1.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE((1.E309,5.)) REPRESENTATION_ITEM('weighted line'))" } },
          "GeneralNumericRangeUnestablished",
          14 },
    };
    for (const auto& item : cases)
    {
        std::ifstream stream(fixtures / item.fixture, std::ios::binary);
        Require(bool(stream), "synthetic scalar fixture opens");
        std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        for (const auto& [before, after] : item.edits)
        {
            const auto at = text.find(before);
            Require(at != std::string::npos, "synthetic scalar conversion target exists");
            text.replace(at, before.size(), after);
        }
        // Exact source values and dimension admission are checked independently
        // of the native binary64/3D construction range. Do not clamp, fit, change
        // units or manufacture a third coordinate when that range is unestablished.
        const auto raw = step_source::InspectBytes(text, options.sourceLimits);
        const auto qualified = step_semantics::Check(raw, options.semanticLimits, options.sourcePolicy, step_semantics::GeometryScope::GeneralBrep);
        Require(bool(qualified.snapshot), "synthetic values reach native conversion after source checks");
        Report report;
        Execution execution(options, report);
        const auto result = BuildGeneral(qualified.snapshot, options, report, execution);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == item.key &&
                result.error().message == "Native derived evidence is unestablished" && !result.error().record && result.error().stage == "D5",
            "native scalar conversion returns the original numerical refusal without throwing"
        );
        const auto observed = item.calls >= 236 ? std::optional<std::size_t>(0) : std::nullopt;
        Require(
            report.nativeCalls == item.calls && report.candidateDiscarded == (item.calls > 0) && report.pointerReports == observed &&
                report.deepReports == observed,
            "scalar refusal preserves billing, existing validation observations and candidate cleanup"
        );
        auto publicResult = ImportBytes(text, options);
        Require(
            publicResult.status == Status::Indeterminate && !publicResult.model && publicResult.report.nativeCalls == item.calls &&
                publicResult.report.source && publicResult.report.source->Bytes() == text && publicResult.report.diagnostics.size() == 1 &&
                publicResult.report.diagnostics[0].source.key == item.key && publicResult.report.diagnostics[0].source.recordId.empty(),
            "public scalar refusal preserves source bytes and atomic publication"
        );
        if (std::string_view(item.fixture) == "conic_bounds_bulged_box.step" && item.calls == 0)
        {
            FaceOrientationRepairRequest request;
            request.sourceSha256 = raw.sha256;
            request.maximumFaceEdits = 1;
            request.maximumProofControls = 10000;
            request.maximumProofProducts = 1000000;
            request.edits.push_back({ "7", "21", "517", true, false });
            auto strictOptions = options;
            strictOptions.faceOrientationRepair = request;
            Report strictReport;
            Execution strictExecution(strictOptions, strictReport);
            const auto strict = BuildGeneral(qualified.snapshot, strictOptions, strictReport, strictExecution);
            Require(
                !strict && strict.error().key == item.key && strictReport.originalAttempt && !strictReport.originalAttemptStatus &&
                    strictReport.originalAttempt->diagnostics.empty() && !strictReport.originalAttempt->candidateDiscarded &&
                    strictReport.repairStage == "StrictCandidate" && strictReport.repairEditsAttempted == 0,
                "scalar conversion preserves unfinished strict evidence without attempting a repair"
            );
        }
    }
    std::ifstream stream(fixtures / "conic_bounds_bulged_box.step", std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    const std::string point = "#100=CARTESIAN_POINT('',(2.,-3.,5.));";
    text.replace(text.find(point), point.size(), "#100=CARTESIAN_POINT('',(+2.,-3.E0,5.0));");
    const auto ordinary = ImportBytes(text, options);
    Require(
        ordinary.status == Status::Accepted && ordinary.model && ordinary.report.source->Bytes() == text,
        "ordinary signed/exponent decimals retain geometry, parameters and source lexemes"
    );
}

void NativeStatusBusinessErrors(Plan plan)
{
    Require(bool(construction::Good(SM_SUCCESS)), "native success remains successful in the general construction boundary");
    const auto general = construction::Good(SM_ERR_INVALID_INPUT);
    Require(
        !general && general.error().status == Status::BuildFailure && general.error().key == "NativeReturnOrConstructionFailure" &&
            general.error().message == "GeneralNativeReturn:1007" && !general.error().record && general.error().stage == "D5",
        "general native status returns its distinct original message without source or phase enrichment"
    );
    // Contradict a disposable plan, never the qualified immutable source.
    // A zero direction is refused by the real line constructor before any
    // allocation/adoption. The builder must return its status and discard the
    // already-held incomplete BRep, preserving the pre-migration work count.
    plan.edges.front().maximumMm = plan.edges.front().minimumMm;
    const auto options = Options::PlanarFixture();
    Report report;
    Execution execution(options, report);
    const auto result = Build(plan, options, report, execution);
    Require(
        !result && result.error().status == Status::BuildFailure && result.error().key == "NativeReturnOrConstructionFailure" &&
            result.error().message == "NativeReturnFailure:1007" && !result.error().record && result.error().stage == "D5",
        "real native status failure returns its former importer diagnostic without throwing"
    );
    // Validators have not run; absent observations cannot claim zero reports.
    Require(
        report.nativeCalls == 17 && report.candidateDiscarded && !report.pointerReports && !report.deepReports && report.edges.empty() &&
            report.loops.empty(),
        "native construction refusal retains work already charged and discards the unpublished candidate before postchecks"
    );
}
void StrictOperationErrors(const std::string& text)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    const auto raw = step_source::InspectBytes(text, options.sourceLimits);
    const auto qualified = step_semantics::Check(raw, options.semanticLimits, options.sourcePolicy, step_semantics::GeometryScope::GeneralBrep);
    Require(bool(qualified.snapshot), "synthetic B-spline repair support qualifies before the strict-attempt protocol test");
    FaceOrientationRepairRequest request;
    request.sourceSha256 = raw.sha256;
    request.maximumFaceEdits = 1;
    request.maximumProofControls = 10000;
    request.maximumProofProducts = 1000000;
    request.edits.push_back({ "7", "21", "517", true, false });
    options.faceOrientationRepair = request;
    for (std::string_view mode : { "native", "trim", "cancel" })
    {
        options.execution.control = std::make_shared<ImportControl>();
        Report report;
        Execution execution(options, report);
        unsigned attempts = 0;
        const auto result = BuildWithOrientationRepair(
            qualified.snapshot,
            {},
            options,
            report,
            execution,
            [&](Report& checks, bool capture, const FaceSenseOverrides& overrides) -> ConstructionResult<NativeCandidate>
            {
                ++attempts;
                Require(capture && overrides.empty(), "operation refusal never enters the repair rebuild or changes a face sense");
                if (auto operation = execution.ChargeNative(checks, 0); !operation)
                {
                    return tl::unexpected(std::move(operation.error()));
                }
                if (mode == "cancel")
                {
                    Require(options.execution.control->RequestCancel() == CancelResult::Requested, "strict-attempt cancellation requested once");
                    auto cancelled = execution.Check(Phase::BeforeNativeCall);
                    Require(!cancelled, "strict-attempt checkpoint observes real cooperative cancellation");
                    return tl::unexpected(std::move(cancelled.error()));
                }
                SmContext context;
                if (mode == "native")
                {
                    SmLine* rawLine = nullptr;
                    const auto status = SmLine::CreateLineSegment(context, 3, SmPoint3d(0, 0, 0), SmPoint3d(0, 0, 0), rawLine);
                    std::unique_ptr<SmLine> owner(rawLine);
                    auto checked = construction::Good(status);
                    Require(!checked, "real native zero-direction construction refuses inside the strict attempt");
                    return tl::unexpected(std::move(checked.error()));
                }
                SmLine edge(SmPoint3d(0, 0, 0), SmPoint3d(1, 0, 0), 3, &context);
                SmLine uv(SmPoint3d(0, 0, 0), SmPoint3d(1, 0, 0), 2, &context);
                SmPlane plane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(-1, -1, 2, 2), &context);
                const auto measured = MeasureFinalTrim(edge, uv, plane, .25, .75);
                Require(!measured, "real paired-parameter domain refusal occurs inside the strict attempt");
                return tl::unexpected(NativeConstructionFailure(TrimMeasurementErrorName(measured.error())));
            }
        );
        Require(
            !result && attempts == 1 && report.originalAttempt && report.repairStage == "StrictCandidate" && report.nativeCalls == 1 &&
                report.originalAttempt->nativeCalls == 1 && report.repairEditsAttempted == 0,
            "strict-operation refusal retains partial work and never claims a repaired candidate"
        );
        if (mode == "cancel")
        {
            Require(
                result.error().status == Status::Cancelled && report.originalAttemptStatus == Status::Cancelled &&
                    report.originalAttempt->diagnostics.size() == 1 && report.originalAttempt->candidateDiscarded,
                "ordinary execution refusal retains its original completed strict-failure observation"
            );
        }
        else
        {
            Require(
                result.error().status == Status::BuildFailure && result.error().key == "NativeReturnOrConstructionFailure" &&
                    !report.originalAttemptStatus && report.originalAttempt->diagnostics.empty() && !report.originalAttempt->candidateDiscarded,
                "native operation refusal preserves the unfinished strict-attempt report instead of inventing completed failure evidence"
            );
        }
    }
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
        Require(argc == 2 || argc == 3, "public planar fixture and optional contract mode required");
        std::ifstream input(argv[1], std::ios::binary);
        const std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        if (argc == 3 && std::string_view(argv[2]) == "--scalar")
        {
            ScalarBusinessErrors(std::filesystem::path(argv[1]).parent_path());
            return 0;
        }
        if (argc == 3 && std::string_view(argv[2]) == "--frame")
        {
            FrameBusinessErrors(std::filesystem::path(argv[1]).parent_path());
            return 0;
        }
        if (argc == 3 && std::string_view(argv[2]) == "--strict-native")
        {
            StrictOperationErrors(text);
            return 0;
        }
        const auto options = Options::PlanarFixture();
        const auto raw = step_source::InspectBytes(text, options.sourceLimits);
        const auto semantics = step_semantics::Check(raw, options.semanticLimits);
        const auto checked = step_planar::Check(semantics, { options.quality.id, options.quality.consistencyMm }, options.planarLimits);
        Require(checked.status == step_planar::Status::Checked, "public fixture qualifies before native construction");
        const auto planned = MakePlan(checked);
        Require(bool(planned), "qualified public fixture has a native plan");
        const auto& plan = *planned;
        if (argc == 3 && std::string_view(argv[2]) == "--native")
        {
            NativeStatusBusinessErrors(plan);
        }
        else if (argc == 3 && std::string_view(argv[2]) == "--planning")
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
        else if (argc == 2 || std::string_view(argv[2]) == "--late")
        {
            PlanarBusinessErrors(plan, argc == 3);
        }
        else
        {
            Require(false, "unknown business-failure contract mode");
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
