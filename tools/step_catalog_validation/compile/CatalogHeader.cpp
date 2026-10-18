// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepCatalog.h"

#include <type_traits>

// A real-header compile control, not a claim that the current runtime catalog
// constructor or future production validator has become constant-evaluable.
static_assert(step_semantics::TypeMembership{}.size() == 256);
static_assert(std::is_same_v<decltype(step_semantics::Field{}.name), std::string_view>);
static_assert(std::is_same_v<decltype(step_semantics::AttributeView{}.nestedAggregates), std::span<const step_semantics::AggregateDimension>>);
static_assert(std::is_same_v<decltype(std::declval<const step_semantics::Snapshot&>().RegisteredTypes()), std::span<const std::string_view>>);
