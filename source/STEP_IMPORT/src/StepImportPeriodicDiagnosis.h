// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <span>

class SmFaceProps;
class SmVertex;

namespace step_import::detail
{
// Read-only, bounded observation of the already computed native classification.
// True means every U near-miss is attached to a retained source vertex; it does
// not establish a source error, authorize projection, or qualify an import.
// Mixed/unknown/incomplete/oversized observations stay unclassified.
bool IsPeriodicSourceVertexNearMiss(const SmFaceProps& properties, std::span<SmVertex* const> sourceVertices);
} // namespace step_import::detail
