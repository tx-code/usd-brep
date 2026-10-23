// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"
#include "StepImportExecution.h"
#include "StepImportResult.h"

#include <concepts>
#include <functional>
#include <gsl/assert>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace step_import::detail
{
struct DefinitionMember
{
    std::size_t ordinal, sourceDefinitionRecord;
    std::shared_ptr<const step_semantics::Snapshot> source;
};
namespace observation_impl
{
template <class T>
struct ResultTraits
{
    static constexpr bool isResult = false;
    using Value = void;
};
template <class T>
struct ResultTraits<Result<T>>
{
    static constexpr bool isResult = true;
    using Value = T;
};
template <class Operation, class Member>
concept MemberWork = std::invocable<Operation&, const Member&, Report&> &&
                     std::same_as<std::invoke_result_t<Operation&, const Member&, Report&>, Result<void>>;
template <class Operation>
concept CandidateWork = std::invocable<Operation&> && !std::is_reference_v<std::invoke_result_t<Operation&>> &&
                        ResultTraits<std::invoke_result_t<Operation&>>::isResult &&
                        !std::is_void_v<typename ResultTraits<std::invoke_result_t<Operation&>>::Value> &&
                        !std::is_reference_v<typename ResultTraits<std::invoke_result_t<Operation&>>::Value> &&
                        std::is_nothrow_move_constructible_v<typename ResultTraits<std::invoke_result_t<Operation&>>::Value>;

// Borrowed synchronous callbacks. Only the thin typed bridge is instantiated;
// report lifecycle policy remains in one translation unit, without std::function
// allocations or callbacks retained past the invocation.
template <class Member>
struct MemberOperation
{
    void* state;
    Result<void> (*invoke)(void*, const Member&, Report&);
};
struct CandidateOperation
{
    void* state;
    Result<void> (*invoke)(void*);
};
[[nodiscard]] Result<void> Bodies(
    const step_semantics::Snapshot& source,
    Report& parent,
    Execution& execution,
    MemberOperation<step_semantics::Part::Shape> member,
    CandidateOperation candidate
);
[[nodiscard]] Result<void> Definitions(
    const step_semantics::ProductDocument& source,
    std::span<const std::size_t> selected,
    Report& parent,
    Execution& execution,
    MemberOperation<DefinitionMember> member,
    CandidateOperation candidate
);
[[nodiscard]] Result<void> Definitions(
    const step_semantics::AssemblyDocument& source,
    std::span<const std::size_t> selected,
    Report& parent,
    Execution& execution,
    MemberOperation<DefinitionMember> member,
    CandidateOperation candidate
);

template <class Member, class Adopt, class Finish, class Drive>
auto Run(Adopt&& adopt, Finish&& finish, Drive&& drive)
{
    using Candidate = typename ResultTraits<std::invoke_result_t<Finish&>>::Value;
    struct State
    {
        Adopt& adopt;
        Finish& finish;
        std::optional<Candidate> candidate;
    } state{ adopt, finish, {} };
    auto completed = drive(
        MemberOperation<Member>{ &state,
                                 [](void* data, const Member& item, Report& checks) -> Result<void>
                                 {
                                     return std::invoke(static_cast<State*>(data)->adopt, item, checks);
                                 } },
        CandidateOperation{ &state,
                            [](void* data) -> Result<void>
                            {
                                auto& operation = *static_cast<State*>(data);
                                auto result = std::invoke(operation.finish);
                                if (!result)
                                {
                                    return tl::unexpected(std::move(result.error()));
                                }
                                operation.candidate.emplace(std::move(*result));
                                return {};
                            } }
    );
    if (!completed)
    {
        return Result<Candidate>(tl::unexpected(std::move(completed.error())));
    }
    Expects(state.candidate.has_value());
    return Result<Candidate>(std::move(*state.candidate));
}
} // namespace observation_impl

// adopt includes both building and inserting the member into its private
// candidate. Only its successful return establishes checksPassed. finish runs
// after integration aggregation, still inside the batch's failure protection.
template <class Adopt, class Finish>
    requires observation_impl::MemberWork<Adopt, step_semantics::Part::Shape> && observation_impl::CandidateWork<Finish>
auto ObserveBodies(const step_semantics::Snapshot& source, Report& parent, Execution& execution, Adopt&& adopt, Finish&& finish)
{
    return observation_impl::Run<step_semantics::Part::Shape>(
        std::forward<Adopt>(adopt),
        std::forward<Finish>(finish),
        [&](auto member, auto candidate)
        {
            return observation_impl::Bodies(source, parent, execution, member, candidate);
        }
    );
}
template <class Source, class Adopt, class Finish>
    requires observation_impl::MemberWork<Adopt, DefinitionMember> && observation_impl::CandidateWork<Finish>
auto ObserveDefinitions(
    const Source& source,
    std::span<const std::size_t> selected,
    Report& parent,
    Execution& execution,
    Adopt&& adopt,
    Finish&& finish
)
{
    return observation_impl::Run<DefinitionMember>(
        std::forward<Adopt>(adopt),
        std::forward<Finish>(finish),
        [&](auto member, auto candidate)
        {
            return observation_impl::Definitions(source, selected, parent, execution, member, candidate);
        }
    );
}
} // namespace step_import::detail
