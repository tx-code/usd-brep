// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <bitset>

consteval auto BuildMembership()
{
    std::bitset<256> bits;
    bits.set(3);
    return bits;
}
constexpr auto membership = BuildMembership();
