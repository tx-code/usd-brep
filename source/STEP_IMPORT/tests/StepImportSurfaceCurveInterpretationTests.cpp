// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportSurfaceCurveInterpretation.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace step_import;
using step_import_test::Require;
namespace
{
Options Config()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    return options;
}
std::shared_ptr<SurfaceCurve3dInterpretationRequest> Request(const std::string& source)
{
    const auto inspected = step_source::InspectBytes(source, step_source::Limits::PlanarFixtures());
    Require(bool(inspected.source), "public immutable input identity");
    auto request = std::make_shared<SurfaceCurve3dInterpretationRequest>();
    request->sourceSha256 = inspected.source->Sha256();
    request->maximumUses = 1;
    request->uses.push_back({ "7", "21", "203", "920", "202", SurfaceCurveDeclaredMaster::PcurveS1, { "921" } });
    return request;
}
void Check(const std::string& source, bool conflict, ReportEncoding encoding = ReportEncoding::ExpandedV1)
{
    auto options = Config();
    options.reportEncoding = encoding;
    auto request = Request(source);
    options.surfaceCurve3dInterpretation = request;
    auto limited = options;
    limited.nativeLimits.nativeCalls = 2;
    const auto resource = ImportBytes(source, limited);
    Require(resource.status == Status::ResourceLimit && !resource.model, "resource errors are never auxiliary-conflict exceptions");
    auto result = ImportBytes(source, options);
    Require(result.status == Status::AcceptedUnderInterpretation && result.model, "only explicitly qualified interpretation publishes");
    auto model = result.model;
    auto receipt = model->SurfaceCurve3dInterpretation();
    Require(
        receipt && receipt->requestComplete && receipt->plans.size() == 1 && receipt->actions.size() == 1 && receipt->actions[0].nativeChecksPassed &&
            receipt->actions[0].nativeEdgeInterval == std::array<double, 2>{ 0, 1 },
        "complete original-bound action and original 3D parameters"
    );
    Require(!receipt->actions[0].originalConflicts.empty() == conflict, "original auxiliary disagreement retained without inventing one");
    Require(
        receipt->consistencyBudgetMm == std::array<std::int64_t, 2>{ 1, 1000 } &&
            receipt->derivedUvBudgetMm == std::array<std::int64_t, 2>{ 1, 1000 } &&
            receipt->maximumWorkToleranceMm == std::array<std::int64_t, 2>{ 1, 1000 },
        "selected budgets are unchanged and owned"
    );
    Require(
        model->Source()->Source().Bytes() == source && !model->Repairs() && !model->CylindricalBandRepairs(),
        "immutable original and None repair"
    );
    step_import_test::Near(*model->MaterialVolumeMm3(), 6000, .001, "independent original 3D box volume");
    step_import_test::Near(model->Info().areaMm2, 2200, .001, "independent original 3D box area");
    Require(
        result.report.json.find("ExplicitCurve3dPriority") != std::string::npos &&
            result.report.json.find("\"source_pcurve_consistency_qualified\":false") != std::string::npos,
        "mandatory report does not qualify original pcurve consistency"
    );
    bool auxiliary = false;
    for (const auto& mapping : result.report.parameterMappings)
    {
        if (mapping.kind.starts_with("SurfaceCurveAuxiliaryPcurve"))
        {
            auxiliary = true;
            bool expected = false, interval = false, direction = false;
            for (const auto& parameter : mapping.parameters)
            {
                Require(parameter.first != "increasing_parameter_follows_native_master", "expected auxiliary direction is not claimed evidence");
                expected = expected || parameter.first == "expected_increasing_parameter_follows_native_master";
                interval = interval || parameter.first == "selected_interval_established";
                direction = direction || parameter.first == "direction_observation_passed";
                if (conflict && (parameter.first == "selected_interval_established" || parameter.first == "direction_observation_passed"))
                {
                    Require(parameter.second == std::vector<double>{ 0 }, "failed auxiliary association establishes no interval or direction");
                }
            }
            Require(expected && interval && direction, "declared expectations and actual auxiliary observations are distinct");
        }
    }
    Require(auxiliary, "complete retained original auxiliary mapping");
    auto exact = options;
    exact.nativeLimits.reportBytes = result.report.json.size();
    const auto bounded = ImportBytes(source, exact);
    Require(
        bounded.status == Status::AcceptedUnderInterpretation && bounded.model && bounded.report.json == result.report.json,
        "deterministic exact report budget"
    );
    --exact.nativeLimits.reportBytes;
    const auto noReport = ImportBytes(source, exact);
    Require(noReport.status == Status::ResourceLimit && !noReport.model, "one byte short withholds whole interpreted model");
    auto copy = model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent modeling copy");
    result = {};
    request->uses.clear();
    options = {};
    Require(
        model->SurfaceCurve3dInterpretation()->plans.size() == 1 && receipt->sourceSha256 == model->Source()->Source().Sha256(),
        "clearing caller options/report cannot erase snapshot evidence"
    );
    copy.model->Edit(
        [](SmBrep& native)
        {
            step_import_test::CheckNativeWorkBudget(native, .001);
            SmTArray<SmEdge*> edges;
            native.GetEdges(edges);
            unsigned found = 0;
            for (ULONG i = 0; i < edges.GetSize(); ++i)
            {
                const auto* curve = edges[i]->GetCurve();
                if (curve->GetType() == SmBSplineCurve_TYPE)
                {
                    Require(
                        curve->GetDegree() == 1 && curve->GetNaturalInterval() == SmExtent1d(0, 1),
                        "retained original 3D curve, not pcurve composition"
                    );
                    ++found;
                }
            }
            Require(found == 1, "complete original 3D boundary inventory");
        }
    );
}
void Refusals(const std::string& source)
{
    auto options = Config();
    auto request = Request(source);
    options.surfaceCurve3dInterpretation = request;
    auto changed = std::make_shared<SurfaceCurve3dInterpretationRequest>(*request);
    changed->sourceSha256[0] = changed->sourceSha256[0] == 'a' ? 'b' : 'a';
    options.surfaceCurve3dInterpretation = changed;
    const auto wrongSource = ImportBytes(source, options);
    Require(wrongSource.status == Status::Unsupported && !wrongSource.model, "wrong original refuses before construction");
    changed = std::make_shared<SurfaceCurve3dInterpretationRequest>(*request);
    changed->uses[0].curve3dId = "999";
    options.surfaceCurve3dInterpretation = changed;
    const auto wrongCurve = ImportBytes(source, options);
    Require(wrongCurve.status == Status::Unsupported && !wrongCurve.model, "wrong curve binding refuses");
    changed = std::make_shared<SurfaceCurve3dInterpretationRequest>(*request);
    changed->uses[0].edgeId = "999";
    options.surfaceCurve3dInterpretation = changed;
    const auto unused = ImportBytes(source, options);
    Require(unused.status == Status::Unsupported && !unused.model, "unused plan never publishes a normal model instead");
    options.surfaceCurve3dInterpretation = request;
    for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
    {
        auto stopped = options;
        stopped.execution.control = std::make_shared<ImportControl>();
        stopped.execution.progress = [&, phase](Progress p)
        {
            if (p.phase == phase)
            {
                stopped.execution.control->RequestCancel();
            }
        };
        const auto result = ImportBytes(source, stopped);
        Require(result.status == Status::Cancelled && !result.model, "cancelled interpreted model never publishes");
    }
    auto caller = std::make_shared<SurfaceCurve3dInterpretationRequest>(*request);
    options.surfaceCurve3dInterpretation = caller;
    options.execution.progress = [caller](Progress p)
    {
        if (p.phase == Phase::BeforeCapture)
        {
            caller->uses.clear();
        }
    };
    const auto pinned = ImportBytes(source, options);
    Require(pinned.status == Status::AcceptedUnderInterpretation && pinned.model, "request pin precedes progress callbacks");
}
void LaterFailureRetainsConflicts(std::string source)
{
    // Association 1 has an observed endpoint disagreement; association 2 is
    // source-valid but outside the native rational-pcurve construction scope.
    // Its fatal result must survive, together with the earlier observation.
    const auto association = source.find("(#921),.PCURVE_S1.");
    Require(association != std::string::npos, "public original association list");
    source.replace(association, std::strlen("(#921),.PCURVE_S1."), "(#921,#940),.PCURVE_S1.");
    const auto end = source.rfind("ENDSEC;");
    Require(end != std::string::npos, "public DATA terminator");
    source.insert(
        end,
        "#940=PCURVE('',#504,#941);\n"
        "#941=DEFINITIONAL_REPRESENTATION('',(#942),#930);\n"
        "#942=(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#924,#927,#928),.UNSPECIFIED.,.F.,.F.) "
        "B_SPLINE_CURVE_WITH_KNOTS((3,3),(-2.,8.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
        "RATIONAL_B_SPLINE_CURVE((1.,1.,1.)) REPRESENTATION_ITEM('public later unsupported association'));\n"
    );
    auto options = Config();
    auto request = Request(source);
    request->uses[0].pcurveIds.push_back("940");
    options.surfaceCurve3dInterpretation = request;
    const auto failed = ImportBytes(source, options);
    Require(
        failed.status == Status::Unsupported && !failed.model && failed.report.nativeCalls > 1,
        "later unsupported source representation stays fatal"
    );
    Require(
        !failed.report.diagnostics.empty() && failed.report.diagnostics[0].source.key == "GeneralPcurveParameterCurveUnsupported",
        "source construction errors never become eligible geometric conflicts"
    );
    const auto retained = [](const Outcome& result)
    {
        const auto& receipt = result.report.surfaceCurve3dInterpretation;
        Require(
            receipt && !receipt->requestComplete && receipt->actions.size() == 1 && !receipt->actions[0].nativeChecksPassed &&
                !receipt->actions[0].originalConflicts.empty(),
            "fatal exit retains previously observed conflicts without native qualification"
        );
        Require(
            result.report.json.find("GeneralSplineEndpointInversion") != std::string::npos,
            "mandatory failure report owns the earlier diagnostic"
        );
    };
    retained(failed);
    // The final comparison checkpoint is discovered from the public execution
    // receipt, not a hardcoded ordinal. Stop before it after the first conflict.
    auto limited = options;
    limited.nativeLimits.nativeCalls = failed.report.nativeCalls - 1;
    const auto resource = ImportBytes(source, limited);
    Require(resource.status == Status::ResourceLimit && !resource.model, "later budget failure remains fatal");
    retained(resource);
    auto stopped = options;
    stopped.execution.control = std::make_shared<ImportControl>();
    stopped.execution.progress = [&](Progress p)
    {
        if (p.phase == Phase::BeforeNativeCall && p.nativeCalls + 1 == failed.report.nativeCalls)
        {
            stopped.execution.control->RequestCancel();
        }
    };
    const auto cancelled = ImportBytes(source, stopped);
    Require(cancelled.status == Status::Cancelled && !cancelled.model, "later cancellation remains fatal");
    retained(cancelled);
}
void MultipleUses(std::string source)
{
    const auto edge = source.find("#213=EDGE_CURVE('',#111,#112,#212,.T.);");
    Require(edge != std::string::npos, "independent second public box edge");
    source.replace(edge, std::strlen("#213=EDGE_CURVE('',#111,#112,#212,.T.);"), "#213=EDGE_CURVE('',#111,#112,#940,.T.);");
    source.insert(
        source.rfind("ENDSEC;"),
        "#940=SURFACE_CURVE('',#212,(#941),.PCURVE_S1.);\n"
        "#941=PCURVE('',#504,#942);\n"
        "#942=DEFINITIONAL_REPRESENTATION('',(#943),#930);\n"
        "#943=B_SPLINE_CURVE_WITH_KNOTS('',1,(#944,#945),.UNSPECIFIED.,.F.,.F.,(2,2),(2.,5.),.UNSPECIFIED.);\n"
        "#944=CARTESIAN_POINT('',(0.,10.));\n#945=CARTESIAN_POINT('',(20.,10.));\n"
    );
    auto request = Request(source);
    request->maximumUses = 2;
    // Exercise both plan orders. Dispatch must use immutable source scope;
    // neither array order nor native edge index grants a choice.
    request->uses.insert(request->uses.begin(), { "7", "21", "213", "940", "212", SurfaceCurveDeclaredMaster::PcurveS1, { "941" } });
    auto options = Config();
    options.surfaceCurve3dInterpretation = request;
    const auto result = ImportBytes(source, options);
    Require(result.status == Status::AcceptedUnderInterpretation && result.model, "unordered complete multi-use interpretation");
    const auto receipt = result.model->SurfaceCurve3dInterpretation();
    Require(
        receipt && receipt->requestComplete && receipt->actions.size() == 2 && receipt->actions[0].planIndex < 2 &&
            receipt->actions[1].planIndex < 2 && receipt->actions[0].planIndex != receipt->actions[1].planIndex &&
            receipt->actions[0].nativeChecksPassed && receipt->actions[1].nativeChecksPassed,
        "each actual source edge completes its independently bound plan"
    );
    step_import_test::Near(*result.model->MaterialVolumeMm3(), 6000, .001, "independent multi-use box volume");
    auto reordered = std::make_shared<SurfaceCurve3dInterpretationRequest>(*request);
    std::swap(reordered->uses[0], reordered->uses[1]);
    options.surfaceCurve3dInterpretation = reordered;
    const auto otherOrder = ImportBytes(source, options);
    Require(
        otherOrder.status == Status::AcceptedUnderInterpretation && otherOrder.model &&
            otherOrder.model->SurfaceCurve3dInterpretation()->requestComplete,
        "plan order does not change admission or source scope"
    );
    auto missing = std::make_shared<SurfaceCurve3dInterpretationRequest>(*request);
    missing->uses[0].edgeId = "999";
    options.surfaceCurve3dInterpretation = missing;
    const auto unused = ImportBytes(source, options);
    Require(
        unused.status == Status::Unsupported && !unused.model && unused.report.surfaceCurve3dInterpretation &&
            !unused.report.surfaceCurve3dInterpretation->requestComplete && unused.report.surfaceCurve3dInterpretation->actions.size() == 1,
        "one completed use cannot publish a request with an unused second plan"
    );
}
void RepeatedAssociation(std::string source)
{
    const auto at = source.find("(#921),.PCURVE_S1.");
    Require(at != std::string::npos, "public association list for S2");
    source.replace(at, std::strlen("(#921),.PCURVE_S1."), "(#921,#921),.PCURVE_S2.");
    auto request = Request(source);
    request->uses[0].expectedMaster = SurfaceCurveDeclaredMaster::PcurveS2;
    request->uses[0].pcurveIds.push_back("921");
    auto options = Config();
    options.surfaceCurve3dInterpretation = request;
    const auto result = ImportBytes(source, options);
    Require(result.status == Status::AcceptedUnderInterpretation && result.model, "explicit original S2 with repeated source identity");
    std::vector<double> occurrences;
    for (const auto& mapping : result.report.parameterMappings)
    {
        if (mapping.kind == "SurfaceCurveAuxiliaryPcurveSourcePcurveComposition")
        {
            for (const auto& parameter : mapping.parameters)
            {
                if (parameter.first == "source_association_occurrence")
                {
                    Require(parameter.second.size() == 1, "one original association position");
                    occurrences.push_back(parameter.second[0]);
                }
            }
        }
    }
    Require(occurrences == std::vector<double>{ 1, 2 }, "repeated original identity does not erase either comparison obligation");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public source-pcurve box fixture required");
        std::ifstream input(argv[1], std::ios::binary);
        std::string source{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        Check(source, false);
        Check(source, false, ReportEncoding::CompactV2);
        Refusals(source);
        MultipleUses(source);
        RepeatedAssociation(source);
        auto reversed = source;
        const auto controls = reversed.find("(#924,#927,#928)");
        Require(controls != std::string::npos, "public source control order");
        reversed.replace(controls, std::strlen("(#924,#927,#928)"), "(#928,#927,#924)");
        Check(reversed, true);
        // Shift the whole ORIGINAL UV line sideways; source 3D remains exact.
        // The default path must still refuse, even after interpreted imports.
        for (const auto& pair : { std::pair{ "(0.,0.)", "(0.1,0.)" }, std::pair{ "(0.,2.)", "(0.1,2.)" }, std::pair{ "(0.,10.)", "(0.1,10.)" } })
        {
            const auto at = source.find(pair.first);
            Require(at != std::string::npos, "public pcurve control");
            source.replace(at, std::strlen(pair.first), pair.second);
        }
        const auto strict = ImportBytes(source, Config());
        Require(strict.status == Status::PostcheckFailure && !strict.model, "no default preference change or conflict exception");
        Check(source, true);
        LaterFailureRetainsConflicts(source);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
