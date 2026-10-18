// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanar.h"

// Expected rejection in the surveyed revision: this constructor is not constexpr.
constexpr step_planar::Rational half(1, 2);
static_assert(half.Numerator() == 1 && half.Denominator() == 2);
