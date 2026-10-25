// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportTrimmedSplineConstruction.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportInterval.h"
#include "StepImportTerminalSpline.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <algorithm>
#include <cmath>

namespace step_import::detail::construction
{
namespace
{
namespace ts = terminal_spline;
class Builder : private GeometryConstruction
{
public:

    explicit Builder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }
    Result<TrimmedSplineDefinition> Make(Index record)
    {
        if (!m_context || !m_report.units || m_report.units->lengthToMm.Numerator() <= 0)
        {
            return tl::unexpected(Fail("GeneralWireSplineConstructionContextMissing", record, Status::BuildFailure));
        }
        if (!Is(record, "TRIMMED_CURVE") || !Is(Ref(record, "basis_curve"), "B_SPLINE_CURVE_WITH_KNOTS"))
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimBasisUnsupported", record, Status::Unsupported));
        }
        const auto& preference = F(record, "master_representation").text;
        if (preference != "PARAMETER" && preference != "UNSPECIFIED")
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimPreferenceUnsupported", record, Status::Unsupported));
        }
        auto first = ReadCut(record, "trim_1");
        if (!first)
        {
            return tl::unexpected(std::move(first.error()));
        }
        auto last = ReadCut(record, "trim_2");
        if (!last)
        {
            return tl::unexpected(std::move(last.error()));
        }
        const bool forward = B(record, "sense_agreement");
        if (!(forward ? last->parameter > first->parameter : first->parameter > last->parameter))
        {
            // This finite, nonperiodic range never wraps, snaps or silently
            // collapses two distinct source decimals to one binary endpoint.
            return tl::unexpected(Fail("GeneralWireSplineTrimOrderUnestablished", record, Status::Indeterminate));
        }
        const std::array<Cut, 2> cuts{ std::move(*first), std::move(*last) };
        const auto low = forward ? cuts[0].parameter : cuts[1].parameter;
        const auto high = forward ? cuts[1].parameter : cuts[0].parameter;
        const std::array<std::string, 2> sourceRange{ forward ? cuts[0].lexeme : cuts[1].lexeme, forward ? cuts[1].lexeme : cuts[0].lexeme };
        const ts::Limits limits{ m_options.sourceLimits.values, 4096, 1234 };
        const ts::Charge charge = [&](std::size_t units)
        {
            return m_execution.SpendExactGeometryWork(m_report, units, record);
        };
        const auto basis = Ref(record, "basis_curve");
        auto source = Source(basis, record, limits, charge);
        if (!source)
        {
            return tl::unexpected(std::move(source.error()));
        }
        auto prepared = ts::Extend(*source, limits, charge);
        if (!prepared)
        {
            return MathFailure(std::move(prepared.error()), record);
        }
        // The shared source factory establishes the complete basis first. The
        // target below is a private parameter-preserving restriction; no fit,
        // source control rewrite or source endpoint inversion is introduced.
        auto whole = MakeSplineCurve(basis, record);
        if (!whole)
        {
            return tl::unexpected(std::move(whole.error()));
        }
        auto* spline = dynamic_cast<SmBSplineCurve*>(whole->get());
        if (!spline || spline->GetType() != SmBSplineCurve_TYPE || spline->GetContext() != m_context || spline->GetOwner())
        {
            return tl::unexpected(Fail("GeneralWireSplineBasisOwnershipUnestablished", record, Status::Indeterminate));
        }
        const auto wholeDomain = spline->GetNaturalInterval();
        if (low < wholeDomain.GetMin() || high > wholeDomain.GetMax())
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimOutsideNativeBasis", record, Status::Indeterminate));
        }
        TrimmedSplineDefinition result;
        result.binarySourceCuts = { cuts[0].parameter, cuts[1].parameter };
        result.evidence.sourceParameters = { cuts[0].lexeme, cuts[1].lexeme };
        for (std::size_t i = 0; i < cuts.size(); ++i)
        {
            SmPoint3d point;
            if (auto operation = Step(record); !operation)
            {
                return tl::unexpected(std::move(operation.error()));
            }
            if (auto good = Good(spline->EvaluatePoint(cuts[i].parameter, point)); !good)
            {
                return tl::unexpected(std::move(good.error()));
            }
            const auto nativePoint = P(point);
            result.nativeBasisEnds[forward ? i : 1 - i] = nativePoint;
            if (!cuts[i].point)
            {
                continue;
            }
            const auto& coordinates = F(*cuts[i].point, "coordinates").children;
            if (coordinates.size() != 3)
            {
                return tl::unexpected(Fail("GeneralWireSplineTrimPointDimension", *cuts[i].point, Status::Unsupported));
            }
            auto certified = ts::SourceCurvePointDifferenceUpper(
                prepared->reference,
                cuts[i].lexeme,
                { coordinates[0].text, coordinates[1].text, coordinates[2].text },
                source->lengthToMm,
                limits,
                charge
            );
            if (!certified)
            {
                return MathFailure(std::move(certified.error()), record);
            }
            if (!std::isfinite(*certified) || *certified > Enclose(m_options.quality.consistencyMm).low)
            {
                // An L1 upper bound above an Euclidean budget is insufficient
                // evidence, not a proof that the source selector is invalid.
                return tl::unexpected(Fail("GeneralWireSplineTrimSelectorUnestablished", record, Status::Indeterminate));
            }
            result.evidence.sourceConsistencyUpperMm = std::max(result.evidence.sourceConsistencyUpperMm.value_or(0), *certified);
            auto declaredPoint = Position(*cuts[i].point);
            if (!declaredPoint)
            {
                return tl::unexpected(std::move(declaredPoint.error()));
            }
            const auto measured = Norm(Sub(nativePoint, *declaredPoint));
            if (!std::isfinite(measured) || measured > Q(m_options.quality.consistencyMm))
            {
                return tl::unexpected(Fail("GeneralWireSplineTrimMeasurementUnestablished", record, Status::Indeterminate));
            }
            result.measuredSourceResidualMm = std::max(result.measuredSourceResidualMm, measured);
        }
        auto restricted = Construct(*spline, SmExtent1d(low, high), record);
        if (!restricted)
        {
            return tl::unexpected(std::move(restricted.error()));
        }
        const auto actualDomain = (*restricted)->GetNaturalInterval();
        if (actualDomain.GetMin() != low || actualDomain.GetMax() != high)
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimNativeDomainChanged", record, Status::Indeterminate));
        }
        auto actual = Read(**restricted, record, limits);
        if (!actual)
        {
            return tl::unexpected(std::move(actual.error()));
        }
        const auto definition = m_options.quality.definitionMm;
        auto proof = ts::CertifyCurveTrim(
            prepared->reference,
            sourceRange,
            *actual,
            { std::to_string(definition.Numerator()), std::to_string(definition.Denominator()) },
            limits,
            charge
        );
        if (!proof)
        {
            return MathFailure(std::move(proof.error()), record);
        }
        if (!proof->established || !proof->maximumMm || !std::isfinite(*proof->maximumMm))
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimDefinitionUnestablished", record, Status::Indeterminate));
        }
        SmAssertArray findings;
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (!(*restricted)->AssertValid(&findings, SM_LEVEL_2, SM_WALK) || findings.GetSize())
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimNativeValidation", record, Status::PostcheckFailure));
        }
        result.evidence.definitionErrorUpperMm = *proof->maximumMm;
        result.evidence.proofPatches = proof->patches;
        result.curve = std::move(*restricted);
        return result;
    }

private:

    struct Cut
    {
        double parameter = 0;
        std::string lexeme;
        std::optional<Index> point;
    };
    static tl::unexpected<Failure> MathFailure(Failure failure, Index record)
    {
        if (failure.stage == "TerminalSpline" && !failure.record)
        {
            failure.record = record;
        }
        return tl::unexpected(std::move(failure));
    }
    Result<Cut> ReadCut(Index record, const char* name)
    {
        Cut cut;
        bool parameter = false;
        for (const auto& value : F(record, name).children)
        {
            if (value.kind == step_source::ValueKind::Reference)
            {
                cut.point = m_source->ReferenceTarget(value);
            }
            else if (value.children.size() == 1)
            {
                if (value.children[0].text.size() > 1234)
                {
                    return tl::unexpected(Fail("GeneralWireSplineTrimParameterCapacity", record, Status::ResourceLimit));
                }
                auto number = Number(value.children[0]);
                if (!number)
                {
                    return tl::unexpected(std::move(number.error()));
                }
                cut.parameter = *number;
                cut.lexeme = value.children[0].text;
                parameter = true;
            }
        }
        if (!parameter)
        {
            return tl::unexpected(Fail("GeneralWireSplineParameterTrimRequired", record, Status::Unsupported));
        }
        return cut;
    }
    Result<ts::Source> Source(Index basis, Index use, const ts::Limits& limits, const ts::Charge& charge)
    {
        ts::Source source;
        source.lengthToMm = { std::to_string(m_report.units->lengthToMm.Numerator()), std::to_string(m_report.units->lengthToMm.Denominator()) };
        auto degree = Degree(basis, "degree");
        if (!degree)
        {
            return tl::unexpected(std::move(degree.error()));
        }
        source.u.degree = *degree;
        auto multiplicities = Multiplicities(basis, "knot_multiplicities");
        if (!multiplicities)
        {
            return tl::unexpected(std::move(multiplicities.error()));
        }
        for (ULONG i = 0; i < multiplicities->GetSize(); ++i)
        {
            source.u.multiplicities.push_back((*multiplicities)[i]);
        }
        for (const auto& knot : F(basis, "knots").children)
        {
            if (auto spent = charge(1); !spent)
            {
                return tl::unexpected(std::move(spent.error()));
            }
            source.u.knots.push_back(knot.text);
        }
        const auto controls = Refs(basis, "control_points_list");
        source.countU = controls.size();
        if (source.countU > limits.controlSlots)
        {
            return tl::unexpected(Fail("GeneralWireSplineSourceControlLimit", use, Status::ResourceLimit));
        }
        if (auto spent = charge(source.countU); !spent)
        {
            return tl::unexpected(std::move(spent.error()));
        }
        source.controls.reserve(source.countU);
        for (const auto control : controls)
        {
            const auto& coordinates = F(control, "coordinates").children;
            if (coordinates.size() != 3)
            {
                return tl::unexpected(Fail("GeneralWireSplineControlDimension", control, Status::Unsupported));
            }
            source.controls.push_back({ coordinates[0].text, coordinates[1].text, coordinates[2].text });
        }
        if (Is(basis, "RATIONAL_B_SPLINE_CURVE"))
        {
            for (const auto& weight : F(basis, "weights_data").children)
            {
                if (auto spent = charge(1); !spent)
                {
                    return tl::unexpected(std::move(spent.error()));
                }
                source.weights.push_back(weight.text);
            }
        }
        return source;
    }
    Result<std::unique_ptr<SmBSplineCurve>> Construct(const SmBSplineCurve& basis, SmExtent1d interval, Index record)
    {
        SmTArray<SmPoint3d> points;
        SmTArray<double> weights, expanded, knots;
        SmTArray<ULONG> multiplicities;
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto good = Good(basis.GetControlPolygon(points, weights)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto good = Good(basis.GetKnotsAll(expanded)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (auto spent = m_execution.SpendExactGeometryWork(m_report, expanded.GetSize(), record); !spent)
        {
            return tl::unexpected(std::move(spent.error()));
        }
        for (const auto value : expanded)
        {
            if (!knots.GetSize() || knots[knots.GetSize() - 1] != value)
            {
                knots.Add(value);
                multiplicities.Add(1);
            }
            else
            {
                ++multiplicities[multiplicities.GetSize() - 1];
            }
        }
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        SmBSplineCurve* raw = nullptr;
        // CreateCanonical's explicit interval uses N_IgesClampCrv's knot
        // insertion path. SmBSplineCurve::Trim would snap near-knot endpoints.
        // Euclidean reconstruction/native canonicalization can still round:
        // CertifyCurveTrim reads the final raw homogeneous net and bounds ALL
        // resulting error instead of assuming this operation is exact.
        const auto status = SmBSplineCurve::CreateCanonical(
            *m_context,
            3,
            basis.GetDegree(),
            points,
            SM_CF_UNSPECIFIED,
            multiplicities,
            knots,
            SM_KT_UNSPECIFIED,
            basis.IsRational() ? &weights : nullptr,
            &interval,
            raw
        );
        std::unique_ptr<SmBSplineCurve> result(raw);
        if (auto good = Good(status); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (!result || result->GetContext() != m_context || result->GetOwner())
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimOwnershipUnestablished", record, Status::Indeterminate));
        }
        return result;
    }
    Result<ts::BinaryNet> Read(SmBSplineCurve& curve, Index record, const ts::Limits& limits)
    {
        ULONG count = 0;
        double* controls = nullptr;
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto good = Good(curve.GetControlPointsPointer(count, controls)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (count > limits.controlSlots)
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimControlLimit", record, Status::ResourceLimit));
        }
        if (!controls || !count || curve.GetDim() != 3)
        {
            return tl::unexpected(Fail("GeneralWireSplineTrimActualNet", record, Status::Indeterminate));
        }
        ts::BinaryNet result;
        result.basis = { curve.GetDegree(), 0, count, 1, {}, {} };
        SmTArray<double> knots;
        if (auto operation = Step(record); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        if (auto good = Good(curve.GetKnotsAll(knots)); !good)
        {
            return tl::unexpected(std::move(good.error()));
        }
        if (auto spent = m_execution.SpendExactGeometryWork(m_report, count, record); !spent)
        {
            return tl::unexpected(std::move(spent.error()));
        }
        result.basis.knotsU.assign(knots.GetDataArray(), knots.GetDataArray() + knots.GetSize());
        result.homogeneous.reserve(count);
        for (ULONG i = 0; i < count; ++i)
        {
            result.homogeneous.push_back({ controls[4 * i], controls[4 * i + 1], controls[4 * i + 2], curve.IsRational() ? controls[4 * i + 3] : 1. });
        }
        return result;
    }
};
} // namespace
Result<TrimmedSplineDefinition> MakeTrimmedSplineDefinition(const GeometryConstruction& inputs, std::size_t record)
{
    return Builder(inputs).Make(record);
}
} // namespace step_import::detail::construction
