// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportLinearExtrusion.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportInterval.h"
#include "StepImportNativeStatus.h"
#include "StepImportProofEnvironment.h"
#include "StepImportTerminalSpline.h"

#include <SmBSplineSurface.h>
#include <SmCrvOnSurf.h>
#include <SmLine.h>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace step_import::detail::construction
{
namespace ts = terminal_spline;
struct LinearExtrusionBinding
{
    std::size_t geometry = 0, use = 0;
    bool carrier = false;
    const SmSurface* identity = nullptr; // Borrowed address, never dereferenced without a live owner match.
    ts::BinaryNet expected;
    ParameterMapping mapping;
};
struct LinearExtrusionSession
{
    std::vector<LinearExtrusionBinding> bindings;
};
bool HasLinearExtrusions(const LinearExtrusionSession& session) noexcept
{
    return !session.bindings.empty();
}
std::shared_ptr<LinearExtrusionSession> MakeLinearExtrusionSession()
{
    return std::make_shared<LinearExtrusionSession>();
}
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
        auto made = MakeSplineCurve(generator, use, CurveEndControlPolicy::PendingNaturalDomainCertificate);
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
        SmTArray<double> generatorKnots;
        if (auto checked = Good(spline->GetKnotsAll(generatorKnots)); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        bool sameKnots = true;
        ULONG knotIndex = 0;
        for (ULONG i = 0; i < knots->GetSize(); ++i)
        {
            for (ULONG j = 0; j < (*mult)[i]; ++j)
            {
                sameKnots = sameKnots && knotIndex < generatorKnots.GetSize() && generatorKnots[knotIndex] == (*knots)[i];
                ++knotIndex;
            }
        }
        sameKnots = sameKnots && knotIndex == generatorKnots.GetSize();
        const auto originalInterval = spline->GetNaturalInterval(), sourceDomain = spline->GetSTEPInterval();
        if (sourceDomain.GetMin() != originalInterval.GetMin() || sourceDomain.GetMax() != originalInterval.GetMax())
        {
            return tl::unexpected(Fail("GeneralLinearExtrusionSourceParametersUnestablished", generator));
        }
        std::optional<ts::Proof> generatorProof;
        SmTArray<ULONG> canonicalMult;
        SmTArray<double> canonicalKnots;
        if (!sameKnots)
        {
            const ts::Limits limits{ m_options.sourceLimits.values, 4096, 1234 };
            const ts::Charge charge = [&](std::size_t units)
            {
                return m_execution.SpendExactGeometryWork(m_report, units, generator);
            };
            auto source = ReadExactSplineSource(generator, use, limits, charge);
            if (!source)
            {
                return tl::unexpected(std::move(source.error()));
            }
            auto reference = ts::CaptureNaturalCurveReference(*source, limits, charge);
            if (!reference)
            {
                return tl::unexpected(std::move(reference.error()));
            }
            ts::BinaryNet stored;
            stored.basis = { *degree, 0, count, 1, { generatorKnots.GetDataArray(), generatorKnots.GetDataArray() + generatorKnots.GetSize() }, {} };
            if (auto checked = charge(count); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            stored.homogeneous.reserve(count);
            for (ULONG i = 0; i < count; ++i)
            {
                stored.homogeneous.push_back({ net[4 * i], net[4 * i + 1], net[4 * i + 2], rational ? net[4 * i + 3] : 1. });
            }
            const auto allowance = m_options.quality.definitionMm;
            // Allocate ONE original definition budget: half for the complete
            // exact-source -> stored-generator certificate, half for constructing
            // the tensor from that generator and the constructed physical axis.
            // Unsigned doubling covers every positive int64 Fraction denominator
            // without overflow or modifying the caller's quality configuration.
            const ts::IntegerRatio half{ std::to_string(allowance.Numerator()), std::to_string(std::uint64_t(allowance.Denominator()) * 2) };
            // Certify compares at the SAME scalar U and requires equal EXACT
            // natural endpoints. No affine trim map is silently applied to a
            // source pcurve. Unestablished endpoint/basis evidence stays a refusal.
            auto proof = ts::Certify(*reference, stored, half, limits, charge);
            if (!proof)
            {
                return tl::unexpected(std::move(proof.error()));
            }
            if (!proof->established || !proof->maximumMm || !std::isfinite(*proof->maximumMm))
            {
                return tl::unexpected(Fail("GeneralLinearExtrusionGeneratorCertificateUnestablished", generator, Status::Indeterminate));
            }
            generatorProof = std::move(*proof);
            for (ULONG i = 0; i < generatorKnots.GetSize();)
            {
                ULONG end = i + 1;
                while (end < generatorKnots.GetSize() && generatorKnots[end] == generatorKnots[i])
                {
                    ++end;
                }
                canonicalKnots.Add(generatorKnots[i]);
                canonicalMult.Add(end - i);
                i = end;
            }
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
        const auto created = SmBSplineSurface::CreateCanonical(
            *m_context,
            *degree,
            1,
            cp,
            SM_SF_UNSPECIFIED,
            sameKnots ? *mult : canonicalMult,
            vm,
            sameKnots ? *knots : canonicalKnots,
            vk,
            SM_KT_UNSPECIFIED,
            &surfaceWeights,
            nullptr,
            raw
        );
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
        const auto definitionLimit = Enclose(m_options.quality.definitionMm).low;
        if (error > definitionLimit * (generatorProof ? .5 : 1.))
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
        if (generatorProof)
        {
            mapping.sourceBasisGeometryId = Id(generator);
            mapping.equation =
                "S(u,v)=certified_same_U_generator(u)+v*constructed_physical_vector_mm; canonical target knots retained; source bytes unchanged; generator and tensor budgets share one definition allowance; original vector conversion is not certified here";
            mapping.parameters.push_back({ "source_generator_error_upper_mm", { *generatorProof->maximumMm } });
            mapping.parameters.push_back({ "source_generator_certificate_patches", { double(generatorProof->patches) } });
            mapping.parameters.push_back({ "generator_definition_allowance_mm", { definitionLimit * .5 } });
            mapping.parameters.push_back({ "tensor_definition_allowance_mm", { definitionLimit * .5 } });
            const auto combinedError = Up(*generatorProof->maximumMm + error);
            mapping.parameters.push_back({ "generator_plus_tensor_error_upper_mm", { combinedError } });
            mapping.parameters.push_back({ "defining_carrier", { parameterV ? 1. : 0. } });
            if (combinedError > definitionLimit)
            {
                return tl::unexpected(Fail("GeneralLinearExtrusionCombinedDefinitionUnestablished", geometry, Status::Indeterminate));
            }
            if (auto checked = m_execution.SpendExactGeometryWork(m_report, std::size_t(nu) * nv + ku + kv, use); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            ts::BinaryNet expected;
            expected.basis = { raw->GetDegree(SM_SP_U), raw->GetDegree(SM_SP_V), nu, nv, { uk, uk + ku }, { vknots, vknots + kv } };
            expected.homogeneous.reserve(std::size_t(nu) * nv);
            for (std::size_t i = 0; i < std::size_t(nu) * nv; ++i)
            {
                expected.homogeneous.push_back({ actual[4 * i], actual[4 * i + 1], actual[4 * i + 2], raw->IsRational() ? actual[4 * i + 3] : 1. });
            }
            m_linearExtrusions->bindings.push_back({ geometry, use, bool(parameterV), raw, std::move(expected), mapping });
        }
        m_report.parameterMappings.push_back(std::move(mapping));
        return surface;
    }
};
} // namespace
Result<std::vector<ParameterMapping>> VerifyLinearExtrusions(
    const LinearExtrusionSession& session,
    std::span<const LinearExtrusionTarget> targets,
    const SmContext& context,
    const std::function<Result<void>(std::size_t)>& native,
    const std::function<Result<void>(std::size_t, std::size_t)>& work
)
{
    std::vector<ParameterMapping> evidence;
    if (session.bindings.empty())
    {
        return evidence;
    }
    using Key = std::pair<std::size_t, std::size_t>;
    std::map<Key, std::vector<const LinearExtrusionTarget*>> groups;
    for (const auto& target : targets)
    {
        if (auto checked = work(1, target.use); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        groups[{ target.geometry, target.use }].push_back(&target);
    }
    std::set<Key> expectedGroups, matchedGroups;
    for (const auto& binding : session.bindings)
    {
        if (auto checked = work(1, binding.use); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const Key key{ binding.geometry, binding.use };
        expectedGroups.insert(key);
        const auto found = groups.find(key);
        const LinearExtrusionTarget* target = nullptr;
        if (found != groups.end())
        {
            for (const auto* candidate : found->second)
            {
                if (candidate->surface == binding.identity)
                {
                    target = candidate;
                    break;
                }
            }
        }
        if (!target)
        {
            if (binding.carrier)
            {
                continue; // Non-driving auxiliary definition, no published native tree.
            }
            return tl::unexpected(
                Failure{ Status::PostcheckFailure,
                         "GeneralLinearExtrusionTargetMissing",
                         "Native target surface identity changed",
                         binding.use,
                         "LinearExtrusion" }
            );
        }
        matchedGroups.insert(key);
        const auto fail = [&](const char* key)
        {
            return tl::unexpected(
                Failure{ Status::PostcheckFailure, key, "Certified stored extrusion definition changed", binding.use, "LinearExtrusion" }
            );
        };
        const auto* surface = dynamic_cast<const SmBSplineSurface*>(target->surface);
        if (!surface || surface->GetType() != SmBSplineSurface_TYPE || surface->GetContext() != &context)
        {
            return fail("GeneralLinearExtrusionTargetClass");
        }
        if (auto checked = native(binding.use); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        ULONG nu = 0, nv = 0;
        double* data = nullptr;
        if (auto checked = CheckNativeStatus(surface->GetControlPointsPointer(nu, nv, data), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        const auto& expected = binding.expected;
        if (nu != expected.basis.countU || nv != expected.basis.countV || !data || surface->GetDegree(SM_SP_U) != expected.basis.degreeU ||
            surface->GetDegree(SM_SP_V) != expected.basis.degreeV)
        {
            return fail("GeneralLinearExtrusionTargetNet");
        }
        for (std::size_t i = 0; i < expected.homogeneous.size(); ++i)
        {
            if (auto checked = work(4, binding.use); !checked)
            {
                return tl::unexpected(std::move(checked.error()));
            }
            for (unsigned k = 0; k < 4; ++k)
            {
                if ((k == 3 && !surface->IsRational() ? 1. : data[4 * i + k]) != expected.homogeneous[i][k])
                {
                    return fail("GeneralLinearExtrusionTargetNet");
                }
            }
        }
        ULONG ku = 0, kv = 0;
        double *u = nullptr, *v = nullptr;
        if (auto checked = native(binding.use); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto checked = CheckNativeStatus(surface->GetKnotsPointers(ku, kv, u, v), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto checked = work(std::size_t(ku) + kv, binding.use); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (ku != expected.basis.knotsU.size() || kv != expected.basis.knotsV.size() || !u || !v ||
            !std::equal(u, u + ku, expected.basis.knotsU.begin()) || !std::equal(v, v + kv, expected.basis.knotsV.begin()))
        {
            return fail("GeneralLinearExtrusionTargetKnots");
        }
        auto mapping = binding.mapping;
        mapping.targetFaceIndex = target->face;
        evidence.push_back(std::move(mapping));
    }
    // A selected carrier whose object identity changed cannot be mistaken for
    // a retired auxiliary: its group still exists in the final source inventory.
    for (const auto& [key, group] : groups)
    {
        if (expectedGroups.contains(key) && !matchedGroups.contains(key))
        {
            return tl::unexpected(
                Failure{ Status::PostcheckFailure,
                         "GeneralLinearExtrusionSelectedCarrierChanged",
                         "Selected source carrier no longer owns the certified definition",
                         key.second,
                         "LinearExtrusion" }
            );
        }
    }
    return evidence;
}
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
Result<std::optional<double>> BoundLinearExtrusionSeam(
    const SmSurface& surface,
    const std::array<Point, 2>& endpoints,
    Fraction maximumMm,
    const std::function<Result<void>()>& native,
    const std::function<Result<void>(std::size_t)>& work,
    const SmCurve* actualCut
)
{
    const auto* spline = dynamic_cast<const SmBSplineSurface*>(&surface);
    if (!spline || !native || !work || !SurfaceProofNumericEnvironment() || maximumMm.Numerator() <= 0 || spline->GetDegree(SM_SP_V) != 1)
    {
        return std::optional<double>{};
    }
    if (auto checked = native(); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    ULONG nu = 0, nv = 0;
    double* cp = nullptr;
    if (auto checked = CheckNativeStatus(spline->GetControlPointsPointer(nu, nv, cp), NativeStatusOrigin::GeneralConstruction); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    if (auto checked = native(); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    ULONG ku = 0, kv = 0;
    double* u = nullptr;
    double* v = nullptr;
    if (auto checked = CheckNativeStatus(spline->GetKnotsPointers(ku, kv, u, v), NativeStatusOrigin::GeneralConstruction); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    const auto degree = spline->GetDegree(SM_SP_U);
    if (!cp || !u || !v || nv != 2 || nu <= degree || std::uint64_t(nu) + degree + 1 != ku || kv != 4 || v[0] != v[1] || v[2] != v[3] ||
        !(v[1] < v[2]))
    {
        return std::optional<double>{};
    }
    if (auto checked = work(std::size_t(degree) * 2 + 96); !checked)
    {
        return tl::unexpected(std::move(checked.error()));
    }
    for (ULONG i = 0; i <= degree; ++i)
    {
        if (u[i] != u[degree] || u[nu + i] != u[nu])
        {
            return std::optional<double>{};
        }
    }
    std::array<Point, 2> cutEndpoints = endpoints;
    if (actualCut)
    {
        const auto* line = dynamic_cast<const SmLine*>(actualCut);
        if (!line || line->IsRational())
        {
            return std::optional<double>{};
        }
        if (auto checked = native(); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        ULONG count = 0;
        double* data = nullptr;
        if (auto checked = CheckNativeStatus(line->GetControlPointsPointer(count, data), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (auto checked = native(); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        SmTArray<double> knots;
        if (auto checked = CheckNativeStatus(line->GetKnotsAll(knots), NativeStatusOrigin::GeneralConstruction); !checked)
        {
            return tl::unexpected(std::move(checked.error()));
        }
        if (count != 2 || !data || knots.GetSize() != 4 || knots[0] != 0 || knots[1] != 0 || knots[2] != 1 || knots[3] != 1)
        {
            return std::optional<double>{};
        }
        for (std::size_t i = 0; i < 2; ++i)
        {
            for (std::size_t k = 0; k < 3; ++k)
            {
                cutEndpoints[i][k] = data[4 * i + k];
            }
        }
    }
    // A clamped U end selects exactly its end control row. Equal positive
    // weights along degree-one V make BOTH end rows affine in the SAME scalar
    // V. A straight cut between A,B and either boundary at affine V(A),V(B)
    // therefore has an affine residual: the maximum endpoint norm encloses
    // the WHOLE cut. This is not an endpoint-sampling argument for a spline.
    // Only arithmetic failures become absence. Callbacks, allocation and native
    // failures stay outside this boundary and retain their original contracts.
    try
    {
        std::array<std::array<IntervalPoint, 2>, 2> sides;
        for (std::size_t side = 0; side < 2; ++side)
        {
            const auto index = std::size_t(side ? nu - 1 : 0) * 8;
            const double w = spline->IsRational() ? cp[index + 3] : 1.;
            if (!(w > 0) || !std::isfinite(w) || (spline->IsRational() && cp[index + 7] != w))
            {
                return std::optional<double>{};
            }
            for (std::size_t row = 0; row < 2; ++row)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    sides[side][row][k] = Exact(cp[index + row * 4 + k]) / Exact(w);
                }
            }
        }
        const auto direction = Subtract(sides[0][1], sides[0][0]);
        const auto squared = Dot(direction, direction);
        if (!(squared.low > 0))
        {
            return std::optional<double>{};
        }
        std::array<double, 2> fractions;
        double bound = 0;
        for (std::size_t endpoint = 0; endpoint < 2; ++endpoint)
        {
            const auto projected = Dot(Subtract(Exact(endpoints[endpoint]), sides[0][0]), direction) / squared;
            // Choose a finite witness, then verify it by outward arithmetic.
            // The midpoint is NOT claimed to be an exact closest parameter.
            const double fraction = projected.low * .5 + projected.high * .5;
            if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
            {
                return std::optional<double>{};
            }
            fractions[endpoint] = fraction;
            for (const auto& side : sides)
            {
                const auto at = Add(Times(side[0], Exact(1) - Exact(fraction)), Times(side[1], Exact(fraction)));
                bound = std::max(bound, NormUpper(Subtract(Exact(cutEndpoints[endpoint]), at)));
            }
        }
        if (fractions[0] == fractions[1] || bound > Enclose(maximumMm).low)
        {
            return std::optional<double>{};
        }
        return std::optional<double>{ bound };
    }
    catch (const std::overflow_error&)
    {
        return std::optional<double>{};
    }
    catch (const std::domain_error&)
    {
        return std::optional<double>{};
    }
}
} // namespace step_import::detail::construction
