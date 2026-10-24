// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSchemaInterpretationInput.h"

#include <algorithm>

namespace step_semantics
{
namespace
{
tl::unexpected<Diagnostic> Failure(const char* key, const char* message)
{
    Diagnostic diagnostic;
    diagnostic.source.key = key;
    diagnostic.source.stage = "D2";
    diagnostic.source.message = message;
    diagnostic.classification = "ProfileUnsupported";
    diagnostic.responsibility = "explicit source-bound schema interpretation request";
    return tl::unexpected(std::move(diagnostic));
}
} // namespace

tl::expected<void, Diagnostic> ValidateSchemaInterpretationInput(const step_source::InspectOutcome& source, const SchemaInterpretationRequest& request)
{
    if (request != SchemaInterpretation::SourceBoundAp214As2010)
    {
        if (!request.sourceSha256.empty())
        {
            return Failure("InvalidSchemaInterpretationRequest", "Only the explicit source-bound policy takes an input identity");
        }
        return {};
    }
    if (request.sourceSha256.size() != 64 || !std::all_of(
                                                 request.sourceSha256.begin(),
                                                 request.sourceSha256.end(),
                                                 [](char c)
                                                 {
                                                     return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                                 }
                                             ))
    {
        return Failure("InvalidSchemaInterpretationRequest", "Source-bound interpretation requires a canonical original SHA256");
    }
    // Source inspection owns immutable bytes. Mutable receipt fields must agree
    // with that owner; changing an outcome's hash/capture cannot authorize a new
    // file. No source edit, second path read or inferred schema edition occurs.
    if (source.source && (request.sourceSha256 != source.source->Sha256() || source.sha256 != source.source->Sha256() || !source.captured ||
                          (source.captured.get() != &source.source->Bytes() && *source.captured != source.source->Bytes())))
    {
        return Failure(
            "SourceInterpretationInputMismatch",
            "Schema interpretation must bind the immutable original capture and its consistent receipt"
        );
    }
    return {};
}
} // namespace step_semantics
