// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportLinearExtrusion.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportInterval.h"

#include <SmBSplineSurface.h>
#include <SmCrvOnSurf.h>
#include <SmLine.h>
#include <limits>
#include <type_traits>

namespace step_import::detail::construction
{
namespace
{
class Builder : private GeometryConstruction
{
public:

    explicit Builder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }

    Result<std::unique_ptr<SmSurface>> Make(
        Index geometry,
        Index use,
        std::span<const LinearExtrusionBoundary> boundaries,
        std::optional<std::array<double, 2>> parameterV
    )
    {
        return Build(geometry, use, boundaries, parameterV);
    }

private:

    template <class Function>
    auto Arithmetic(Index geometry, Function&& function) -> Result<decltype(function())>
    {
        // Only pure interval arithmetic is translated to evidence refusal.
        // Native calls, allocation and caller callbacks remain outside this
        // boundary, retaining their existing execution/exception contracts.
        try
        {
            if constexpr (std::is_void_v<decltype(function())>)
            {
                function();
                return {};
            }
            else
            {
                return function();
            }
        }
        catch (const std::overflow_error&)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionNumericCapacity", geometry, Status::Indeterminate));
        }
        catch (const std::domain_error&)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionNumericDomain", geometry, Status::Indeterminate));
        }
    }

    Result<void> Work(Index geometry)
    {
        return m_execution.SpendExactGeometryWork(m_report, 1, geometry);
    }
    Result<std::unique_ptr<SmSurface>> Build(
        Index geometry,
        Index use,
        std::span<const LinearExtrusionBoundary> boundaries,
        std::optional<std::array<double, 2>> parameterV
    )
    {
        // AP214E3_2010.exp (71ab140f...), inherited swept_curve and local
        // extrusion_axis. ISO 10303-42 surface_of_linear_extrusion defines
        // sigma(u,v)=lambda(u)+v*V; VECTOR magnitude determines V's scale.
        // Zero magnitude is legal VECTOR source data but has no 2D native chart.
        const auto generator = Ref(geometry, "swept_curve");
        if (!Is(generator, "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionGeneratorUnsupported", generator, Status::Unsupported));
        }
        const auto vector = Ref(geometry, "extrusion_axis");
        auto direction = Direction(Ref(vector, "orientation"));
        auto magnitude = N(vector, "magnitude");
        if (!direction)
        {
            return tl::unexpected(std::move(direction.error()));
        }
        if (!magnitude)
        {
            return tl::unexpected(std::move(magnitude.error()));
        }
        const auto physical = *magnitude * scale;
        const V axis = Mul(*direction, physical);
        if (!std::isfinite(physical) || !(physical > 0) || !std::isfinite(Norm(axis)) || !(Norm(axis) > 0))
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionAxisUnestablished", vector, Status::Unsupported));
        }
        auto squared = Arithmetic(
            geometry,
            [&]
            {
                return Dot(Exact(axis), Exact(axis));
            }
        );
        if (!squared)
        {
            return tl::unexpected(std::move(squared.error()));
        }
        const auto axisI = Exact(axis);
        const auto axisSquared = *squared;
        if (!(axisSquared.low > 0))
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionAxisUnestablished", vector, Status::Indeterminate));
        }
        auto made = MakeSplineCurve(generator, use);
        if (!made)
        {
            return tl::unexpected(std::move(made.error()));
        }
        auto curve = std::move(*made);
        const auto* spline = dynamic_cast<const SmBSplineCurve*>(curve.get());
        if (!spline || spline->GetDim() != 3)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionGeneratorDimension", generator, Status::Unsupported));
        }
        ULONG count = 0;
        double* net = nullptr;
        if (auto checked = Good(spline->GetControlPointsPointer(count, net)); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (!count || !net)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionGeneratorDefinitionMissing", generator));
        }
        if (count > m_options.sourceLimits.values / 2 || count > std::numeric_limits<ULONG>::max() / 2)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionControlLimit", geometry, Status::ResourceLimit));
        }
        if (count != Refs(generator, "control_points_list").size())
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionGeneratorControlsChanged", generator));
        }
        const bool rational = spline->IsRational() != FALSE;
        std::vector<Point> controls;
        std::vector<double> weights;
        controls.reserve(count);
        weights.reserve(count);
        double generatorLow = INFINITY, generatorHigh = -INFINITY;
        for (ULONG i = 0; i < count; ++i)
        {
            if (auto checked = Work(geometry); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            const double w = rational ? net[4 * i + 3] : 1.;
            if (!(w > 0) || !std::isfinite(w))
            {
                return tl::unexpected(Fail("GeneralLinearExtrusionPositiveWeights", generator));
            }
            IntervalPoint p;
            Point center;
            auto projected = Arithmetic(
                geometry,
                [&]
                {
                    for (Index k = 0; k < 3; ++k)
                    {
                        p[k] = Exact(net[4 * i + k]) / Exact(w);
                        center[k] = net[4 * i + k] / w;
                    }
                    return Dot(p, axisI) / axisSquared;
                }
            );
            if (!projected)
            {
                return tl::unexpected(std::move(projected.error()));
            }
            generatorLow = std::min(generatorLow, projected->low);
            generatorHigh = std::max(generatorHigh, projected->high);
            controls.push_back(center);
            weights.push_back(w);
        }
        double low = INFINITY, high = -INFINITY;
        const auto include = [&](IntervalPoint point)
        {
            const auto projected = Dot(point, axisI) / axisSquared;
            low = std::min(low, Down(projected.low - generatorHigh));
            high = std::max(high, Up(projected.high - generatorLow));
        };
        if (parameterV)
        {
            low = (*parameterV)[0];
            high = (*parameterV)[1];
        }
        else
        {
            for (const auto& boundary : boundaries)
            {
                if (!boundary.curve)
                {
                    return tl::unexpected(Fail("GeneralLinearExtrusionBoundaryMissing", use));
                }
                const bool implicit = dynamic_cast<const SmCrvOnSurf*>(boundary.curve) != nullptr;
                const bool line = dynamic_cast<const SmLine*>(boundary.curve) != nullptr;
                const auto* basis = implicit || line ? nullptr : dynamic_cast<const SmBSplineCurve*>(boundary.curve);
                if (basis)
                {
                    const auto natural = basis->GetNaturalInterval();
                    if (boundary.interval[0] < natural.GetMin() || boundary.interval[1] > natural.GetMax())
                    {
                        return tl::unexpected(Fail("GeneralLinearExtrusionBoundaryIntervalUnestablished", use, Status::Indeterminate));
                    }
                    ULONG n = 0;
                    double* data = nullptr;
                    if (auto checked = Good(basis->GetControlPointsPointer(n, data)); !checked)
                    {
                        return tl::unexpected(std::move(checked.error()));
                    }
                    if (!n || !data)
                    {
                        return tl::unexpected(Fail("GeneralLinearExtrusionBoundaryDefinitionMissing", use));
                    }
                    if (n > m_options.sourceLimits.values)
                    {
                        return tl::unexpected(Fail("GeneralLinearExtrusionBoundaryControlLimit", use, Status::ResourceLimit));
                    }
                    for (ULONG i = 0; i < n; ++i)
                    {
                        if (auto checked = Work(geometry); !checked)
                        {
                            return tl::unexpected(std::move(checked.error()));
                        }
                        const auto w = basis->IsRational() ? data[4 * i + 3] : 1.;
                        if (!(w > 0) || !std::isfinite(w))
                        {
                            return tl::unexpected(Fail("GeneralLinearExtrusionPositiveWeights", use));
                        }
                        auto added = Arithmetic(
                            geometry,
                            [&]
                            {
                                include({ Exact(data[4 * i]) / Exact(w), Exact(data[4 * i + 1]) / Exact(w), Exact(data[4 * i + 2]) / Exact(w) });
                            }
                        );
                        if (!added)
                        {
                            return tl::unexpected(std::move(added.error()));
                        }
                    }
                }
                else
                {
                    // Implicit defining curves already carry the shared factory's
                    // positive carrier-control-net enclosure. LINE's cached box
                    // covers its actual retained interval, not a finite unit
                    // segment of the infinite definition. Do not sample either.
                    if (!implicit && !line)
                    {
                        return tl::unexpected(Fail("GeneralLinearExtrusionBoundaryUnsupported", use, Status::Unsupported));
                    }
                    for (unsigned mask = 0; mask < 8; ++mask)
                    {
                        if (auto checked = Work(geometry); !checked)
                        {
                            return tl::unexpected(std::move(checked.error()));
                        }
                        Point p;
                        for (Index k = 0; k < 3; ++k)
                        {
                            p[k] = boundary.implicitBounds[(mask >> k) & 1U][k];
                        }
                        auto added = Arithmetic(
                            geometry,
                            [&]
                            {
                                include(Exact(p));
                            }
                        );
                        if (!added)
                        {
                            return tl::unexpected(std::move(added.error()));
                        }
                    }
                }
            }
        }
        if (!std::isfinite(low) || !std::isfinite(high) || !(high >= low))
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionWindowUnestablished", use, Status::Indeterminate));
        }
        // A computational enclosure changes neither the infinite source surface
        // nor its scalar V. Account for the generator's axial extent as well as
        // every boundary hull; endpoint-only bounds cannot enclose bowed edges.
        const auto padding = std::max(
            parameterV ? 1. / Norm(axis) : Q(m_options.quality.definitionMm) / Norm(axis),
            64 * std::numeric_limits<double>::epsilon() * (1 + std::abs(low) + std::abs(high))
        );
        auto padded = Arithmetic(
            geometry,
            [&]
            {
                low = Down(low - padding);
                high = Up(high + padding);
            }
        );
        if (!padded)
        {
            return tl::unexpected(std::move(padded.error()));
        }
        if (!(high > low))
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionWindowUnestablished", use, Status::Indeterminate));
        }
        SmTArray<SmPoint3d> cp;
        SmTArray<double> surfaceWeights;
        for (Index i = 0; i < controls.size(); ++i)
        {
            for (const auto v : { low, high })
            {
                if (auto checked = Work(geometry); !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                const auto p = Add(controls[i], Mul(axis, v));
                if (!std::all_of(
                        p.begin(),
                        p.end(),
                        [](double x)
                        {
                            return std::isfinite(x);
                        }
                    ))
                {
                    return tl::unexpected(Fail("GeneralLinearExtrusionControlRange", geometry, Status::Indeterminate));
                }
                cp.Add(P(p));
                surfaceWeights.Add(weights[i]);
            }
        }
        auto mult = Multiplicities(generator, "knot_multiplicities");
        auto knots = Numbers(generator, "knots");
        auto degree = Degree(generator, "degree");
        if (!mult)
        {
            return tl::unexpected(std::move(mult.error()));
        }
        if (!knots)
        {
            return tl::unexpected(std::move(knots.error()));
        }
        if (!degree)
        {
            return tl::unexpected(std::move(degree.error()));
        }
        SmTArray<ULONG> vm;
        vm.Add(2);
        vm.Add(2);
        SmTArray<double> vk;
        vk.Add(low);
        vk.Add(high);
        SmBSplineSurface* raw = nullptr;
        if (auto checked = Step(geometry); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const auto created = SmBSplineSurface::
            CreateCanonical(*m_context, *degree, 1, cp, SM_SF_UNSPECIFIED, *mult, vm, *knots, vk, SM_KT_UNSPECIFIED, &surfaceWeights, nullptr, raw);
        std::unique_ptr<SmSurface> surface(raw);
        if (auto checked = Good(created); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (!raw)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionAllocation", geometry));
        }
        ULONG nu = 0, nv = 0;
        double* actual = nullptr;
        if (auto checked = Good(raw->GetControlPointsPointer(nu, nv, actual)); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const auto domain = raw->GetNaturalUVDomain();
        const auto original = spline->GetNaturalInterval();
        if (nu != count || nv != 2 || !actual || domain.GetMin().x != original.GetMin() || domain.GetMax().x != original.GetMax() ||
            domain.GetMin().y != low || domain.GetMax().y != high)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionParametersChanged", geometry));
        }
        SmTArray<double> generatorKnots;
        if (auto checked = Good(spline->GetKnotsAll(generatorKnots)); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        // Creating an ordinary generator may correct near-coincident knots.
        // Such a change cannot establish the original source U parameter map.
        // Compare every transmitted knot group and multiplicity, not just its
        // two endpoints or the newly constructed surface against the copy.
        ULONG knotIndex = 0;
        for (ULONG i = 0; i < knots->GetSize(); ++i)
        {
            for (ULONG j = 0; j < (*mult)[i]; ++j)
            {
                if (knotIndex >= generatorKnots.GetSize() || generatorKnots[knotIndex++] != (*knots)[i])
                {
                    return tl::unexpected(Fail("GeneralLinearExtrusionSourceKnotsChanged", generator));
                }
            }
        }
        const auto sourceDomain = spline->GetSTEPInterval();
        if (knotIndex != generatorKnots.GetSize() || sourceDomain.GetMin() != original.GetMin() || sourceDomain.GetMax() != original.GetMax())
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionSourceParametersUnestablished", generator));
        }
        ULONG ku = 0, kv = 0;
        double *uk = nullptr, *vknots = nullptr;
        if (auto checked = Good(raw->GetKnotsPointers(ku, kv, uk, vknots)); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (raw->GetDegree(SM_SP_U) != spline->GetDegree() || raw->GetDegree(SM_SP_V) != 1 || ku != generatorKnots.GetSize() || !uk || kv != 4 ||
            !vknots || !std::equal(uk, uk + ku, generatorKnots.GetDataArray()) || vknots[0] != low || vknots[1] != low || vknots[2] != high ||
            vknots[3] != high)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionKnotsChanged", geometry));
        }
        double error = 0;
        for (ULONG i = 0; i < count; ++i)
        {
            for (ULONG j = 0; j < 2; ++j)
            {
                if (auto checked = Work(geometry); !checked)
                {
                    return tl::unexpected(std::move(checked.error()));
                }
                const auto index = 4 * (i * 2 + j);
                const auto w = raw->IsRational() ? actual[index + 3] : 1.;
                if (w != weights[i])
                {
                    return tl::unexpected(Fail("GeneralLinearExtrusionWeightsChanged", geometry));
                }
                auto bound = Arithmetic(
                    geometry,
                    [&]
                    {
                        IntervalPoint delta;
                        for (Index k = 0; k < 3; ++k)
                        {
                            const auto ideal = Exact(net[4 * i + k]) / Exact(weights[i]) + Exact(j ? high : low) * axisI[k];
                            delta[k] = Exact(actual[index + k]) / Exact(w) - ideal;
                        }
                        return NormUpper(delta);
                    }
                );
                if (!bound)
                {
                    return tl::unexpected(std::move(bound.error()));
                }
                error = std::max(error, *bound);
            }
        }
        if (error > Enclose(m_options.quality.definitionMm).low)
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionControlConversionUnestablished", geometry, Status::Indeterminate));
        }
        ParameterMapping mapping;
        mapping.kind = "ExactLinearExtrusionChart";
        mapping.sourceGeometryId = Id(geometry);
        mapping.sourceUseId = Id(use);
        mapping.equation =
            "S(u,v)=shared_native_generator(u)+v*physical_vector_mm; original U knots and scalar V retained; stored-definition construction bound only";
        mapping.parameters = { { "native_generator_interval", { original.GetMin(), original.GetMax() } },
                               { "native_scalar_v_interval", { low, high } },
                               { "extrusion_vector_mm", { axis[0], axis[1], axis[2] } },
                               { "source_vector_magnitude", { *magnitude } },
                               { "length_to_mm", { scale } },
                               { "control_construction_error_upper_mm", { error } } };
        m_report.parameterMappings.push_back(std::move(mapping));
        return surface;
    }
};
} // namespace
Result<std::unique_ptr<SmSurface>> MakeSourceLinearExtrusion(
    const GeometryConstruction& inputs,
    std::size_t geometry,
    std::size_t use,
    std::span<const LinearExtrusionBoundary> boundaries,
    std::optional<std::array<double, 2>> parameterV
)
{
    return Builder(inputs).Make(geometry, use, boundaries, parameterV);
}
} // namespace step_import::detail::construction
