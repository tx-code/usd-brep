// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <vector>

consteval std::array<int, 3> BuildFixedData()
{
    std::vector<int> work{ 3, 1, 2 };
    std::sort(work.begin(), work.end());
    return { work[0], work[1], work[2] };
}
static_assert(BuildFixedData() == std::array{ 1, 2, 3 });
