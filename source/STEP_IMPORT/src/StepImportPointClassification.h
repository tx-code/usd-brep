// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <SmTypes.h>
#include <array>
#include <functional>

class SmBrep;
class SmPoint3d;
class SmPointClassification;
class SmRegion;
namespace step_import::detail
{
enum class RegionRayContact
{
    Failed,
    ClearMiss,
    Face,
    Edge,
    Vertex
};
struct RegionRayResult
{
    RegionRayContact contact = RegionRayContact::Failed;
    SmRegion* region = nullptr; // Borrowed from the caller's locked BRep.
};
using RegionRayCaster = std::function<RegionRayResult(const std::array<double, 3>&)>;
// A ray that hits an edge/vertex can graze a boundary. Its region is not
// accepted; bounded retries use a face hit or a successful clear miss.
// Null means no unambiguous evidence, never an invented outside region.
SmRegion* RegionFromStableRays(const RegionRayCaster& castRay);
// Existing point/boundary intersection semantics, followed by stable rays.
// Caller serializes the native graph/context and keeps output/model alive.
SmStatus ClassifySnapshotPoint(const SmBrep& brep, const SmPoint3d& point, double tolerance, SmPointClassification& output);
} // namespace step_import::detail
