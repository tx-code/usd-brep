// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
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
};
Memory Sample()
{
    Memory result;
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
    return result;
}
void Json(const Memory& memory)
{
    std::cout << "{\"heap_busy_bytes\":" << memory.bytes << ",\"heap_busy_blocks\":" << memory.blocks << ",\"private_bytes\":" << memory.privateBytes
              << '}';
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Pass F1 input");
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
            switch (i % 4)
            {
                case 0:
                    options.nativeLimits.nativeCalls = (i / 4) % calls;
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
            const auto expected = i % 4 == 2 ? step_import::Status::Indeterminate :
                                  i % 4 == 3 ? step_import::Status::Accepted :
                                               step_import::Status::ResourceLimit;
            Require(result.status == expected && bool(result.model) == (i % 4 == 3), "Resource job status");
        };
        // Warm every native-call boundary and all four outcomes before measuring retained allocations.
        for (std::size_t i = 0; i < 8 * calls; ++i)
        {
            job(i);
        }
        std::array<Memory, 5> samples;
        samples[0] = Sample();
        for (std::size_t batch = 1; batch < samples.size(); ++batch)
        {
            for (std::size_t i = 0; i < 4 * calls; ++i)
            {
                job(i);
            }
            samples[batch] = Sample();
        }
        std::array<std::shared_ptr<const step_import::ModelSnapshot>, 64> held;
        const auto before = Sample();
        for (auto& model : held)
        {
            model = step_import::ImportBytes(bytes, step_import::Options::PlanarFixture()).model;
            Require(bool(model), "Retained model control");
        }
        const auto allocated = Sample();
        for (auto& model : held)
        {
            model.reset();
        }
        const auto released = Sample();
        // A positive retention control must be visible to the same observer used for failures.
        Require(allocated.bytes > before.bytes + 65536 && allocated.blocks > before.blocks + 256, "Observer detects retained models");
        for (const auto& memory : samples)
        {
            Require(
                memory.bytes <= samples[0].bytes + 4096 && memory.blocks <= samples[0].blocks + 8,
                "Repeated ordinary failures or success retain allocations"
            );
        }
        Require(released.bytes <= before.bytes + 4096 && released.blocks <= before.blocks + 8, "Control releases allocations");
        std::cout << "{\"measured_imports\":" << 16 * calls << ",\"native_boundaries\":" << calls << ",\"samples\":[";
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
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
