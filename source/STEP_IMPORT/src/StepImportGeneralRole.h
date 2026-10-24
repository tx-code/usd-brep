// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportExecutionCallbacks.h"

#include <span>
#include <utility>

namespace step_import::detail
{
struct ModelData;

enum class GeneralBodyRole
{
    MaterialSolid,
    ClosedSurfaceModel,
    OpenSurfaceModel
};

constexpr bool IsMaterial(GeneralBodyRole role)
{
    return role == GeneralBodyRole::MaterialSolid;
}
constexpr bool IsClosedBoundary(GeneralBodyRole role)
{
    return role != GeneralBodyRole::OpenSurfaceModel;
}
constexpr std::string_view GeneralRoleName(GeneralBodyRole role)
{
    switch (role)
    {
        case GeneralBodyRole::MaterialSolid:
            return "MaterialSolid";
        case GeneralBodyRole::ClosedSurfaceModel:
            return "ClosedSurfaceModel";
        case GeneralBodyRole::OpenSurfaceModel:
            return "OpenSurfaceModel";
    }
    return {};
}

struct GeneralRoleFace
{
    std::size_t record;
    bool same;
};
struct GeneralRoleEdge
{
    std::size_t record;
    // One entry per source logical occurrence, including repeated same-face
    // seams: source face ordinal and surface-normal SAME/upward direction.
    std::vector<std::pair<std::size_t, bool>> uses;
};

// Verify an unpublished candidate after ValidatePointers/LEVEL2. This module
// checks role, complete native inventory and incidence correspondence; it does
// not replace geometric checks or prove normalization interval coverage. Faces
// use the effective same-sense used by construction, including authorized edits.
// Without normalization, model.edges starts with the source-edge ordinal prefix;
// any appended edges are derived double-use seams. derivedEdgeFaces records
// the source face record index (not face ordinal) for
// each unnormalized appended edge, in the same order as that native edge tail.
// With normalization, only its
// owning source IDs and targetFaceSources identify target origins, never indices
// left in the original source arrays. Ring/pole evidence may still be pending.
// NativeStep failures/exceptions propagate. Report role/adjacency change only
// after the whole check succeeds. No topology, geometry or source is modified.
[[nodiscard]] Result<void> VerifyGeneralRole(
    const ModelData& model,
    GeneralBodyRole role,
    std::span<const GeneralRoleFace> sourceFaces,
    std::span<const GeneralRoleEdge> sourceEdges,
    std::span<const std::size_t> derivedEdgeFaces,
    Report& report,
    const NativeStep& before
);
} // namespace step_import::detail
