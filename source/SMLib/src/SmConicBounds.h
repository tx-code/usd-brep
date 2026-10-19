// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <SmMessages.h>
#include <SmTypes.h>
class SmCurve;
class SmExtent1d;
class SmExtent3d;
namespace sm_conic_bounds
{
struct Attempt
{
    bool handled = false;
    SmStatus status = SM_SUCCESS;
};
// Internal, stateless query. Unsupported/inconclusive arithmetic leaves output
// untouched and retains the caller's general solver; native evaluation failures
// are handled failures. No output is assigned before the complete box succeeds.
Attempt Try(const SmCurve& curve, const SmExtent1d& interval, SmExtent3d& output);
} // namespace sm_conic_bounds
