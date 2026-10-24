// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace step_import
{
enum class SurfaceCurveDeclaredMaster
{
    PcurveS1,
    PcurveS2
};
struct SurfaceCurve3dInterpretationUse
{
    // Original entity labels, without '#'. Match the complete source scope,
    // never a path, native ordinal, occurrence transform or inferred name.
    std::string definitionId, shapeId, edgeId, surfaceCurveId, curve3dId;
    SurfaceCurveDeclaredMaster expectedMaster = SurfaceCurveDeclaredMaster::PcurveS1;
    std::vector<std::string> pcurveIds; // Exact associated_geometry LIST, including repeated identities and order.
};
struct SurfaceCurve3dInterpretationRequest
{
    std::string sourceSha256;
    std::size_t maximumUses = 0; // Explicit discrete scope budget; zero grants no interpretation.
    std::vector<SurfaceCurve3dInterpretationUse> uses;
};
struct SurfaceCurve3dInterpretationConflict
{
    std::string pcurveId;
    std::size_t associationOccurrence = 0; // 1-based original LIST position.
    // Preserve the original refusal, even though this explicit interpretation
    // does not use the auxiliary pcurve as a defining boundary. No native ptrs.
    std::string status, key, recordId, stage, message;
};
struct SurfaceCurve3dInterpretationAction
{
    std::size_t planIndex = 0;
    std::array<double, 2> nativeEdgeInterval{}; // Increasing original 3D basis interval; not the pcurve's t.
    std::vector<SurfaceCurve3dInterpretationConflict> originalConflicts;
    bool nativeChecksPassed = false;
};
struct SurfaceCurve3dInterpretationReceipt
{
    std::string sourceSha256, qualityId;
    std::size_t maximumUses = 0;
    std::vector<SurfaceCurve3dInterpretationUse> plans;
    std::vector<SurfaceCurve3dInterpretationAction> actions;
    // Exact numerator/positive denominator, in mm. These remain the selected
    // target budgets; the original auxiliary-consistency requirement is NOT
    // qualified under this interpretation, and source uncertainty selects none.
    std::array<std::int64_t, 2> consistencyBudgetMm{}, derivedUvBudgetMm{}, maximumWorkToleranceMm{};
    bool requestComplete = false;
};
} // namespace step_import
