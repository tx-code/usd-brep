// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmTArray.h>
#include <SmVector3d.h>
#include <concepts>
#include <type_traits>
#include <vector>

static_assert(std::is_trivially_copyable_v<double>);
static_assert(std::is_trivially_copyable_v<SmEdge*>);
static_assert(std::is_standard_layout_v<SmVector3d>);
static_assert(!std::is_polymorphic_v<SmVector3d>);
static_assert(!std::is_trivially_copyable_v<SmVector3d>);
static_assert(sizeof(SmVector3d) == 3 * sizeof(double));
static_assert(std::is_polymorphic_v<SmTArray<double>>);
static_assert(!std::is_trivially_copyable_v<SmTArray<double>>);
static_assert(!std::is_polymorphic_v<std::vector<int>>);
static_assert(!std::is_trivially_copyable_v<std::vector<int>>);

// This proves expression availability only, not an equivalence relation,
// safe byte relocation, valid all-zero state, or ownership/lifetime semantics.
template <class T>
concept HasEqualityExpression = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
};
static_assert(HasEqualityExpression<SmVector3d>);
