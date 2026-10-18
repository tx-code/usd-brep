// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <vector>

// Observe whether allocated storage can persist in this constexpr object;
// successful transient-vector evaluation alone does not establish that claim.
constexpr std::vector<int> data{ 1, 2, 3 };
static_assert(data.size() == 3);
