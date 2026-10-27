// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportResourceObserver.h"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#ifdef _DEBUG
#    include <crtdbg.h>
#endif
// clang-format off: psapi.h requires Windows types before its declarations.
#include <windows.h>
#include <psapi.h>
// clang-format on

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
struct Memory
{
    unsigned long long bytes = 0, blocks = 0, privateBytes = 0;
    unsigned long long tick = 0, crtBytes = 0, crtBlocks = 0;
    unsigned long long processLifetimePeakCommitBytes = 0;
    unsigned long long importThreadBytes = 0, importThreadBlocks = 0;
    // Process heaps and even global debug-CRT totals include unrelated thread
    // storage. Gate this serial import on its owning thread's shared-CRT requests;
    // retain the other measurements as diagnostics and calibrate with real models.
    unsigned long long RetainedBytes() const
    {
        return importThreadBytes;
    }
    unsigned long long RetainedBlocks() const
    {
        return importThreadBlocks;
    }
};
Memory Sample(const step_import_test::ResourceObserver& observer)
{
    Memory result;
    result.tick = GetTickCount64();
#ifdef _DEBUG
    _CrtMemState state{};
    _CrtMemCheckpoint(&state);
    result.crtBytes = state.lSizes[_NORMAL_BLOCK] + state.lSizes[_CLIENT_BLOCK];
    result.crtBlocks = state.lCounts[_NORMAL_BLOCK] + state.lCounts[_CLIENT_BLOCK];
#endif
    HANDLE heaps[128];
    const DWORD count = GetProcessHeaps(128, heaps);
    Require(count > 0 && count <= 128, "Heap enumeration");
    for (DWORD i = 0; i < count; ++i)
    {
        Require(HeapLock(heaps[i]) != FALSE, "Heap lock");
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
        Require(error == ERROR_NO_MORE_ITEMS, "Complete heap walk");
    }
    PROCESS_MEMORY_COUNTERS_EX process{};
    Require(
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process), sizeof(process)) != FALSE,
        "Private usage measurement"
    );
    result.privateBytes = process.PrivateUsage;
    result.processLifetimePeakCommitBytes = process.PeakPagefileUsage;
    const auto scoped = observer.Sample();
    Require(bool(scoped), scoped ? "" : scoped.error());
    result.importThreadBytes = scoped->bytes;
    result.importThreadBlocks = scoped->blocks;
    return result;
}
void Json(const Memory& memory)
{
    std::cout << "{\"heap_busy_bytes\":" << memory.bytes << ",\"heap_busy_blocks\":" << memory.blocks << ",\"private_bytes\":" << memory.privateBytes
              << ",\"tick\":" << memory.tick << ",\"crt_bytes\":" << memory.crtBytes << ",\"crt_blocks\":" << memory.crtBlocks
              << ",\"process_lifetime_peak_commit_bytes\":" << memory.processLifetimePeakCommitBytes
              << ",\"import_thread_crt_bytes\":" << memory.importThreadBytes << ",\"import_thread_crt_blocks\":" << memory.importThreadBlocks << '}';
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        step_import_test::ResourceObserver observer;
        const auto started = observer.Start();
        Require(bool(started), started ? "" : started.error());
        Require(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "lifecycle"), "Pass fixture input and optional lifecycle mode");
        const bool lifecycle = argc == 3;
        std::ifstream stream(argv[1], std::ios::binary);
        const std::string bytes{ std::istreambuf_iterator<char>(stream), {} };
        Require(!bytes.empty(), "Readable fixture");
        std::size_t calls = 0;
        {
            const auto result = step_import::ImportBytes(bytes, step_import::Options::PlanarFixture());
            Require(bool(result.model), "Native baseline");
            calls = result.report.nativeCalls;
        }
        const auto job = [&](std::size_t i)
        {
            auto options = step_import::Options::PlanarFixture();
            if (lifecycle && i % 4 == 0)
            {
                options.execution.control = std::make_shared<step_import::ImportControl>();
                const auto control = options.execution.control;
                options.execution.progress = [&, control](step_import::Progress progress)
                {
                    if (progress.phase == step_import::Phase::BeforeNativeCall && progress.nativeCalls == (i / 4) % calls)
                    {
                        control->RequestCancel();
                    }
                };
            }
            switch (i % 4)
            {
                case 0:
                    if (!lifecycle)
                    {
                        options.nativeLimits.nativeCalls = (i / 4) % calls;
                    }
                    break;
                case 1:
                    options.nativeLimits.reportBytes = 1;
                    break;
                case 2:
                    options.quality.id = "resource-tight-definition";
                    options.quality.definitionMm = step_import::Fraction(1, 1000000000000000LL);
                    break;
                default:
                    break;
            }
            const auto result = step_import::ImportBytes(bytes, options);
            const auto expected = lifecycle && i % 4 == 0 ? step_import::Status::Cancelled :
                                  i % 4 == 2              ? step_import::Status::Indeterminate :
                                  i % 4 == 3              ? step_import::Status::Accepted :
                                                            step_import::Status::ResourceLimit;
            Require(result.status == expected && bool(result.model) == (i % 4 == 3), "Resource job status");
            if (lifecycle && result.model)
            {
                auto copy = result.model->CopyForModeling();
                Require(copy.status == step_import::Status::Accepted && bool(copy.model), "Resource copy created");
                copy.model->Edit(
                    [](SmBrep&)
                    {
                    }
                );
            }
        };
        // Warm every native-call boundary and all four outcomes before measuring retained allocations.
        for (std::size_t i = 0; i < 8 * calls; ++i)
        {
            job(i);
        }
        std::array<Memory, 5> samples;
        samples[0] = Sample(observer);
        for (std::size_t batch = 1; batch < samples.size(); ++batch)
        {
            for (std::size_t i = 0; i < 4 * calls; ++i)
            {
                job(i);
            }
            samples[batch] = Sample(observer);
        }
        std::array<std::shared_ptr<const step_import::ModelSnapshot>, 64> held;
        const auto before = Sample(observer);
        for (auto& model : held)
        {
            model = step_import::ImportBytes(bytes, step_import::Options::PlanarFixture()).model;
            Require(bool(model), "Retained model control");
        }
        const auto allocated = Sample(observer);
        for (auto& model : held)
        {
            model.reset();
        }
        const auto released = Sample(observer);
        std::cout << "{\"retention_observer\":\"ImportThreadSharedCrtHeapRequests\",\"mode\":\""
                  << (lifecycle ? "CancellationAndEditableCopies" : "OrdinaryFailures") << "\",\"measured_imports\":" << 16 * calls
                  << ",\"native_boundaries\":" << calls << ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            if (i)
            {
                std::cout << ',';
            }
            Json(samples[i]);
        }
        std::cout << "],\"retention_control\":{\"before\":";
        Json(before);
        std::cout << ",\"held\":";
        Json(allocated);
        std::cout << ",\"released\":";
        Json(released);
        std::cout << "}}\n";
        std::cout.flush(); // Preserve completed observations even when a resource assertion fails.
        // A positive retention control must be visible to the same observer used for failures.
        Require(
            allocated.RetainedBytes() > before.RetainedBytes() + 65536 && allocated.RetainedBlocks() > before.RetainedBlocks() + 256,
            "Observer detects retained models"
        );
        for (const auto& memory : samples)
        {
            Require(
                memory.RetainedBytes() <= samples[0].RetainedBytes() + 4096 && memory.RetainedBlocks() <= samples[0].RetainedBlocks() + 8,
                "Repeated ordinary failures or success retain allocations"
            );
        }
        Require(
            released.RetainedBytes() <= before.RetainedBytes() + 4096 && released.RetainedBlocks() <= before.RetainedBlocks() + 8,
            "Control releases allocations"
        );
        const auto stopped = observer.Stop();
        Require(bool(stopped), stopped ? "" : stopped.error());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
