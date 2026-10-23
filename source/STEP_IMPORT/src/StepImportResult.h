// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportFailure.h"

#include <tl/expected.hpp>
#include <type_traits>

namespace step_import::detail
{
// Private vocabulary: public outcomes and their diagnostic/ownership contracts
// do not acquire a third-party type or a different error classification.
template <class T>
using Result = tl::expected<T, Failure>;
static_assert(std::is_nothrow_move_constructible_v<Failure>);
} // namespace step_import::detail
