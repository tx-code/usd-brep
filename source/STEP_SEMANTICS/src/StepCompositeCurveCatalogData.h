// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::composite_curve
{
inline constexpr std::string_view Transitions[] = { "DISCONTINUOUS", "CONTINUOUS", "CONT_SAME_GRADIENT", "CONT_SAME_GRADIENT_SAME_CURVATURE" };
constexpr void AppendDeclarations(Recipe& recipe)
{
    // Compared ordinary transport in AP214E3_2010.exp (71ab140f...), N2635
    // (c68dd022...) and modified N916 (020b4d25...). LIST occurrences retain
    // order/repetition; derived closed/count and inverse uses have no P21 slot.
    // CCS is founded, not RI. No reparametrised/surface subtype is admitted.
    Field segments{ "segments", "COMPOSITE_CURVE_SEGMENT", FieldKind::Aggregate };
    segments.minimum = 1;
    recipe.declarations.insert(
        recipe.declarations.end(),
        {
            { "COMPOSITE_CURVE", { "BOUNDED_CURVE" }, { segments, { "self_intersect", "LOGICAL", FieldKind::Logical } }, true },
            { "COMPOSITE_CURVE_SEGMENT",
              { "FOUNDED_ITEM" },
              { { "transition", "TRANSITION_CODE", FieldKind::Enumeration },
                { "same_sense", "BOOLEAN", FieldKind::Boolean },
                { "parent_curve", "CURVE", FieldKind::Reference } },
              true },
        }
    );
    recipe.enumerations.push_back({ "TRANSITION_CODE", Transitions });
}
} // namespace step_semantics::catalog_build::composite_curve
