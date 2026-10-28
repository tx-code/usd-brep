// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <fstream>
#include <iostream>
namespace
{
using namespace step_import;
using step_import_test::Require;
Options Settings(bool preserve)
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    if (preserve)
    {
        o.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
        o.headerMetadataPolicy = step_source::HeaderMetadataPolicy::PreserveUnsetAuthorization;
    }
    return o;
}
void Replace(std::string& text, const std::string& a, const std::string& b)
{
    const auto at = text.find(a);
    Require(at != std::string::npos, "public header mutation token");
    text.replace(at, a.size(), b);
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public native fixture required");
        std::ifstream in(argv[1], std::ios::binary);
        Require(bool(in), "public fixture readable");
        const std::string original{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        auto good = ImportBytes(original, Settings(false));
        Require(good.model != nullptr, "unchanged valid source baseline");
        auto input = original;
        const auto name = input.find("FILE_NAME(");
        const auto end = input.find(';', name);
        // The frozen synthetic header ends in an empty authorization STRING.
        const auto authorization = input.rfind("''", end);
        Require(name != std::string::npos && authorization > name, "frozen header authorization");
        input.replace(authorization, 2, "$");
        auto strict = ImportBytes(input, Settings(false));
        Require(
            strict.status == Status::Rejected && !strict.model && strict.report.nativeCalls == 0,
            "strict carrier rejects original malformed header"
        );
        auto options = Settings(true);
        auto result = ImportBytes(input, options);
        Require(
            result.status == Status::AcceptedUnderInterpretation && result.model,
            "explicit metadata exception retains complete native qualification scope"
        );
        const auto owner = result.model;
        const auto& source = owner->Source()->Source();
        Require(
            source.Bytes() == input && source.Header()[1].parameters[6].kind == step_source::ValueKind::Unset &&
                source.Raw(source.Header()[1].parameters[6].span) == "$",
            "original unset is not an invented string"
        );
        Require(
            source.PreservedHeaderIssues().size() == 1 && result.report.json.find("KnownNonGeometricErrorPreserved") != std::string::npos,
            "immutable original issue in mandatory report"
        );
        auto a = good.model->CopyBrepForModeling(), b = owner->CopyBrepForModeling();
        Require(a.model && b.model, "independent baseline and interpreted copies");
        a.model->Edit(
            [&](SmBrep& first)
            {
                b.model->Edit(
                    [&](SmBrep& second)
                    {
                        step_import_test::CompareNativeDefinitions(first, second);
                    }
                );
            }
        );
        for (auto phase : { Phase::NativeReady, Phase::BeforeReport, Phase::BeforeCommit })
        {
            auto canceled = options;
            canceled.execution.control = std::make_shared<ImportControl>();
            canceled.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    canceled.execution.control->RequestCancel();
                }
            };
            auto failed = ImportBytes(input, canceled);
            Require(failed.status == Status::Cancelled && !failed.model, "header exception cannot bypass cancellation commit");
        }
        auto diagnosticLimited = options;
        diagnosticLimited.semanticLimits.preservedDiagnostics = 0;
        const auto noDiagnosticCapacity = ImportBytes(input, diagnosticLimited);
        Require(
            noDiagnosticCapacity.status == Status::ResourceLimit && !noDiagnosticCapacity.model && noDiagnosticCapacity.report.nativeCalls == 0,
            "header exceptions consume the shared diagnostic budget before native construction"
        );
        diagnosticLimited.semanticLimits.preservedDiagnostics = 1;
        Require(ImportBytes(input, diagnosticLimited).model != nullptr, "one header issue fits exact diagnostic capacity");
        auto bareSchema = input;
        const auto schema = bareSchema.find("FILE_SCHEMA((");
        const auto schemaEnd = bareSchema.find(';', schema);
        Require(schema != std::string::npos && schemaEnd != std::string::npos, "synthetic schema declaration");
        bareSchema.replace(schema, schemaEnd - schema, "FILE_SCHEMA(('AUTOMOTIVE_DESIGN'))");
        diagnosticLimited.schemaInterpretation = step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010;
        Require(
            ImportBytes(bareSchema, diagnosticLimited).status == Status::ResourceLimit,
            "header and explicit schema assumptions share one diagnostic capacity"
        );
        diagnosticLimited.semanticLimits.preservedDiagnostics = 2;
        Require(ImportBytes(bareSchema, diagnosticLimited).model != nullptr, "two preserved issues fit exact capacity");
        auto limited = options;
        limited.nativeLimits.nativeCalls = result.report.nativeCalls - 1;
        auto failed = ImportBytes(input, limited);
        Require(failed.status == Status::ResourceLimit && !failed.model, "native N-1 remains atomic");
        limited = options;
        limited.nativeLimits.reportBytes = 1;
        failed = ImportBytes(input, limited);
        Require(failed.status == Status::ResourceLimit && !failed.model, "mandatory header receipt cannot be dropped for publication");
        auto bad = input;
        Replace(bad, "ELLIPSE('',#51,2.5,2.)", "ELLIPSE('',#51,2.6,2.)");
        Require(!ImportBytes(bad, options).model, "nongeometric header option cannot admit off-surface geometry");
        auto invalid = options;
        invalid.sourcePolicy = step_semantics::SourcePolicy::Strict;
        bool caught = false;
        try
        {
            (void)ImportBytes(input, invalid);
        }
        catch (const std::invalid_argument&)
        {
            caught = true;
        }
        Require(caught, "strict native caller cannot select header exception");
        auto raw = step_source::InspectBytes(
            input,
            options.sourceLimits,
            step_source::HeaderNamePolicy::StrictAscii,
            step_source::DataStringPolicy::StrictAscii,
            step_source::CarrierInterpretation::DeclaredOnly,
            step_source::HeaderMetadataPolicy::PreserveUnsetAuthorization
        );
        raw.headerMetadataPolicy = step_source::HeaderMetadataPolicy::Strict;
        raw.preservedHeaderIssues.clear();
        raw.diagnostics.clear();
        caught = false;
        try
        {
            (void)step_semantics::Check(raw, options.semanticLimits, step_semantics::SourcePolicy::Strict, step_semantics::GeometryScope::GeneralBrep);
        }
        catch (const std::invalid_argument&)
        {
            caught = true;
        }
        Require(caught, "immutable source issue blocks strict semantic rebinding after notice reset");
        result = {};
        Require(owner->Source()->Source().PreservedHeaderIssues().size() == 1, "owning model keeps header issue after outcome release");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
