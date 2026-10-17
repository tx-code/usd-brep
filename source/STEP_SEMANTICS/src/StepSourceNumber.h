// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string_view>

namespace step_semantics
{
// Internal, already lexically validated STEP numbers. No binary-float conversion,
// power expansion or source rewriting; the result compares their exact values.
int CompareSourceReals(std::string_view left, std::string_view right);
std::optional<std::size_t> SourceIndexAtMost(std::string_view integer, std::size_t maximum);
} // namespace step_semantics
