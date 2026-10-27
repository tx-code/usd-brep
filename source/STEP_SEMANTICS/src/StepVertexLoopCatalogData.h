// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::vertex_loop
{
inline constexpr std::string_view LoopKinds[] = { "EDGE_LOOP", "VERTEX_LOOP" };
constexpr void AppendDeclarations(Recipe& recipe)
{
    // AP214E3_2010.exp (71ab140f...), VERTEX_LOOP and LOOP's ONEOF. Append
    // after all existing identities. A point boundary has no PATH.edge_list;
    // admitting it does not establish support membership or a native region.
    recipe.declarations.push_back({ "VERTEX_LOOP", { "LOOP" }, { { "loop_vertex", "VERTEX", FieldKind::Reference } }, true });
}
} // namespace step_semantics::catalog_build::vertex_loop
