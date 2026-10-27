// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportResourceObserver.h"

#include <atomic>
#include <cstring>
#include <limits>
#include <windows.h>

namespace step_import_test
{
namespace
{
using Allocate = LPVOID(WINAPI*)(HANDLE, DWORD, SIZE_T);
using Reallocate = LPVOID(WINAPI*)(HANDLE, DWORD, LPVOID, SIZE_T);
using Free = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID);
std::atomic<Allocate> originalAllocate{};
std::atomic<Reallocate> originalReallocate{};
std::atomic<Free> originalFree{};
std::atomic<bool> active{};
std::atomic<DWORD> owner{};
SRWLOCK lock = SRWLOCK_INIT;
std::atomic_flag changingImports = ATOMIC_FLAG_INIT;
struct ImportChange
{
    bool acquired = !changingImports.test_and_set();
    ~ImportChange()
    {
        if (acquired)
        {
            changingImports.clear();
        }
    }
};

struct Slot
{
    std::uintptr_t address;
    std::size_t bytes;
};
// Static storage must not allocate through the observed CRT. Exhaustion is an
// observer failure, never an excuse to drop a request or pass a resource test.
Slot slots[ResourceObserver::MaximumCapacity]{};
std::size_t capacity = 0;
CrtAllocations counts;
const char* fault = nullptr;
bool installationBroken = false;
constexpr std::uintptr_t tombstone = 1;

std::size_t Hash(std::uintptr_t address)
{
    return ((address >> 4) * std::uintptr_t(0x9e3779b1U)) & (capacity - 1);
}
Slot* Find(void* pointer)
{
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto start = Hash(address);
    for (std::size_t n = 0; n < capacity; ++n)
    {
        auto& slot = slots[(start + n) & (capacity - 1)];
        if (!slot.address)
        {
            return nullptr;
        }
        if (slot.address == address)
        {
            return &slot;
        }
    }
    return nullptr;
}
void Erase(Slot& slot)
{
    counts.bytes -= slot.bytes;
    --counts.blocks;
    slot = { tombstone, 0 };
}
void Insert(void* pointer, std::size_t bytes)
{
    if (counts.bytes > std::numeric_limits<std::uint64_t>::max() - bytes)
    {
        fault = "CRT observer byte counter overflow";
        return;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto start = Hash(address);
    Slot* available = nullptr;
    for (std::size_t n = 0; n < capacity; ++n)
    {
        auto& slot = slots[(start + n) & (capacity - 1)];
        if (slot.address == address)
        {
            fault = "CRT observer duplicate live address";
            return;
        }
        if (!available && slot.address <= tombstone)
        {
            available = &slot;
        }
        if (!slot.address)
        {
            break;
        }
    }
    if (!available)
    {
        fault = "CRT observer live-request capacity exhausted";
        return;
    }
    *available = { address, bytes };
    counts.bytes += bytes;
    ++counts.blocks;
}

LPVOID WINAPI ObserveAllocate(HANDLE heap, DWORD flags, SIZE_T bytes)
{
    auto* pointer = originalAllocate.load()(heap, flags, bytes);
    const DWORD error = GetLastError();
    // The new pointer cannot be freed by its caller until this wrapper returns.
    if (pointer && active.load() && GetCurrentThreadId() == owner.load())
    {
        AcquireSRWLockExclusive(&lock);
        if (active.load() && GetCurrentThreadId() == owner.load())
        {
            Insert(pointer, bytes);
        }
        ReleaseSRWLockExclusive(&lock);
    }
    SetLastError(error);
    return pointer;
}
BOOL WINAPI ObserveFree(HANDLE heap, DWORD flags, LPVOID pointer)
{
    if (!active.load())
    {
        return originalFree.load()(heap, flags, pointer);
    }
    const DWORD incoming = GetLastError();
    AcquireSRWLockExclusive(&lock);
    SetLastError(incoming);
    const BOOL freed = originalFree.load()(heap, flags, pointer);
    const DWORD error = GetLastError();
    if (freed && active.load())
    {
        if (auto* slot = Find(pointer))
        {
            Erase(*slot);
        }
    }
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
    return freed;
}
LPVOID WINAPI ObserveReallocate(HANDLE heap, DWORD flags, LPVOID pointer, SIZE_T bytes)
{
    if (!active.load())
    {
        return originalReallocate.load()(heap, flags, pointer, bytes);
    }
    // The shared CRT uses nonthrowing heap requests. Preserve any unexpected SEH
    // request, but fail observation rather than holding a lock across its unwind.
    if (flags & HEAP_GENERATE_EXCEPTIONS)
    {
        const DWORD incoming = GetLastError();
        AcquireSRWLockExclusive(&lock);
        fault = "CRT observer cannot qualify a throwing heap request";
        ReleaseSRWLockExclusive(&lock);
        SetLastError(incoming);
        return originalReallocate.load()(heap, flags, pointer, bytes);
    }
    const DWORD incoming = GetLastError();
    AcquireSRWLockExclusive(&lock);
    const bool tracked = active.load() && Find(pointer);
    SetLastError(incoming);
    auto* resized = originalReallocate.load()(heap, flags, pointer, bytes);
    const DWORD error = GetLastError();
    if (resized && active.load())
    {
        if (tracked)
        {
            Erase(*Find(pointer));
        }
        if (tracked || GetCurrentThreadId() == owner.load())
        {
            Insert(resized, bytes);
        }
    }
    ReleaseSRWLockExclusive(&lock);
    SetLastError(error);
    return resized;
}

struct Patch
{
    void** address = nullptr;
    void* original = nullptr;
    void* replacement = nullptr;
    DWORD protection = 0;
    bool changed = false;
};
Patch patches[3];

bool Rewrite(Patch& patch, void* value)
{
    DWORD previous = 0;
    if (!VirtualProtect(patch.address, sizeof(void*), PAGE_READWRITE, &previous))
    {
        return false;
    }
    if (!patch.protection)
    {
        patch.protection = previous;
    }
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(patch.address), value);
    patch.changed = value != patch.original;
    DWORD ignored = 0;
    return VirtualProtect(patch.address, sizeof(void*), patch.protection, &ignored) != FALSE;
}
bool Restore()
{
    bool restored = true;
    for (auto& patch : patches)
    {
        if ((patch.changed || patch.protection) && !Rewrite(patch, patch.original))
        {
            restored = false;
        }
    }
    return restored;
}
tl::expected<void, const char*> LocateImports()
{
#ifdef _DEBUG
    const auto module = GetModuleHandleW(L"ucrtbased.dll");
#else
    const auto module = GetModuleHandleW(L"ucrtbase.dll");
#endif
    if (!module)
    {
        return tl::unexpected("Matching shared CRT is not loaded");
    }
    auto* base = reinterpret_cast<unsigned char*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return tl::unexpected("Invalid CRT image");
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC)
    {
        return tl::unexpected("Unsupported CRT image layout");
    }
    auto* imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    patches[0].replacement = reinterpret_cast<void*>(ObserveAllocate);
    patches[1].replacement = reinterpret_cast<void*>(ObserveReallocate);
    patches[2].replacement = reinterpret_cast<void*>(ObserveFree);
    const char* names[] = { "HeapAlloc", "HeapReAlloc", "HeapFree" };
    for (; imports->Name; ++imports)
    {
        if (!imports->OriginalFirstThunk)
        {
            continue;
        }
        auto* original = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->OriginalFirstThunk);
        auto* resolved = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imports->FirstThunk);
        for (; original->u1.AddressOfData; ++original, ++resolved)
        {
            if (IMAGE_SNAP_BY_ORDINAL(original->u1.Ordinal))
            {
                continue;
            }
            const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + original->u1.AddressOfData);
            for (unsigned i = 0; i < 3; ++i)
            {
                if (std::strcmp(imported->Name, names[i]) != 0)
                {
                    continue;
                }
                if (patches[i].address)
                {
                    return tl::unexpected("Ambiguous CRT heap import");
                }
                patches[i].address = reinterpret_cast<void**>(&resolved->u1.Function);
                patches[i].original = reinterpret_cast<void*>(resolved->u1.Function);
            }
        }
    }
    for (const auto& patch : patches)
    {
        if (!patch.address || !patch.original)
        {
            return tl::unexpected("Shared CRT heap imports are unavailable");
        }
    }
    originalAllocate.store(reinterpret_cast<Allocate>(patches[0].original));
    originalReallocate.store(reinterpret_cast<Reallocate>(patches[1].original));
    originalFree.store(reinterpret_cast<Free>(patches[2].original));
    return {};
}
} // namespace

ResourceObserver::~ResourceObserver()
{
    (void)Stop();
}
tl::expected<void, const char*> ResourceObserver::Start(std::size_t requestedCapacity)
{
    if (m_started || active.load())
    {
        return tl::unexpected("A CRT observer is already active");
    }
    if (requestedCapacity < 16 || requestedCapacity > MaximumCapacity || (requestedCapacity & (requestedCapacity - 1)))
    {
        return tl::unexpected("Invalid CRT observer capacity");
    }
    ImportChange change;
    if (!change.acquired)
    {
        return tl::unexpected("CRT observer imports are being changed");
    }
    if (active.load() || installationBroken)
    {
        return tl::unexpected("CRT observer is active or a previous installation failed");
    }
    for (auto& patch : patches)
    {
        patch = {};
    }
    auto located = LocateImports();
    if (!located)
    {
        return located;
    }
    for (auto& patch : patches)
    {
        if (!Rewrite(patch, patch.replacement))
        {
            installationBroken = !Restore();
            return tl::unexpected("Cannot install CRT observer imports");
        }
    }
    // Loader APIs and import-page edits stay outside the accounting lock:
    // a DLL thread callback may allocate while it holds the loader lock.
    AcquireSRWLockExclusive(&lock);
    capacity = requestedCapacity;
    std::memset(slots, 0, capacity * sizeof(Slot));
    counts = {};
    fault = nullptr;
    owner.store(GetCurrentThreadId());
    m_started = true;
    active.store(true);
    ReleaseSRWLockExclusive(&lock);
    return {};
}
tl::expected<CrtAllocations, const char*> ResourceObserver::Sample() const
{
    if (!m_started || !active.load())
    {
        return tl::unexpected("CRT observer is not active");
    }
    if (GetCurrentThreadId() != owner.load())
    {
        return tl::unexpected("CRT observer sampled from another thread");
    }
    AcquireSRWLockExclusive(&lock);
    const auto result = counts;
    const auto* error = fault;
    ReleaseSRWLockExclusive(&lock);
    if (error)
    {
        return tl::unexpected(error);
    }
    return result;
}
tl::expected<void, const char*> ResourceObserver::Stop()
{
    if (!m_started)
    {
        return {};
    }
    ImportChange change;
    if (!change.acquired)
    {
        return tl::unexpected("CRT observer imports are being changed");
    }
    AcquireSRWLockExclusive(&lock);
    active.store(false);
    ReleaseSRWLockExclusive(&lock);
    const bool restored = Restore();
    installationBroken = !restored;
    m_started = false;
    if (!restored)
    {
        return tl::unexpected("Cannot restore CRT observer imports");
    }
    return {};
}
} // namespace step_import_test
