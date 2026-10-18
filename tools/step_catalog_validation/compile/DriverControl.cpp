// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <array>

constexpr std::array<int, 3> Values{ 1, 2, 3 };
static_assert(Values[2] == 3);
#if defined(STEP_CATALOG_DRIVER_REJECT)
static_assert(false, "STEP_CATALOG_DRIVER_CONTROL: deliberate rejection");
#endif
