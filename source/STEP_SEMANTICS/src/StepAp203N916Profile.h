// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string_view>

namespace step_semantics::ap203_n916
{
// Compared against fixed STEPcode87e1f9948c60837c2a1664cd86e00e3962a02ff5,
// data/ap203/ap203.exp (020b4d25...). These are borrowed N2635 diagnostic
// descriptors, NOT declarations in the modified N916 artifact. Registration
// never admits an instance; only an attached nonphysical layer has an explicit
// interoperability proof. Keep every other foreign type unsupported.
inline constexpr std::string_view ForeignDeclarations[] = { "DERIVED_UNIT",
                                                            "DERIVED_UNIT_ELEMENT",
                                                            "MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION",
                                                            "STYLED_ITEM",
                                                            "PRESENTATION_STYLE_ASSIGNMENT",
                                                            "SURFACE_STYLE_USAGE",
                                                            "SURFACE_SIDE_STYLE",
                                                            "SURFACE_STYLE_FILL_AREA",
                                                            "FILL_AREA_STYLE",
                                                            "FILL_AREA_STYLE_COLOUR",
                                                            "COLOUR",
                                                            "COLOUR_SPECIFICATION",
                                                            "COLOUR_RGB",
                                                            "MEASURE_REPRESENTATION_ITEM",
                                                            "DESCRIPTIVE_REPRESENTATION_ITEM",
                                                            "VALUE_REPRESENTATION_ITEM",
                                                            "PRESENTATION_LAYER_ASSIGNMENT",
                                                            "PRE_DEFINED_ITEM",
                                                            "PRE_DEFINED_COLOUR",
                                                            "DRAUGHTING_PRE_DEFINED_COLOUR" };
} // namespace step_semantics::ap203_n916
