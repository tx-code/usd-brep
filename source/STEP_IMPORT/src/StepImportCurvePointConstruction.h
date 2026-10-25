// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportCurvePointDefinitions.h"
#include "StepImportResult.h"

namespace step_import::detail
{
// Shared native type scope only. Detailed trims, units, numerical evidence and
// publication still belong to construction; a true result is not acceptance.
// Registered-type guards keep an absent catalog branch from becoming a query
// error, and prevent independent builders from drifting to different lists.
bool SupportsDetachedCurve(const step_semantics::Snapshot& source, std::size_t record);
// One request's construction module. Append performs source-identity reuse,
// owning registration, parameter correspondence and role-specific evidence.
// Callers retain the original per-item execution checkpoint and source order;
// every native Step, including a bbox for each repeated wireframe use, stays
// inside the original operation. Finish only moves ownership, never publishes.
class CurvePointDefinitions
{
public:

    explicit CurvePointDefinitions(const construction::GeometryConstruction& inputs);
    ~CurvePointDefinitions();
    CurvePointDefinitions(const CurvePointDefinitions&) = delete;
    CurvePointDefinitions& operator=(const CurvePointDefinitions&) = delete;

    [[nodiscard]] Result<void> Append(SourceRecord record, WireframeInfo& group, WireframeEvidence& evidence);
    // Classify in the original point -> placement -> curve order. Only a known
    // plane/frame returns ExistingPlacement, whose unchanged handler the caller
    // must execute immediately; unsupported required content is never skipped.
    [[nodiscard]] Result<SupportingAppendResult> Append(SourceRecord record, SupportingGeometryInfo& group, SupportingEvidence& evidence);
    [[nodiscard]] OwnedCurvePointDefinitions Finish() &&;

private:

    class Implementation;
    std::unique_ptr<Implementation> m_implementation;
};

} // namespace step_import::detail
