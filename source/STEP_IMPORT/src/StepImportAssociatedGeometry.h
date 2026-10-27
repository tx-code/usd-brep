// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"
namespace step_import::detail
{
class Execution;
namespace construction
{
class GeometryConstruction;
}
bool HasAssociatedGeometry(const step_semantics::Snapshot& source);
// Shared source bindings, factories, role traversal and per-item checkpoints.
// The caller holds NativeMutex; the target owns its allocating context. A part
// owner has no BRep and is never exposed as a material ModelSnapshot.
[[nodiscard]] Result<void> CheckAssociatedGeometry(const construction::GeometryConstruction& inputs);
[[nodiscard]] Result<void> BuildAssociatedGeometry(const construction::GeometryConstruction& inputs, ModelData& target);
[[nodiscard]] Result<std::shared_ptr<ModelData>> BuildPartAssociatedGeometry(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
);
} // namespace step_import::detail
