// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <SmTypes.h>
#include <span>

class SmBSplineSurface;
class SmContext;
class SmExtent2d;
class SmSurface;

namespace step_import::detail
{
struct ModelData;
// Creates an unpublished, independently owned restriction with the ORIGINAL
// native parameters. Output must be empty; failure leaves it empty. The caller
// still proves whole-domain equivalence and validates the final BRep/UV.
// Supports ordinary B-splines, cylinders and cones; other classes are refused.
// Every allocation/native work boundary invokes checkpoint; its exceptions
// unwind the private candidate before its context. No tolerance is enlarged.
SmStatus RestrictPeriodicSupport(
    const SmBSplineSurface& source,
    const SmContext& context,
    SmExtent2d& domain,
    std::unique_ptr<SmSurface>& output,
    const std::function<void()>& checkpoint
);
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
