// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepCatalogBuilder.h"

namespace step_semantics::catalog_build::ap214_construction
{
constexpr std::string_view ParametricGeometryContext[] = { "GEOMETRIC_REPRESENTATION_CONTEXT",
                                                           "PARAMETRIC_REPRESENTATION_CONTEXT",
                                                           "REPRESENTATION_CONTEXT" };
constexpr std::string_view ParametricContext[] = { "GEOMETRIC_REPRESENTATION_CONTEXT",
                                                   "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                                                   "PARAMETRIC_REPRESENTATION_CONTEXT",
                                                   "REPRESENTATION_CONTEXT" };
constexpr std::string_view ParametricUncertaintyContext[] = { "GEOMETRIC_REPRESENTATION_CONTEXT",
                                                              "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT",
                                                              "GLOBAL_UNIT_ASSIGNED_CONTEXT",
                                                              "PARAMETRIC_REPRESENTATION_CONTEXT",
                                                              "REPRESENTATION_CONTEXT" };
constexpr void AppendDeclarations(Recipe& recipe)
{
    // Pinned AP214E3_2010.exp (71ab140f...), CGR/CGRR: inherited
    // REPRESENTATION's three / REPRESENTATION_RELATIONSHIP's four explicit
    // slots. Endpoint restrictions are WHERE clauses in this artifact. Do not
    // borrow N2635's explicit redeclarations or register this in modified N916.
    recipe.declarations.insert(
        recipe.declarations.end(),
        {
            { "CONSTRUCTIVE_GEOMETRY_REPRESENTATION", { "REPRESENTATION" }, {}, true },
            { "CONSTRUCTIVE_GEOMETRY_REPRESENTATION_RELATIONSHIP", { "REPRESENTATION_RELATIONSHIP" }, {}, true },
            // The same fixed artifact's PARAMETRIC_REPRESENTATION_CONTEXT has
            // no local fields or WHERE clauses. It labels a context; it cannot
            // supply units, change dimension or authorize a parameter transform.
            { "PARAMETRIC_REPRESENTATION_CONTEXT", { "REPRESENTATION_CONTEXT" }, {}, true },
        }
    );
    recipe.complexSignatures.push_back(ParametricGeometryContext);
    recipe.complexSignatures.push_back(ParametricContext);
    recipe.complexSignatures.push_back(ParametricUncertaintyContext);
}
} // namespace step_semantics::catalog_build::ap214_construction
