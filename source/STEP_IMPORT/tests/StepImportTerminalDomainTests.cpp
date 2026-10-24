// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"
#include "StepImportRevolutionFrameProof.h"

#include <SmBrepData.h>
#include <SmCrvOnSurf.h>
#include <SmFace.h>
#include <SmNativeArchive.h>
#include <SmSurfOfRevolution.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <numbers>

using namespace step_import;
using step_import_test::Near;
using step_import_test::Require;
namespace
{
void FrameChecks()
{
    using namespace step_import::detail;
    namespace ts = terminal_spline;
    ts::BinaryNet net;
    net.homogeneous = { { 0, 3, 0, 1 }, { 0, 3, 7, 1 } };
    const ts::Limits limits{ 1000, 4096, 1234 };
    const ts::Charge charge = [](std::size_t) -> Result<void>
    {
        return {};
    };
    const std::array<Point, 3> identity{ Point{ 1, 0, 0 }, Point{ 0, 1, 0 }, Point{ 0, 0, 1 } };
    const auto calculate = [&](const auto& frame)
    {
        return TerminalRevolutionFrameBound({ "0", "0", "1" }, { 0, 0, 1 }, { "0", "0", "0" }, { "1", "1" }, {}, frame, net, limits, charge);
    };
    const auto shortBound = calculate(identity);
    Require(shortBound && *shortBound >= 0 && *shortBound < 1e-9, "whole-angle cardinal frame bound");
    auto damaged = identity;
    damaged[0][0] = 1.01;
    const auto wrong = calculate(damaged);
    Require(wrong && *wrong > .01, "a scaled native frame cannot pass as an isometry");
    net.homogeneous.back()[2] = 1e8;
    const auto longBound = calculate(identity);
    Require(longBound && *longBound > *shortBound * 1000000, "frame proof uses the new-domain control hull rather than the old source interval");
    const auto stopped = TerminalRevolutionFrameBound(
        { "0", "0", "1" },
        { 0, 0, 1 },
        { "0", "0", "0" },
        { "1", "1" },
        {},
        identity,
        net,
        limits,
        [](std::size_t) -> Result<void>
        {
            return tl::unexpected(Failure{ Status::ResourceLimit, "CallerFrameWorkLimit", "caller", 42, "Caller" });
        }
    );
    Require(
        !stopped && stopped.error().key == "CallerFrameWorkLimit" && stopped.error().record == 42,
        "frame proof preserves caller execution failure"
    );
}
void Replace(std::string& text, std::string_view old, std::string_view next)
{
    const auto offset = text.find(old);
    Require(offset != std::string::npos, "public fixture mutation anchor");
    text.replace(offset, old.size(), next);
}
std::string Fixture(const std::filesystem::path& path, bool revolution, bool tilted)
{
    std::ifstream input(path, std::ios::binary);
    std::string bytes{ std::istreambuf_iterator<char>(input), {} };
    if (revolution)
    {
        // Public cylinder: the source generator defines z=.7*(v+2) only on
        // [-2,7]. Its retained pcurve reaches v=8, where the explicit terminal
        // formula gives z=7. The auxiliary edge is the exact segment z=0..7.
        Replace(bytes, "B_SPLINE_CURVE(1,(#102,#103)", "B_SPLINE_CURVE(1,(#102,#943)");
        Replace(bytes, "(-2.,8.),.UNSPECIFIED.)", "(-2.,7.),.UNSPECIFIED.)");
        Replace(bytes, "RATIONAL_B_SPLINE_CURVE((1.,2.))", "RATIONAL_B_SPLINE_CURVE((1.,1.))");
        Replace(bytes, "#960=SURFACE_CURVE", "#943=CARTESIAN_POINT('',(0.,3.,6.3));\n#960=SURFACE_CURVE");
        if (tilted)
        {
            // Exact rigid rotation with sin=.6, cos=.8, independent of SMLib.
            Replace(bytes, "#101=CARTESIAN_POINT('',(0.,0.,7.));", "#101=CARTESIAN_POINT('',(4.2,0.,5.6));");
            Replace(bytes, "#103=CARTESIAN_POINT('',(0.,3.,7.));", "#103=CARTESIAN_POINT('',(4.2,3.,5.6));");
            Replace(bytes, "#104=DIRECTION('',(0.,0.,1.));", "#104=DIRECTION('',(0.6,0.,0.8));");
            Replace(bytes, "#105=DIRECTION('',(1.,0.,0.));", "#105=DIRECTION('',(0.8,0.,-0.6));");
            Replace(bytes, "#941=AXIS1_PLACEMENT('source z axis',#100,$);", "#941=AXIS1_PLACEMENT('source z axis',#100,#104);");
            Replace(bytes, "#943=CARTESIAN_POINT('',(0.,3.,6.3));", "#943=CARTESIAN_POINT('',(3.78,3.,5.04));");
        }
    }
    else
    {
        // Public bottom plane S(u,v)=(2+v,-3+u,5), initially v=0..8.
        // The original UV quadratic reaches v=10; extend the SAME formula.
        Replace(bytes, "#501=DIRECTION('',(0.,0.,-1.));", "");
        Replace(bytes, "#502=DIRECTION('',(0.,1.,0.));", "");
        Replace(bytes, "#503=AXIS2_PLACEMENT_3D('',#100,#501,#502);", "");
        Replace(
            bytes,
            "#504=PLANE('',#503);",
            "#504=B_SPLINE_SURFACE_WITH_KNOTS('',1,1,((#950,#951),(#952,#953)),.UNSPECIFIED.,.F.,.F.,.F.,(2,2),(2,2),(0.,20.),(0.,8.),.UNSPECIFIED.);\n"
            "#950=CARTESIAN_POINT('',(2.,-3.,5.));\n#951=CARTESIAN_POINT('',(10.,-3.,5.));\n"
            "#952=CARTESIAN_POINT('',(2.,17.,5.));\n#953=CARTESIAN_POINT('',(10.,17.,5.));"
        );
    }
    return bytes;
}
std::string Assembly(std::string bytes)
{
    // Two occurrences share ONE interpreted part definition. The parent is a
    // placement-only definition, so neither cloning nor body omission can pass.
    Replace(bytes, "'body',(#21),#12", "'body',(#21,#2931),#12");
    const std::string records = R"STEP(
#2900=PRODUCT('assembly','public parent',$,(#3));
#2901=PRODUCT_DEFINITION_FORMATION('A',$,#2900);
#2902=PRODUCT_DEFINITION('parent',$,#2901,#6);
#2903=PRODUCT_DEFINITION_SHAPE('',$,#2902);
#2904=SHAPE_DEFINITION_REPRESENTATION(#2903,#2920);
#2910=NEXT_ASSEMBLY_USAGE_OCCURRENCE('instance-a','same label',$,#2902,#7,$);
#2911=PRODUCT_DEFINITION_SHAPE('',$,#2910);
#2912=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#2914,#2911);
#2914=(REPRESENTATION_RELATIONSHIP('',$,#20,#2920) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#2915) SHAPE_REPRESENTATION_RELATIONSHIP());
#2915=ITEM_DEFINED_TRANSFORMATION('',$,#2931,#2933);
#2916=NEXT_ASSEMBLY_USAGE_OCCURRENCE('instance-b','same label',$,#2902,#7,$);
#2917=PRODUCT_DEFINITION_SHAPE('',$,#2916);
#2918=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#2924,#2917);
#2920=SHAPE_REPRESENTATION('parent',(#2933,#2935),#2921);
#2921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) REPRESENTATION_CONTEXT('parent','3D'));
#2924=(REPRESENTATION_RELATIONSHIP('',$,#20,#2920) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#2925) SHAPE_REPRESENTATION_RELATIONSHIP());
#2925=ITEM_DEFINED_TRANSFORMATION('',$,#2931,#2935);
#2930=CARTESIAN_POINT('',(0.,0.,0.));
#2931=AXIS2_PLACEMENT_3D('',#2930,#2936,#2937);
#2932=CARTESIAN_POINT('',(100.,200.,300.));
#2933=AXIS2_PLACEMENT_3D('',#2932,#2936,#2938);
#2934=CARTESIAN_POINT('',(-40.,0.,0.));
#2935=AXIS2_PLACEMENT_3D('',#2934,#2936,#2937);
#2936=DIRECTION('',(0.,0.,1.));
#2937=DIRECTION('',(1.,0.,0.));
#2938=DIRECTION('',(0.,1.,0.));
)STEP";
    const auto end = bytes.rfind("ENDSEC;");
    Require(end != std::string::npos, "public assembly DATA closure");
    bytes.insert(end, records);
    return bytes;
}
Options Request(const std::string& bytes, bool revolution)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.numericalIntegration = NumericalIntegrationSettings{ 1e-8 };
    const auto raw = step_source::InspectBytes(bytes, options.sourceLimits);
    Require(raw.source && raw.status == step_source::Status::Inspected, "public source captured once for explicit identity");
    TerminalDomainSurfacePlan plan;
    plan.surfaceId = revolution ? "142" : "504";
    plan.pcurveIds = { revolution ? "961" : "921" };
    plan.v = revolution ? std::array<std::string, 2>{ "-2", "8" } : std::array<std::string, 2>{ "0", "10" };
    options.terminalDomainInterpretation = TerminalDomainInterpretationRequest{ raw.source->Sha256(), { plan }, 1, 100000, 10000000 };
    return options;
}
void Verify(SmBrep& brep, bool revolution, bool tilted)
{
    SmTArray<SmEdge*> edges;
    brep.GetEdges(edges);
    unsigned compositions = 0;
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        auto* curve = dynamic_cast<SmCrvOnSurf*>(edges[i]->GetCurve());
        if (!curve)
        {
            continue;
        }
        ++compositions;
        for (const auto a : { 0., .2, .5, 1. })
        {
            SmVector3d value;
            const double t = revolution ? -3 + 12 * a : -2 + 10 * a;
            Require(curve->Evaluate(t, 0, TRUE, &value) == SM_SUCCESS, "new-domain source composition evaluates");
            const double z = .7 * (4 * a + 6 * a * a);
            Near(value.x, revolution ? (tilted ? .6 * z : 0) : 2 + 4 * a + 6 * a * a, 1e-9, "independent terminal formula x");
            Near(value.y, revolution ? 3 : -3, 1e-9, "independent terminal formula y");
            Near(value.z, revolution ? (tilted ? .8 * z : z) : 5, 1e-9, "independent terminal formula z");
        }
    }
    Require(compositions == 1, "source master curve identity survives native copy/archive");
    SmTArray<SmFace*> faces;
    brep.GetFaces(faces);
    unsigned admitted = 0;
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        if ((revolution && dynamic_cast<SmSurfOfRevolution*>(faces[i]->GetSurface())) ||
            (!revolution && typeid(*faces[i]->GetSurface()) == typeid(SmBSplineSurface)))
        {
            ++admitted;
        }
    }
    Require(admitted == 1, "native basis class is retained");
    step_import_test::CheckNativeWorkBudget(brep, .001);
}
void Run(const std::filesystem::path& path, const std::filesystem::path& output, bool revolution, bool tilted = false)
{
    const auto bytes = Fixture(path, revolution, tilted);
    const auto options = Request(bytes, revolution);
    auto strict = options;
    strict.terminalDomainInterpretation.reset();
    const auto ordinary = ImportBytes(bytes, strict);
    if (ordinary.status != Status::Indeterminate)
    {
        std::cerr << ordinary.report.json << '\n';
    }
    Require(ordinary.status == Status::Indeterminate && !ordinary.model, "ordinary path retains its source-domain refusal");
    auto result = ImportBytes(bytes, options);
    if (result.status != Status::AcceptedUnderInterpretation)
    {
        std::cerr << result.report.json << '\n'; // Only these public synthetic inputs.
    }
    Require(result.status == Status::AcceptedUnderInterpretation && result.model, "explicit whole-model development interpretation");
    const auto receipt = result.model->TerminalDomainInterpretation();
    Require(
        receipt && receipt->requestComplete && !receipt->actions.empty() && receipt == result.report.terminalDomainInterpretation,
        "read-only model owns the request evidence"
    );
    Require(receipt->proofWorkUnits > 0 && receipt->proofWorkUnits <= receipt->maximumProofWorkUnits, "actual cumulative exact work");
    for (const auto& action : receipt->actions)
    {
        Require(action.outsideSourceSemanticsAssumed && action.wholeNewDomainErrorUpperMm <= 1e-9, "one unchanged definition budget");
        Require(action.originalDomainDiagnostic == "GeneralPcurveBasisDomainUnestablished", "ordinary domain diagnostic remains recorded");
    }
    Require(result.model->Source()->Source().Bytes() == bytes, "original definitions and UV bytes remain immutable");
    Require(
        result.report.json.find("\"strict_original_geometry_qualified\":false") != std::string::npos,
        "report never promotes the interpreted source"
    );
    Near(*result.model->MaterialVolumeMm3(), revolution ? 63 * std::numbers::pi : 6000, .001, "independent analytic volume");
    Near(result.model->Info().areaMm2, revolution ? 60 * std::numbers::pi : 2200, .001, "independent analytic area");
    const auto assemblyBytes = Assembly(bytes);
    const auto assemblyOptions = Request(assemblyBytes, revolution);
    const auto assembly = ImportAssemblyBytes(assemblyBytes, assemblyOptions, { 100, 100, 16 });
    if (assembly.status != Status::AcceptedUnderInterpretation)
    {
        std::cerr << "Public assembly status " << StatusName(assembly.status) << '\n';
        for (const auto& diagnostic : assembly.report.diagnostics)
        {
            std::cerr << diagnostic.source.key << '\n';
        }
    }
    Require(
        assembly.status == Status::AcceptedUnderInterpretation && assembly.assembly && assembly.assembly->DefinitionCount() == 2 &&
            assembly.assembly->InstanceCount() == 3 && assembly.assembly->BodyCount(0) == 1 && assembly.assembly->BodyCount(1) == 0,
        "explicit assembly entry commits the complete public root"
    );
    Require(
        assembly.assembly->Body(0, 0)->TerminalDomainInterpretation() == assembly.report.terminalDomainInterpretation &&
            assembly.report.terminalDomainInterpretation->requestComplete,
        "assembly root and body share the owning interpretation receipt"
    );
    auto copy = result.model->CopyForModeling();
    Require(copy.status == Status::Accepted && copy.model, "independent modeling copy");
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            Verify(brep, revolution, tilted);
        }
    );
    const auto archive = output / (revolution ? (tilted ? "terminal-tilted-cylinder.smb" : "terminal-cylinder.smb") : "terminal-box.smb");
    result.model->WriteNative(archive);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&breps);
    Require(
        SmNativeArchive::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS && breps.GetSize() == 1,
        "native archive contains the complete interpreted model"
    );
    Verify(*breps[0], revolution, tilted);
    result = {};
    Require(receipt->requestComplete, "evidence survives discarded outcome");
    copy.model->Edit(
        [&](SmBrep& brep)
        {
            Verify(brep, revolution, tilted);
        }
    );
    for (const auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
    {
        auto cancelled = options;
        cancelled.execution.control = std::make_shared<ImportControl>();
        cancelled.execution.progress = [&, phase](Progress p)
        {
            if (p.phase == phase)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        const auto stopped = ImportBytes(bytes, cancelled);
        Require(stopped.status == Status::Cancelled && !stopped.model, "cancelled interpretation never publishes");
    }
    auto limited = options;
    limited.terminalDomainInterpretation->maximumProofWorkUnits = 1;
    const auto exhausted = ImportBytes(bytes, limited);
    Require(exhausted.status == Status::ResourceLimit && !exhausted.model, "exact-work exhaustion never publishes");
    limited = options;
    limited.nativeLimits.reportBytes = 1;
    const auto reportFailure = ImportBytes(bytes, limited);
    Require(reportFailure.status == Status::ResourceLimit && !reportFailure.model, "required report precedes interpretation publication");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "two public fixtures and disposable archive directory required");
        FrameChecks();
        std::filesystem::create_directories(argv[3]);
        Run(argv[1], argv[3], false);
        Run(argv[2], argv[3], true);
        Run(argv[2], argv[3], true, true);
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
