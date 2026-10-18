// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Single-threaded acceptance probe. Only this executable replaces allocation;
// counters record successful requests, not live or peak memory. Allocator/free
// pairs and alignment must match even when a measured window is disabled.
#include "StepCatalog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#if defined(_WIN32)
#    include <malloc.h>
#endif
#include <new>

namespace measured
{
bool enabled = false;
std::size_t calls = 0, bytes = 0;
void record(std::size_t n) noexcept
{
    if (enabled)
    {
        ++calls;
        bytes += n;
    }
}
struct Count
{
    std::size_t calls, bytes;
};
void begin() noexcept
{
    calls = 0;
    bytes = 0;
    enabled = true;
}
Count end() noexcept
{
    enabled = false;
    return { calls, bytes };
}
} // namespace measured
void* operator new(std::size_t n)
{
    const std::size_t requested = n;
    if (n == 0)
    {
        n = 1;
    }
    if (void* p = std::malloc(n))
    {
        measured::record(requested);
        return p;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void* operator new(std::size_t n, std::align_val_t a)
{
    const auto requested = n;
    if (n == 0)
    {
        n = 1;
    }
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(n, static_cast<std::size_t>(a));
#else
    if (posix_memalign(&p, static_cast<std::size_t>(a), n) != 0)
    {
        p = nullptr;
    }
#endif
    if (p)
    {
        measured::record(requested);
        return p;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n, std::align_val_t a)
{
    return ::operator new(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept
{
    ::operator delete(p, a);
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(n);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept
{
    return ::operator new(n, std::nothrow);
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(n, a);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept
{
    return ::operator new(n, a, std::nothrow);
}
void operator delete(void* p, const std::nothrow_t&) noexcept
{
    ::operator delete(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept
{
    ::operator delete[](p);
}
void operator delete(void* p, std::align_val_t a, const std::nothrow_t&) noexcept
{
    ::operator delete(p, a);
}
void operator delete[](void* p, std::align_val_t a, const std::nothrow_t&) noexcept
{
    ::operator delete[](p, a);
}

void VerifyCounters()
{
    // Explicit allocation-function calls avoid new-expression allocation elision.
    // Each success must count exactly once with the caller's requested byte size.
    for (int mode = 0; mode < 8; ++mode)
    {
        measured::begin();
        void* p = nullptr;
        const auto aligned = std::align_val_t(64);
        switch (mode)
        {
            case 0:
                p = ::operator new(17);
                break;
            case 1:
                p = ::operator new[](17);
                break;
            case 2:
                p = ::operator new(17, std::nothrow);
                break;
            case 3:
                p = ::operator new[](17, std::nothrow);
                break;
            case 4:
                p = ::operator new(17, aligned);
                break;
            case 5:
                p = ::operator new[](17, aligned);
                break;
            case 6:
                p = ::operator new(17, aligned, std::nothrow);
                break;
            case 7:
                p = ::operator new[](17, aligned, std::nothrow);
                break;
        }
        const auto count = measured::end();
        if (!p || count.calls != 1 || count.bytes != 17)
        {
            std::abort();
        }
        if (mode >= 4)
        {
            if (mode % 2)
            {
                ::operator delete[](p, aligned);
            }
            else
            {
                ::operator delete(p, aligned);
            }
        }
        else
        {
            if (mode % 2)
            {
                ::operator delete[](p);
            }
            else
            {
                ::operator delete(p);
            }
        }
    }
}

volatile std::size_t observed = 0;
int main(int argc, char** argv)
{
    using namespace step_semantics;
    if (argc != 2)
    {
        std::fprintf(stderr, "one profile index required\n");
        return 2;
    }
    if (argv[1][0] < '0' || argv[1][0] > '3' || argv[1][1] != '\0')
    {
        return 2;
    }
    const int index = argv[1][0] - '0';
    constexpr ProfileKind kinds[] = { ProfileKind::Ap203e2Planar,
                                      ProfileKind::Ap203e2GeneralSource,
                                      ProfileKind::Ap214e3Source,
                                      ProfileKind::Ap214MbxSource };
    constexpr const char* names[] = { "Ap203e2Planar", "Ap203e2GeneralSource", "Ap214e3Source", "Ap214MbxSource" };
    if (index < 0 || index >= 4)
    {
        return 2;
    }
    // Complete all harness/runtime setup and flush output before opening the measured window.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::fflush(nullptr);
    VerifyCounters();
    measured::begin();
    const Catalog& catalog = Schema(kinds[index]);
    const auto first = measured::end();
    measured::begin();
    for (std::size_t i = 0; i < 1000; ++i)
    {
        const Catalog& repeated = Schema(kinds[index]);
        if (&repeated != &catalog)
        {
            std::abort();
        }
        observed = repeated.names.size();
    }
    const auto repeat = measured::end();
    std::size_t fields = 0, flattened = 0, nestedFields = 0, dimensions = 0, parents = 0, signatureFacets = 0, selectBranches = 0, enumLiterals = 0;
    for (const auto& declaration : catalog.declarations)
    {
        fields += declaration.fields.size();
        parents += declaration.parents.size();
        for (const auto& field : declaration.fields)
        {
            nestedFields += !field.nestedAggregates.empty();
            dimensions += field.nestedAggregates.size();
        }
    }
    for (const auto& row : catalog.flattened)
    {
        flattened += row.size();
    }
    for (const auto signature : catalog.complexSignatures)
    {
        signatureFacets += signature.size();
    }
    for (const auto& select : catalog.referenceSelects)
    {
        selectBranches += select.branches.size();
    }
    for (const auto& enumeration : catalog.enumerations)
    {
        enumLiterals += enumeration.branches.size();
    }
    const std::string* shortest = &catalog.names.front();
    const std::string* longest = shortest;
    for (const auto& name : catalog.names)
    {
        if (name.size() < shortest->size())
        {
            shortest = &name;
        }
        if (name.size() > longest->size())
        {
            longest = &name;
        }
    }
    const auto shortId = catalog.Find(*shortest);
    const auto longId = catalog.Find(*longest);
    if (!shortId || !longId)
    {
        std::abort();
    }
    measured::begin();
    for (std::size_t i = 0; i < 1000; ++i)
    {
        const auto found = catalog.Find(*shortest);
        if (found != shortId)
        {
            std::abort();
        }
        observed = *found;
    }
    const auto shortFind = measured::end();
    measured::begin();
    for (std::size_t i = 0; i < 1000; ++i)
    {
        const auto found = catalog.Find(*longest);
        if (found != longId)
        {
            std::abort();
        }
        observed = *found;
    }
    const auto longFind = measured::end();
    constexpr std::string_view unknown = "STEP_CATALOG_UNREGISTERED_TYPE_FOR_ALLOCATION_CONTROL";
    measured::begin();
    for (std::size_t i = 0; i < 1000; ++i)
    {
        if (catalog.Find(unknown))
        {
            std::abort();
        }
    }
    const auto unknownFind = measured::end();
    std::printf(
        "{\"profile\":\"%s\",\"types\":%zu,\"parents\":%zu,\"local_fields\":%zu,\"flattened_bindings\":%zu,\"nested_fields\":%zu,\"nested_dimensions\":%zu,\"complex_signatures\":%zu,\"signature_facets\":%zu,\"selects\":%zu,\"select_branches\":%zu,\"enumerations\":%zu,\"enum_literals\":%zu,\"derived_slots\":%zu,\"measure_branches\":%zu,",
        names[index],
        catalog.names.size(),
        parents,
        fields,
        flattened,
        nestedFields,
        dimensions,
        catalog.complexSignatures.size(),
        signatureFacets,
        catalog.referenceSelects.size(),
        selectBranches,
        catalog.enumerations.size(),
        enumLiterals,
        catalog.derivedSlots.size(),
        catalog.measureBranches.size()
    );
    std::printf(
        "\"first_schema\":{\"allocations\":%zu,\"requested_bytes\":%zu},\"repeat_schema_1000\":{\"allocations\":%zu,\"requested_bytes\":%zu},",
        first.calls,
        first.bytes,
        repeat.calls,
        repeat.bytes
    );
    std::printf(
        "\"short_find_1000\":{\"name\":\"%s\",\"length\":%zu,\"allocations\":%zu,\"requested_bytes\":%zu},\"long_find_1000\":{\"name\":\"%s\",\"length\":%zu,\"allocations\":%zu,\"requested_bytes\":%zu},",
        shortest->c_str(),
        shortest->size(),
        shortFind.calls,
        shortFind.bytes,
        longest->c_str(),
        longest->size(),
        longFind.calls,
        longFind.bytes
    );
    std::printf(
        "\"unknown_find_1000\":{\"allocations\":%zu,\"requested_bytes\":%zu},\"allocation_controls\":{\"paths\":8,\"allocations\":8,\"requested_bytes\":136}}\n",
        unknownFind.calls,
        unknownFind.bytes
    );
}
