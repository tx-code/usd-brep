// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepAssemblyInputs.h"
#include "StepSemantics.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
step_source::InspectOutcome BoundInput(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    std::string bytes{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
    const auto start = bytes.find("FILE_SCHEMA((");
    const auto end = bytes.find(";", start);
    Require(start != std::string::npos && end != std::string::npos, "public schema fixture");
    bytes.replace(start, end - start, "FILE_SCHEMA(('AUTOMOTIVE_DESIGN {1 0 10303 214 -1 1 1 1}'))");
    return step_source::InspectBytes(std::move(bytes), step_source::Limits::PlanarFixtures());
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "public analytic box, assembly and independent fixtures required");
        std::ifstream stream(argv[1], std::ios::binary);
        std::string bytes{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        const auto start = bytes.find("FILE_SCHEMA((");
        const auto end = bytes.find(";", start);
        Require(start != std::string::npos && end != std::string::npos, "public schema fixture");
        bytes.replace(start, end - start, "FILE_SCHEMA(('AUTOMOTIVE_DESIGN {1 0 10303 214 -1 1 1 1}'))");
        auto raw = step_source::InspectBytes(bytes, step_source::Limits::PlanarFixtures());
        Require(bool(raw.source), "complete immutable synthetic source capture");
        using namespace step_semantics;
        const auto original = raw.source->Sha256();
        SchemaInterpretationRequest request{ SchemaInterpretation::SourceBoundAp214As2010, original };
        const auto check = [&](const step_source::InspectOutcome& input, const SchemaInterpretationRequest& choice)
        {
            return Check(input, {}, SourcePolicy::PreserveNonGeometric, GeometryScope::GeneralBrep, choice);
        };
        auto checked = check(raw, request);
        Require(checked.status == Status::CheckedWithDiagnostics && checked.snapshot, "explicit source-bound interpretation");
        auto snapshot = checked.snapshot;
        Require(snapshot->AppliedInterpretation() == SchemaInterpretation::SourceBoundAp214As2010, "owning policy qualification");
        Require(snapshot->InterpretationDiagnostics().size() == 1, "original unsupported binding retained");
        Require(snapshot->SchemaIdentity().match == SchemaMatch::Malformed, "original malformed identity is not corrected");
        checked = {};
        request.sourceSha256.clear();
        Require(
            snapshot->AppliedInterpretationRequest().sourceSha256 == original && snapshot->Source().Bytes() == bytes,
            "snapshot owns the applied input request after caller/report disposal"
        );

        auto forged = raw;
        forged.sha256.assign(64, '0');
        const SchemaInterpretationRequest wrong{ SchemaInterpretation::SourceBoundAp214As2010, forged.sha256 };
        auto failure = check(forged, wrong);
        Require(
            failure.status == Status::Unsupported && !failure.snapshot &&
                failure.diagnostics.front().source.key == "SourceInterpretationInputMismatch",
            "mutable receipt hash cannot rebind immutable source"
        );
        forged = raw;
        forged.captured = std::make_shared<const std::string>("different capture");
        request = { SchemaInterpretation::SourceBoundAp214As2010, original };
        failure = check(forged, request);
        Require(
            failure.status == Status::Unsupported && !failure.snapshot &&
                failure.diagnostics.front().source.key == "SourceInterpretationInputMismatch",
            "mutable capture cannot borrow a qualified immutable owner"
        );
        failure = check(raw, { SchemaInterpretation::DeclaredOnly, original });
        Require(
            failure.status == Status::Unsupported && !failure.snapshot &&
                failure.diagnostics.front().source.key == "InvalidSchemaInterpretationRequest",
            "unrelated policies cannot ignore a source binding argument"
        );
        raw = {};
        Require(
            snapshot->AppliedInterpretationRequest().sourceSha256 == original && snapshot->Source().Bytes() == bytes,
            "source and assumption survive inspection disposal"
        );

        auto independentRaw = BoundInput(argv[3]);
        Require(bool(independentRaw.source), "independent public capture");
        const SchemaInterpretationRequest independentRequest{ SchemaInterpretation::SourceBoundAp214As2010, independentRaw.source->Sha256() };
        auto independent = CheckIndependentProducts(independentRaw, {}, SourcePolicy::PreserveNonGeometric, independentRequest);
        Require(
            independent.status == Status::CheckedWithDiagnostics && independent.document && independent.document->PartCount() == 2,
            "complete independent document preserves the input-bound interpretation"
        );
        auto second = independent.document->Part(1);
        independent = {};
        independentRaw = {};
        Require(second->AppliedInterpretationRequest() == independentRequest, "owning independent part retains complete request");

        auto assemblyRaw = BoundInput(argv[2]);
        Require(bool(assemblyRaw.source), "assembly public capture");
        const SchemaInterpretationRequest assemblyRequest{ SchemaInterpretation::SourceBoundAp214As2010, assemblyRaw.source->Sha256() };
        auto assembly = CheckAssemblySource(assemblyRaw, {}, SourcePolicy::PreserveNonGeometric, assemblyRequest);
        Require(
            assembly.document && assembly.document->DefinitionCount() == 2,
            "complete assembly source accepts the bound interpretation without flattening definitions"
        );
        auto definition = assembly.document->Definition(1);
        auto structure = assembly.checks.productStructure;
        Require(structure && structure->Basis().schemaInterpretation == assemblyRequest, "declared structure retains input binding");
        assembly = {};
        assemblyRaw = {};
        Require(
            definition->AppliedInterpretationRequest() == assemblyRequest && structure->Basis().schemaInterpretation == assemblyRequest,
            "assembly source and structure own request identity independently of their reports"
        );
        std::cout << "source-bound schema ownership and forged receipt contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
