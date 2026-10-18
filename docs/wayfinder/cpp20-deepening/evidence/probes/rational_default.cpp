// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanar.h"

constexpr step_planar::Rational zero;
static_assert(zero.Numerator() == 0 && zero.Denominator() == 1);
