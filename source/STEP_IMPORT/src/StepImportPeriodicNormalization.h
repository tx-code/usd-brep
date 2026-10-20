// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <span>

namespace step_import::detail
{
struct ModelData;
// All indices refer to the immutable model.source snapshot; source arrays in
// ModelData retain original vertices/edges. Native handles never leave this
// serialized unpublished-candidate operation. Failure discards the ENTIRE BRep.
// Only nominal analytic cylinder/cone faces with a single explicit outer bound
// and a U seam near-miss are considered. Every native/proof work boundary is
// charged through step; read-only inventory/coefficients are not native calls.
void NormalizePeriodicTopology(
    ModelData& model,
    std::span<const std::size_t> faceRecords,
    std::span<const std::size_t> edgeRecords,
    std::span<const std::size_t> vertexRecords,
    std::span<const std::size_t> derivedEdgeFaces,
    const Options& options,
    Report& report,
    const std::function<void(std::size_t)>& step
);
} // namespace step_import::detail
