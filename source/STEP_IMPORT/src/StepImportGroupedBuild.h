// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

namespace step_import::detail
{
using MaterialCandidates = std::vector<std::shared_ptr<ModelData>>;
using DocumentCandidate = std::shared_ptr<const DocumentSnapshot>;

// Keep result vocabulary out of the common native-layout header: only grouped
// builders and their entrypoint/publication consumer require tl::expected.
Result<DocumentCandidate> BuildDocument(
    std::shared_ptr<const step_semantics::ProductDocument> source,
    const Options& options,
    Report& report,
    Execution& execution
);
Result<MaterialCandidates> BuildMaterialCollection(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
);
} // namespace step_import::detail
