// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <tl/expected.hpp>

namespace step_import_test
{
struct CrtAllocations
{
    std::uint64_t bytes = 0;
    std::uint64_t blocks = 0;
};

// Test-only Windows observer for the current serial import thread. It observes
// the shared CRT's underlying heap requests, including those made by SDK DLLs.
// A tracked request may be freed/reallocated by another thread. It does not
// qualify native work performed on worker threads, virtual allocations, or a
// different/static CRT. Process-wide observations remain separate diagnostics.
class ResourceObserver
{
public:

    static constexpr std::size_t MaximumCapacity = 1U << 20;
    ResourceObserver() = default;
    ResourceObserver(const ResourceObserver&) = delete;
    ResourceObserver& operator=(const ResourceObserver&) = delete;
    ~ResourceObserver();

    [[nodiscard]] tl::expected<void, const char*> Start(std::size_t capacity = MaximumCapacity);
    [[nodiscard]] tl::expected<CrtAllocations, const char*> Sample() const;
    [[nodiscard]] tl::expected<void, const char*> Stop();

private:

    bool m_started = false;
};
} // namespace step_import_test
