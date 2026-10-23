// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyImport.h"
#include "StepImportAssemblyEvidence.h"

#include <unordered_map>

namespace step_import::detail
{
struct AssemblySnapshotData
{
    AssemblyNativeCandidate native;
    std::shared_ptr<const AssemblyEvidence> evidence;
    std::unordered_map<std::size_t, std::size_t> definitionIndices;
};
struct AssemblyAccess
{
    static std::shared_ptr<const AssemblySnapshot> Seal(AssemblyNativeCandidate candidate, std::shared_ptr<const AssemblyEvidence> evidence);
};
} // namespace step_import::detail
