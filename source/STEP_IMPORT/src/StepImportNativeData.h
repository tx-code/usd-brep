// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

#include <SmBrep.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <mutex>

class SmEdge;
class SmFace;
class SmLine;
class SmLoop;
class SmPlane;
class SmRegion;
class SmShell;
class SmVertex;

namespace step_import::detail
{
std::recursive_mutex& NativeMutex();
struct NativeFaceData
{
    SmFace* face = nullptr;
    SmPlane* plane = nullptr; // Only the legacy planar verifier uses this narrower view.
    std::vector<SmLoop*> loops;
    std::string faceId;
    Statistics info;
};
struct ModelData
{
    ~ModelData(); // Native destruction is serialized; BRep must precede its allocating context.
    std::unique_ptr<SmContext> context;
    std::unique_ptr<SmBrep> brep;
    std::shared_ptr<const step_semantics::Snapshot> source;
    std::vector<NativeFaceData> faces;
    std::vector<SmVertex*> vertices;
    std::vector<SmEdge*> edges;
    std::vector<SmLine*> curves; // Legacy affine verifier only; native edges own all defining curves.
    std::vector<std::unique_ptr<SmCurve>> wireCurves; // Detached definitions; must be destroyed before context, under NativeMutex.
    std::vector<WireframePointInfo> wirePoints; // Coordinates do not imply source vertices or connection to the material BRep.
    std::vector<WireframeInfo> wireframes;
    SmRegion* material = nullptr;
    SmRegion* bounded = nullptr;
    SmShell* outside = nullptr;
    SmShell* inside = nullptr;
    double queryTolerance = 0;
    double definitionToleranceMm = 0; // General defining-geometry budget, also bounds linked archive readback.
    Fraction queryBudget;
    Statistics info;
    mutable std::mutex mutex;
};
void WriteWireframeArchive(const ModelData& model, const std::filesystem::path& path);
std::shared_ptr<ModelData> BuildGeneral(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
);
} // namespace step_import::detail
