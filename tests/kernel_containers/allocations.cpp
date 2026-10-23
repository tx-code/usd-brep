// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmTArray.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>

namespace
{
std::size_t allocations = 0;
bool refuseNextAllocation = false;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
// Test-binary allocator boundary, not a production hook. SmTArray template
// instantiations in this executable use it; matching SDK allocation/free uses
// the same CRT. Inject the real allocation-error channel without exhausting
// host memory or driving the Debug null-allocation assertion path.
void* smos_Calloc(std::size_t count, std::size_t bytes)
{
    ++allocations;
    if (refuseNextAllocation)
    {
        refuseNextAllocation = false;
        throw std::bad_alloc();
    }
    void* memory = std::calloc(count, bytes);
    if (!memory)
    {
        throw std::bad_alloc();
    }
    return memory;
}
int main()
{
    try
    {
        SmTArray<double> owned;
        owned.Add(3);
        owned.Add(7);
        const auto data = owned.GetDataArray();
        allocations = 0;
        SmTArray<double> moved(std::move(owned));
        Require(allocations == 0 && moved.GetDataArray() == data && owned.GetSize() == 0, "owned move performs zero data-buffer allocations");
        double external[] = { 2, 4, 8 };
        SmTArray<double> borrowed(3, external, 3);
        allocations = 0;
        refuseNextAllocation = true;
        bool refused = false;
        try
        {
            SmTArray<double> failed(std::move(borrowed));
        }
        catch (const std::bad_alloc&)
        {
            refused = true;
        }
        Require(
            refused && allocations == 1 && borrowed.GetDataArray() == external && borrowed.GetIsBorrowed() && borrowed.GetSize() == 3 &&
                borrowed[0] == 2 && borrowed[2] == 8,
            "failed borrowed allocation propagates bad_alloc and leaves the source untouched"
        );
        allocations = 0;
        SmTArray<double> copied(std::move(borrowed));
        Require(
            allocations == 1 && !copied.GetIsBorrowed() && copied.GetDataArray() != external && copied[1] == 4 && borrowed.GetSize() == 0 &&
                borrowed.GetDataArray() == nullptr,
            "borrowed move retries with exactly one independent data allocation"
        );
        std::cout << "owned data allocations: 0; borrowed data allocations: 1; failed allocation preserves source\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
