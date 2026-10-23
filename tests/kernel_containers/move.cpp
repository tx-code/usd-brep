// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmContext.h>
#include <SmPointSet.h>
#include <SmTArray.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <tl/expected.hpp>
#include <type_traits>
#include <utility>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
template <class T>
void EmptySource(const SmTArray<T>& array)
{
    Require(
        array.GetSize() == 0 && array.GetDataSize() == 0 && array.GetDataArray() == nullptr && !array.GetIsBorrowed(),
        "successful move leaves an empty unborrowed reusable source"
    );
}
void Owned()
{
    SmContext context;
    SmTArray<double> source(context);
    source.Add(1.25);
    source.Add(2.5);
    const auto data = source.GetDataArray();
    const auto capacity = source.GetDataSize();
    SmTArray<double> copy(source);
    Require(copy.GetDataArray() != data && copy[0] == 1.25 && source.GetSize() == 2, "lvalue copying remains independent");
    SmTArray<double> moved(std::move(source));
    Require(
        moved.GetDataArray() == data && moved.GetDataSize() == capacity && moved.GetSize() == 2 && moved[1] == 2.5,
        "owned move transfers the existing data buffer and capacity without copying"
    );
    Require(moved.GetContext() == &context && source.GetContext() == &context, "ordinary move retains nonowning context association");
    EmptySource(source);
    source.Add(7);
    Require(
        source[0] == 7 && source.GetDataArray() != moved.GetDataArray() && moved[0] == 1.25,
        "moved-from source can allocate and grow independently"
    );
    SmTArray<double> reserved(64);
    const auto emptyData = reserved.GetDataArray();
    const auto emptyCapacity = reserved.GetDataSize();
    SmTArray<double> emptyMove(std::move(reserved));
    Require(
        emptyMove.GetSize() == 0 && emptyMove.GetDataArray() == emptyData && emptyMove.GetDataSize() == emptyCapacity,
        "reserved owned storage also transfers when no elements are used"
    );
    EmptySource(reserved);
}
SmTArray<double> BorrowedReturn()
{
    double stack[] = { 1.25, 2.5, 5 };
    SmTArray<double> borrowed(3, stack, 3);
    // Explicit construction exercises the move even with return-value elision.
    return SmTArray<double>(std::move(borrowed));
}
void Borrowed()
{
    double external[] = { 1, 2 };
    SmTArray<double> source(2, external, 2);
    SmTArray<double> moved(std::move(source));
    Require(
        !moved.GetIsBorrowed() && moved.GetDataArray() != external && moved.GetSize() == 2 && moved[1] == 2,
        "borrowed move owns independent data instead of exporting an external lifetime"
    );
    EmptySource(source);
    external[0] = 9;
    Require(moved[0] == 1, "borrowed fallback does not retain an external alias");
    source.Add(4);
    Require(source[0] == 4 && external[0] == 9, "borrowed moved-from source no longer writes external storage");
    auto returned = BorrowedReturn();
    Require(
        !returned.GetIsBorrowed() && returned.GetSize() == 3 && returned[0] == 1.25 && returned[2] == 5,
        "borrowed move can leave the external stack buffer's scope"
    );
    SmTArray<double> detached(2, external, 2);
    detached.Add(3);
    Require(!detached.GetIsBorrowed(), "fixture grows beyond external storage");
    const auto data = detached.GetDataArray();
    SmTArray<double> detachedMove(std::move(detached));
    Require(detachedMove.GetDataArray() == data && detachedMove.GetSize() == 3, "grown borrowed arrays use the owned transfer path");
    EmptySource(detached);
}
void ContextAndPointers()
{
    SmContext first, second;
    SmTArray<ULONG> source(first);
    source.Add(23);
    const auto data = source.GetDataArray();
    std::unique_ptr<SmTArray<ULONG>> moved(new (second) SmTArray<ULONG>(std::move(source)));
    Require(
        moved->GetContext() == &second && source.GetContext() == &first && moved->GetDataArray() == data && (*moved)[0] == 23,
        "SmObject placement policy retains destination context while transferring the buffer"
    );
    EmptySource(source);
    int value = 7;
    SmTArray<int*> pointers;
    pointers.Add(&value);
    const auto entries = pointers.GetDataArray();
    SmTArray<int*> pointerMove(std::move(pointers));
    Require(
        pointerMove.GetDataArray() == entries && pointerMove[0] == &value && value == 7,
        "pointer-array move transfers entries without owning or cloning pointees"
    );
    EmptySource(pointers);
}
tl::expected<SmTArray<double>, int> Pack(SmTArray<double>&& source)
{
    return std::move(source);
}
tl::expected<SmTArray<double>, int> LocalResult(const double*& data)
{
    SmTArray<double> values;
    values.Add(11);
    data = values.GetDataArray();
    // C++20 implicit return-move also applies when the result wraps the array.
    return values;
}
void ExpectedValue()
{
    SmTArray<double> source;
    source.Add(3);
    source.Add(8);
    const auto data = source.GetDataArray();
    auto result = Pack(std::move(source));
    Require(result && result->GetDataArray() == data, "expected construction preserves the original owned data allocation");
    EmptySource(source);
    SmTArray<double> value(std::move(*result));
    Require(
        value.GetDataArray() == data && value[0] == 3 && value[1] == 8,
        "expected extraction performs a second move without copying or reallocating data"
    );
    EmptySource(*result);
    const double* localData = nullptr;
    auto local = LocalResult(localData);
    Require(
        local && local->GetDataArray() == localData && (*local)[0] == 11,
        "returning a local array into expected uses implicit move without copying its data"
    );
}
void DerivedCopyCompatibility()
{
    SmPointSet3d source;
    source.Add(SmPoint3d(0, 0, 0));
    source.Add(SmPoint3d(1, 2, 3));
    const auto data = source.GetDataArray();
    SmPointSet3d copied(std::move(source));
    Require(
        source.GetSize() == 2 && source.GetDataArray() == data && copied.GetSize() == 2 && copied.GetDataArray() != data,
        "adding a base-array move must not change the point-set's existing rvalue copy contract and cached state"
    );
}
} // namespace
static_assert(std::is_copy_constructible_v<SmTArray<double>>);
static_assert(std::is_move_constructible_v<SmTArray<double>>);
// Borrowed storage can need an allocation; do not advertise unconditional noexcept.
static_assert(!std::is_nothrow_move_constructible_v<SmTArray<double>>);
int main()
{
    try
    {
        Owned();
        Borrowed();
        ContextAndPointers();
        ExpectedValue();
        DerivedCopyCompatibility();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
