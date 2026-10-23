// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImportResult.h"

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace step_import::detail
{
namespace legacy_failure
{
template <class T>
inline constexpr bool isExpected = false;
template <class T, class E>
inline constexpr bool isExpected<tl::expected<T, E>> = true;
} // namespace legacy_failure
// Transitional adapter around existing operations, outside the orientation
// repair state machine that consumes its own native-candidate witness. Only
// known import failures become values: allocation, configuration and other
// exceptions retain their existing outer contracts and classification.
template <class Operation>
    requires std::invocable<Operation&> && (!std::is_reference_v<std::invoke_result_t<Operation&>>) &&
             (!legacy_failure::isExpected<std::remove_cv_t<std::invoke_result_t<Operation&>>>)
auto LegacyFailureAdapter(Operation&& operation) -> Result<std::invoke_result_t<Operation&>>
{
    try
    {
        if constexpr (std::is_void_v<std::invoke_result_t<Operation&>>)
        {
            std::invoke(operation);
            return {};
        }
        else
        {
            return std::invoke(operation);
        }
    }
    catch (Failure& failure)
    {
        return tl::unexpected(std::move(failure));
    }
}
} // namespace step_import::detail
