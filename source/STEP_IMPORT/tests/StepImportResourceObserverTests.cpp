// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportResourceObserver.h"

#include <SmThreadLocalStorage.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <windows.h>

using step_import_test::CrtAllocations;
using step_import_test::ResourceObserver;
namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
template <class T>
T Checked(tl::expected<T, const char*> value)
{
    if (!value)
    {
        throw std::runtime_error(value.error());
    }
    return *value;
}
void Checked(tl::expected<void, const char*> value)
{
    if (!value)
    {
        throw std::runtime_error(value.error());
    }
}
bool WithinAllowance(CrtAllocations baseline, CrtAllocations current)
{
    return current.bytes <= baseline.bytes + 4096 && current.blocks <= baseline.blocks + 8;
}
CrtAllocations ProcessHeaps()
{
    HANDLE heaps[128];
    const auto count = GetProcessHeaps(128, heaps);
    Require(count > 0 && count <= 128, "heap enumeration");
    CrtAllocations result;
    for (DWORD i = 0; i < count; ++i)
    {
        Require(HeapLock(heaps[i]) != FALSE, "heap lock");
        PROCESS_HEAP_ENTRY entry{};
        while (HeapWalk(heaps[i], &entry))
        {
            if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
            {
                result.bytes += entry.cbData;
                ++result.blocks;
            }
        }
        const auto error = GetLastError();
        HeapUnlock(heaps[i]);
        Require(error == ERROR_NO_MORE_ITEMS, "complete heap enumeration");
    }
    return result;
}
struct Worker
{
    HANDLE ready, release;
    void* freeOnWorker = nullptr;
};
DWORD WINAPI WaitWithoutModeling(void* argument)
{
    const auto& worker = *static_cast<Worker*>(argument);
    std::free(worker.freeOnWorker);
    if (!SetEvent(worker.ready))
    {
        return 1;
    }
    return WaitForSingleObject(worker.release, 30000) == WAIT_OBJECT_0 ? 0 : 1;
}

void ForeignThreads(ResourceObserver& observer)
{
    // SMLib attaches its TLS even to threads which never call a geometry API.
    // Kernel/runtime thread storage is real memory, but is not a model leak.
    const auto baseline = Checked(observer.Sample());
    const auto wholeBaseline = ProcessHeaps();
    std::array<Worker, 16> workers{};
    std::array<HANDLE, 16> threads{}, ready{};
    for (std::size_t i = 0; i < workers.size(); ++i)
    {
        auto& worker = workers[i];
        worker.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        worker.release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Require(worker.ready && worker.release, "worker events");
        ready[i] = worker.ready;
        threads[i] = CreateThread(nullptr, 0, WaitWithoutModeling, &worker, 0, nullptr);
        Require(threads[i] != nullptr, "idle worker creation");
    }
    Require(WaitForMultipleObjects(DWORD(ready.size()), ready.data(), TRUE, 30000) == WAIT_OBJECT_0, "workers ready");
    const auto scopedHeld = Checked(observer.Sample());
    const auto wholeHeld = ProcessHeaps();
    for (auto& worker : workers)
    {
        SetEvent(worker.release);
    }
    Require(WaitForMultipleObjects(DWORD(threads.size()), threads.data(), TRUE, 30000) == WAIT_OBJECT_0, "workers joined");
    for (std::size_t i = 0; i < workers.size(); ++i)
    {
        DWORD status = 1;
        Require(GetExitCodeThread(threads[i], &status) && status == 0, "worker completes normally");
        CloseHandle(threads[i]);
        CloseHandle(workers[i].ready);
        CloseHandle(workers[i].release);
    }
    const auto released = Checked(observer.Sample());
    Require(!WithinAllowance(wholeBaseline, wholeHeld), "process-heap observer reproduces the original false retention finding");
    Require(
        scopedHeld.bytes == baseline.bytes && scopedHeld.blocks == baseline.blocks && released.bytes == baseline.bytes &&
            released.blocks == baseline.blocks,
        "unrelated worker storage is excluded without widening the retention allowance"
    );
    std::cout << "idle worker process growth=" << wholeHeld.bytes - wholeBaseline.bytes << '/' << wholeHeld.blocks - wholeBaseline.blocks
              << "; import-thread growth=0/0\n";
}

void RetainedRequests(ResourceObserver& observer)
{
    const auto baseline = Checked(observer.Sample());
    void* block = std::malloc(4097);
    Require(block != nullptr, "controlled CRT allocation");
    const auto held = Checked(observer.Sample());
    Require(!WithinAllowance(baseline, held), "a 4097-byte retained allocation still fails the 4096-byte allowance");
    void* resized = std::realloc(block, 16384);
    Require(resized != nullptr, "controlled CRT reallocation");
    const auto grown = Checked(observer.Sample());
    Require(grown.bytes >= baseline.bytes + 16384 && grown.blocks == held.blocks, "reallocation keeps one live request and its new size");
    std::free(resized);
    const auto released = Checked(observer.Sample());
    Require(released.bytes == baseline.bytes && released.blocks == baseline.blocks, "free returns exactly to baseline");
    std::array<void*, 9> tinyRequests{};
    for (auto& pointer : tinyRequests)
    {
        pointer = std::calloc(1, 1);
        Require(pointer != nullptr, "small controlled allocation");
    }
    const auto many = Checked(observer.Sample());
    Require(!WithinAllowance(baseline, many) && many.blocks == baseline.blocks + 9, "nine retained blocks still fail the eight-block allowance");
    for (auto pointer : tinyRequests)
    {
        std::free(pointer);
    }
    Require(Checked(observer.Sample()).blocks == baseline.blocks, "all small allocations released");
}

void CrossThreadFree(ResourceObserver& observer)
{
    const auto baseline = Checked(observer.Sample());
    Worker worker{ CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr), std::malloc(8192) };
    Require(worker.ready && worker.release && worker.freeOnWorker, "cross-thread free setup");
    Require(!WithinAllowance(baseline, Checked(observer.Sample())), "the main-thread allocation is initially visible");
    const auto thread = CreateThread(nullptr, 0, WaitWithoutModeling, &worker, 0, nullptr);
    Require(thread && WaitForSingleObject(worker.ready, 30000) == WAIT_OBJECT_0, "worker freed the tracked allocation");
    const auto freed = Checked(observer.Sample());
    SetEvent(worker.release);
    Require(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0, "free worker joined");
    CloseHandle(thread);
    CloseHandle(worker.ready);
    CloseHandle(worker.release);
    Require(freed.bytes == baseline.bytes && freed.blocks == baseline.blocks, "a different thread can release a tracked request");
}

DWORD WINAPI ResizeWithoutModeling(void* argument)
{
    auto& worker = *static_cast<Worker*>(argument);
    auto* resized = std::realloc(worker.freeOnWorker, 16384);
    if (!resized)
    {
        SetEvent(worker.ready);
        return 1;
    }
    worker.freeOnWorker = resized;
    if (!SetEvent(worker.ready))
    {
        return 1;
    }
    return WaitForSingleObject(worker.release, 30000) == WAIT_OBJECT_0 ? 0 : 1;
}
void CrossThreadResize(ResourceObserver& observer)
{
    const auto baseline = Checked(observer.Sample());
    Worker worker{ CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr), std::malloc(8192) };
    Require(worker.ready && worker.release && worker.freeOnWorker, "cross-thread resize setup");
    const auto thread = CreateThread(nullptr, 0, ResizeWithoutModeling, &worker, 0, nullptr);
    Require(thread && WaitForSingleObject(worker.ready, 30000) == WAIT_OBJECT_0, "worker resized the tracked allocation");
    const auto resized = Checked(observer.Sample());
    SetEvent(worker.release);
    Require(WaitForSingleObject(thread, 30000) == WAIT_OBJECT_0, "resize worker joined");
    DWORD status = 1;
    Require(GetExitCodeThread(thread, &status) && status == 0, "worker resize succeeded");
    CloseHandle(thread);
    CloseHandle(worker.ready);
    CloseHandle(worker.release);
    Require(
        resized.bytes >= baseline.bytes + 16384 && resized.blocks == baseline.blocks + 1,
        "worker reallocation retains the importing thread's original ownership"
    );
    std::free(worker.freeOnWorker);
    const auto released = Checked(observer.Sample());
    Require(released.bytes == baseline.bytes && released.blocks == baseline.blocks, "resized worker request is fully released");
}

void CapacityFailure()
{
    ResourceObserver observer;
    Checked(observer.Start(16));
    std::array<void*, 17> allocations{};
    for (auto& pointer : allocations)
    {
        pointer = std::malloc(1);
        Require(pointer != nullptr, "capacity control allocation");
    }
    Require(!observer.Sample(), "an exhausted observer fails closed instead of dropping allocations");
    for (auto pointer : allocations)
    {
        std::free(pointer);
    }
    Checked(observer.Stop());
    Checked(observer.Start());
    Require(Checked(observer.Sample()).blocks == 0, "a stopped observer can restart with a clean ledger");
    Checked(observer.Stop());
}
} // namespace

int main()
{
    try
    {
        (void)SmThreadLocalStorage::GetSERDepth(); // Load/initialize the real SDK before choosing a measurement baseline.
        ResourceObserver observer;
        Checked(observer.Start());
        ResourceObserver duplicate;
        Require(!duplicate.Start(), "a second observer cannot replace the active ledger");
        RetainedRequests(observer);
        CrossThreadFree(observer);
        CrossThreadResize(observer);
        ForeignThreads(observer);
        Checked(observer.Stop());
        Require(!observer.Sample(), "sampling after restoration fails explicitly");
        CapacityFailure();
        std::cout << "scoped shared-CRT retention contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
