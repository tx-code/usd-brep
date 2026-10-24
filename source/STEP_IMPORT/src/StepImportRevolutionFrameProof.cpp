// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportRevolutionFrameProof.h"

#include "StepImportInterval.h"
#include "StepImportProofEnvironment.h"

namespace step_import::detail
{
namespace
{
auto Refuse(const char* key, Status status = Status::Indeterminate)
{
    return tl::unexpected(Failure{ status, key, "Whole-domain revolution frame evidence is unestablished", {}, "TerminalRevolutionFrame" });
}
double Magnitude(Interval a)
{
    return std::max(std::abs(a.low), std::abs(a.high));
}
} // namespace
Result<double> TerminalRevolutionFrameBound(
    const std::array<std::string, 3>& sourceAxis,
    const Point& parsedSourceAxis,
    const std::array<std::string, 3>& sourceOrigin,
    const terminal_spline::IntegerRatio& lengthToMm,
    const Point& actualOrigin,
    const std::array<Point, 3>& actualFrame,
    const terminal_spline::BinaryNet& generator,
    const terminal_spline::Limits& limits,
    const terminal_spline::Charge& charge
)
{
    namespace ts = terminal_spline;
    if (!SurfaceProofNumericEnvironment())
    {
        return Refuse("TerminalRevolutionFrameNumericEnvironment");
    }
    if (generator.homogeneous.empty() || generator.homogeneous.size() > limits.controlSlots ||
        generator.homogeneous.size() > (std::numeric_limits<std::size_t>::max() - 128) / 16)
    {
        return Refuse("TerminalRevolutionFrameControlCapacity", Status::ResourceLimit);
    }
    // Callbacks and their real exceptions run outside the arithmetic catch.
    // The exact helper compares SOURCE decimals, including sub-ulp components.
    if (auto work = charge(128 + 16 * generator.homogeneous.size()); !work)
    {
        return tl::unexpected(std::move(work.error()));
    }
    auto axisError = ts::SourcePointDifferenceUpper(sourceAxis, { "1", "1" }, parsedSourceAxis, limits, charge);
    if (!axisError)
    {
        return tl::unexpected(std::move(axisError.error()));
    }
    auto originError = ts::SourcePointDifferenceUpper(sourceOrigin, lengthToMm, actualOrigin, limits, charge);
    if (!originError)
    {
        return tl::unexpected(std::move(originError.error()));
    }
    try
    {
        IntervalPoint axis{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            axis[i] = *axisError == 0 ? Exact(parsedSourceAxis[i]) :
                                        Interval{ Down(parsedSourceAxis[i] - *axisError), Up(parsedSourceAxis[i] + *axisError) };
        }
        const auto length = SqrtNonnegative(Dot(axis, axis));
        if (!(length.low > 0))
        {
            return Refuse("TerminalRevolutionSourceAxisNormalization");
        }
        axis = Times(axis, Exact(1) / length);
        const auto x = Exact(actualFrame[0]), y = Exact(actualFrame[1]), z = Exact(actualFrame[2]);
        Interval matrixDifference = Exact(0);
        // EvaluateSTEP uses M=c(XX^T+YY^T)+s(YX^T-XY^T)+ZZ^T.
        // Ideal source rotation is c(I-nn^T)+s[n]_cross+nn^T. Summing
        // absolute entries of all three difference matrices bounds their
        // spectral norms for EVERY angle, including imperfect native frames.
        // This avoids assuming GetCanonical's rounded axis is exactly unit.
        for (std::size_t r = 0; r < 3; ++r)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                const auto nn = axis[r] * axis[c];
                const auto cosine = x[r] * x[c] + y[r] * y[c] - (Exact(r == c ? 1 : 0) - nn);
                Interval skew = Exact(0);
                if (r != c)
                {
                    const auto index = 3 - r - c;
                    const bool positive = (r == 1 && c == 0) || (r == 2 && c == 1) || (r == 0 && c == 2);
                    skew = axis[index] * Exact(positive ? 1 : -1);
                }
                const auto sine = y[r] * x[c] - x[r] * y[c] - skew;
                const auto constant = z[r] * z[c] - nn;
                matrixDifference = matrixDifference + Exact(Magnitude(cosine)) + Exact(Magnitude(sine)) + Exact(Magnitude(constant));
            }
        }
        double radius = 0;
        const auto origin = Exact(actualOrigin);
        for (const auto& h : generator.homogeneous)
        {
            if (!(h[3] > 0) || !std::isfinite(h[3]))
            {
                return Refuse("TerminalRevolutionGeneratorWeight");
            }
            IntervalPoint p{};
            for (std::size_t i = 0; i < 3; ++i)
            {
                p[i] = Exact(h[i]) / Exact(h[3]);
            }
            radius = std::max(radius, NormUpper(Subtract(p, origin)));
        }
        // Positive rational control convexity bounds the actual generator
        // throughout the extended domain. With ideal rotation R an isometry:
        // |Sa-Ss| <= |Ga-Gs| + beta*max|Ga-Oa| + 2*|Oa-Os|.
        // The first term belongs to the separate generator certificate.
        return (matrixDifference * Exact(radius) + Exact(2) * Exact(*originError)).high;
    }
    catch (const std::overflow_error&)
    {
        return Refuse("TerminalRevolutionFrameNumericCapacity");
    }
    catch (const std::domain_error&)
    {
        return Refuse("TerminalRevolutionFrameNumericUnestablished");
    }
}
} // namespace step_import::detail
