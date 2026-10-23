// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyImport.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace step_import;
void Require(bool b, const char* message)
{
    if (!b)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "pass synthetic assembly");
        std::ifstream file(argv[1], std::ios::binary);
        Require(bool(file), "fixture exists");
        const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
        auto options = Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = Quality::GeneralDevelopment();
        const AssemblyLimits limits{ 2, 3, 1 };
        auto result = ImportAssemblyBytes(text, options, limits);
        if (result.status != Status::Accepted)
        {
            std::cerr << StatusName(result.status) << '\n';
            for (const auto& d : result.report.diagnostics)
            {
                std::cerr << d.source.key << '\n';
            }
        }
        Require(
            result.status == Status::Accepted && result.assembly && !result.report.json.empty(),
            "complete public model and mandatory report published together"
        );
        auto model = result.assembly;
        Require(
            model->DefinitionCount() == 2 && model->BodyCount(0) == 1 && model->BodyCount(1) == 0 && model->InstanceCount() == 3,
            "public definition sharing and wrapper preserved"
        );
        Require(model->DefinitionRole(1) == "AssemblyPlacementWrapper" && model->SourceDefinitionId(0) == "7", "source identity and role");
        auto first = model->Instance(1), second = model->Instance(2);
        Require(
            first.definitionIndex == second.definitionIndex && first.parentIndex == 0 && second.parentIndex == 0 && first.sourceUsageId == "910" &&
                second.sourceUsageId == "916",
            "repeated instance paths remain distinct"
        );
        Require(
            first.translationToRootMm == Point{ 100, 200, 300 } && second.translationToRootMm == Point{ -40, 0, 0 },
            "independent placement oracle"
        );
        auto body = model->Body(0, 0);
        Require(body->MaterialVolumeMm3() && std::abs(*body->MaterialVolumeMm3() - 6000) < 1e-6, "local body volume");
        result = {};
        Require(
            model->Source()->Definition(0)->Source().Bytes() == text && model->Instance(1).sourceUsageId == "910",
            "clearing mutable report/outcome does not erase snapshot evidence"
        );
        auto fileResult = ImportAssemblyFile(argv[1], options, limits);
        Require(fileResult.status == Status::Accepted && fileResult.assembly, "file entry uses same complete pipeline");
        auto interpretedText = text;
        const std::string verifiedSchema = "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}";
        auto schemaAt = interpretedText.find(verifiedSchema);
        Require(schemaAt != std::string::npos, "synthetic schema");
        interpretedText.replace(schemaAt, verifiedSchema.size(), "AUTOMOTIVE_DESIGN");
        auto explicitOptions = options;
        explicitOptions.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
        explicitOptions.schemaInterpretation = step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010;
        auto interpreted = ImportAssemblyBytes(interpretedText, explicitOptions, limits);
        Require(
            interpreted.status == Status::AcceptedUnderInterpretation && interpreted.assembly &&
                interpreted.assembly->Source()->Definition(0)->AppliedInterpretation() == step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010,
            "assembly success does not authenticate an explicitly assumed schema"
        );
        for (const auto phase :
             { Phase::BeforeCapture, Phase::SemanticsChecked, Phase::BeforeNativeCall, Phase::NativeReady, Phase::ReportReady, Phase::BeforeCommit })
        {
            auto cancelledOptions = options;
            auto control = std::make_shared<ImportControl>();
            cancelledOptions.execution.control = control;
            cancelledOptions.execution.progress = [control, phase](Progress progress)
            {
                if (progress.phase == phase)
                {
                    control->RequestCancel();
                }
            };
            auto cancelled = ImportAssemblyBytes(text, cancelledOptions, limits);
            Require(cancelled.status == Status::Cancelled && !cancelled.assembly, "every public boundary cancellation withholds model");
            Require(
                cancelled.report.json.empty() || cancelled.report.json.find("step-assembly-development.v1") != std::string::npos,
                "assembly failure keeps explicit report scope even before candidate"
            );
        }
        auto small = options;
        small.nativeLimits.reportBytes = 512;
        auto limited = ImportAssemblyBytes(text, small, limits);
        Require(
            limited.status == Status::ResourceLimit && !limited.assembly && limited.report.json.empty(),
            "report budget prevents model publication"
        );
        std::string orphan = text;
        orphan.insert(orphan.find("ENDSEC;\nEND-ISO"), "#950=DIRECTION('',(1.,0.,0.));\n");
        auto rejected = ImportAssemblyBytes(orphan, options, limits);
        Require(
            rejected.status == Status::Unsupported && !rejected.assembly && rejected.report.nativeCalls == 0,
            "source closure failure precedes all native work"
        );
        Require(rejected.report.json.find("step-assembly-development.v1") != std::string::npos, "source failure is an assembly report");
        std::string unused = text;
        unused.insert(
            unused.find("ENDSEC;\nEND-ISO"),
            "#950=CARTESIAN_POINT('',(1.,2.,3.));\n#951=DIRECTION('',(1.,0.,0.));\n"
            "#952=DIRECTION('',(0.,0.,1.));\n#953=AXIS2_PLACEMENT_3D('',#950,#952,#951);\n"
        );
        Require(!ImportAssemblyBytes(unused, options, limits).assembly, "retaining unused frames requires explicit source policy");
        auto retained = ImportAssemblyBytes(unused, options, limits, step_semantics::UnreferencedPlacementPolicy::RetainDisconnected);
        Require(
            retained.status == Status::Accepted && retained.assembly && retained.assembly->DefinitionCount() == 2 &&
                retained.assembly->BodyCount(0) == 1 && retained.assembly->InstanceCount() == 3 &&
                retained.assembly->Source()->RetainedPlacements()->records.size() == 4,
            "explicit retention preserves declarations without changing model inventory"
        );
        Require(
            retained.report.json.find("\"target_geometry_created\":false") != std::string::npos &&
                retained.report.json.find("\"units_assigned\":false") != std::string::npos &&
                retained.report.json.find("UnownedOrOrphanProfileContent") != std::string::npos,
            "complete report retains proof and original unsupported diagnostic"
        );
        auto retainedModel = retained.assembly;
        retained = {};
        Require(
            retainedModel->Source()->RetainedPlacements()->records.size() == 4 &&
                retainedModel->Body(0, 0)->MaterialVolumeMm3() == body->MaterialVolumeMm3(),
            "retained source evidence survives report release with identical body geometry"
        );
        bool invalid = false;
        try
        {
            ImportAssemblyBytes(text, options, {});
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Require(invalid, "assembly expansion budgets require explicit configuration");
        model.reset();
        Require(body->MaterialVolumeMm3().has_value(), "definition body view independently owns its native lifetime");
        std::cout << "Public assembly publication contracts passed\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
