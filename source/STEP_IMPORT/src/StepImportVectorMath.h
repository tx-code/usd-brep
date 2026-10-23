// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"
#include "StepImportResult.h"

#include <cmath>
#include <stdexcept>

namespace step_import::detail::construction
{
using Index = std::size_t;
using V = Point;
inline V Add(V a, V b)
{
    for (Index i = 0; i < 3; ++i)
    {
        a[i] += b[i];
    }
    return a;
}
inline V Sub(V a, V b)
{
    for (Index i = 0; i < 3; ++i)
    {
        a[i] -= b[i];
    }
    return a;
}
inline V Mul(V a, double b)
{
    for (auto& x : a)
    {
        x *= b;
    }
    return a;
}
inline double Dot(V a, V b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
inline V Cross(V a, V b)
{
    return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}
inline double Norm(V a)
{
    return std::hypot(a[0], a[1], a[2]);
}
[[nodiscard]] inline Result<V> Unit(V a)
{
    const double n = Norm(a);
    if (!(n > 0) || !std::isfinite(n))
    {
        // Keep the former domain-error outcome and unfinished strict-attempt
        // observation. Do not rescale the input or silently widen its admitted
        // numeric range; the established normalization formula stays unchanged.
        return tl::unexpected(
            Failure{ Status::Indeterminate,
                     "GeneralDegenerateFrame",
                     "Native derived evidence is unestablished",
                     {},
                     "D5",
                     NativeFailureKind::ConstructionOperation }
        );
    }
    return Mul(a, 1 / n);
}
} // namespace step_import::detail::construction
