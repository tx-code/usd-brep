// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportPcurveBoundaryCurve.h"

#include "StepImportGeometryConstruction.h"
#include "StepImportPcurveLineWindow.h"
#include "StepImportProofEnvironment.h"
#include "StepImportTerminalSpline.h"

#include <SmBSplineSurface.h>
#include <bit>
#include <cstdint>
#include <nurbs.h>

namespace step_import::detail
{
namespace
{
namespace ts = terminal_spline;
auto Refuse(const char* key, Status status = Status::Indeterminate)
{
    return tl::unexpected(Failure{ status, key, "Source boundary-curve conversion evidence is unestablished", {}, "PcurveBoundaryCurve" });
}
#define BC_CHECK(expression)                                                                                                                         \
    do                                                                                                                                               \
    {                                                                                                                                                \
        auto checked = (expression);                                                                                                                 \
        if (!checked)                                                                                                                                \
            return tl::unexpected(std::move(checked.error()));                                                                                       \
    } while (false)
#define BC_VALUE(name, expression)                                                                                                                   \
    auto name = (expression);                                                                                                                        \
    if (!name)                                                                                                                                       \
    return tl::unexpected(std::move(name.error()))
Result<void> Work(const SplineChartCallbacks& cb, std::size_t work)
{
    if (!cb.chargeWork || !cb.beforeNativeCall)
    {
        return Refuse("PcurveBoundaryCallbacksRequired");
    }
    BC_CHECK(cb.chargeWork(work));
    if (!SurfaceProofNumericEnvironment())
    {
        return Refuse("PcurveBoundaryNumericEnvironment");
    }
    return {};
}
Result<void> Native(const SplineChartCallbacks& cb)
{
    BC_CHECK(Work(cb, 1));
    BC_CHECK(cb.beforeNativeCall());
    if (!SurfaceProofNumericEnvironment())
    {
        return Refuse("PcurveBoundaryNumericEnvironment");
    }
    return {};
}
Result<ts::BinaryNet> ReadBoundary(const SmSurface& object, unsigned fixedAxis, unsigned fixedEnd, const SplineChartCallbacks& cb)
{
    BC_CHECK(Native(cb));
    if (object.GetType() != SmBSplineSurface_TYPE || fixedAxis > 1 || fixedEnd > 1)
    {
        return Refuse("PcurveBoundaryCarrierType");
    }
    const auto& surface = static_cast<const SmBSplineSurface&>(object);
    BC_CHECK(Native(cb));
    if (surface.IsNurbBorrowed())
    {
        return Refuse("PcurveBoundaryCarrierBorrowed");
    }
    ULONG nu = 0, nv = 0, ku = 0, kv = 0;
    double *net = nullptr, *u = nullptr, *v = nullptr;
    BC_CHECK(Native(cb));
    BC_CHECK(CheckNativeStatus(surface.GetControlPointsPointer(nu, nv, net), NativeStatusOrigin::GeneralConstruction));
    BC_CHECK(Native(cb));
    BC_CHECK(CheckNativeStatus(surface.GetKnotsPointers(ku, kv, u, v), NativeStatusOrigin::GeneralConstruction));
    if (!net || !u || !v || !nu || !nv || nu > 4096 / nv || !ku || !kv || ku > 8192 || kv > 8192 - ku)
    {
        return Refuse("PcurveBoundaryCarrierCapacity", Status::ResourceLimit);
    }
    BC_CHECK(Native(cb));
    const bool rational = surface.IsRational() != FALSE;
    BC_CHECK(Native(cb));
    const auto degree = surface.GetDegree(fixedAxis ? SM_SP_U : SM_SP_V);
    const auto count = fixedAxis ? nu : nv;
    const auto knotCount = fixedAxis ? ku : kv;
    const auto* knots = fixedAxis ? u : v;
    if (!degree || degree > 17 || count <= degree || knotCount != count + degree + 1)
    {
        return Refuse("PcurveBoundaryCarrierStructure");
    }
    ts::BinaryNet result;
    result.basis.degreeU = degree;
    result.basis.countU = count;
    BC_CHECK(Work(cb, knotCount + std::size_t(count) * 4));
    result.basis.knotsU.assign(knots, knots + knotCount);
    for (ULONG i = 0; i < count; ++i)
    {
        // Native/source tensors use U-major, V-minor layout. A fixed V takes
        // a column, a fixed U a row. These are the DEFINING stored homogeneous
        // values, never GetIsoCurve's floating calculation or a fitted cache.
        const std::size_t at = fixedAxis ? std::size_t(i) * nv + (fixedEnd ? nv - 1 : 0) : std::size_t(fixedEnd ? nu - 1 : 0) * nv + i;
        const double weight = rational ? net[4 * at + 3] : 1.;
        if (!(weight > 0) || !std::isfinite(weight) || (!rational && net[4 * at + 3] != NL_NOW))
        {
            return Refuse("PcurveBoundaryCarrierWeight");
        }
        const std::array<double, 4> h{ net[4 * at], net[4 * at + 1], net[4 * at + 2], weight };
        if (!std::all_of(
                h.begin(),
                h.end(),
                [](double x)
                {
                    return std::isfinite(x);
                }
            ))
        {
            return Refuse("PcurveBoundaryCarrierCoefficient");
        }
        result.homogeneous.push_back(h);
    }
    return result;
}
Result<bool> Same(const ts::BinaryNet& a, const ts::BinaryNet& b, const SplineChartCallbacks& cb)
{
    if (a.basis.degreeU != b.basis.degreeU || a.basis.countU != b.basis.countU || a.basis.knotsU.size() != b.basis.knotsU.size() ||
        a.homogeneous.size() != b.homogeneous.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < a.basis.knotsU.size(); ++i)
    {
        BC_CHECK(Work(cb, 1));
        if (std::bit_cast<std::uint64_t>(a.basis.knotsU[i]) != std::bit_cast<std::uint64_t>(b.basis.knotsU[i]))
        {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.homogeneous.size(); ++i)
    {
        for (std::size_t k = 0; k < 4; ++k)
        {
            BC_CHECK(Work(cb, 1));
            if (std::bit_cast<std::uint64_t>(a.homogeneous[i][k]) != std::bit_cast<std::uint64_t>(b.homogeneous[i][k]))
            {
                return false;
            }
        }
    }
    return true;
}
} // namespace
struct PcurveBoundaryCurveData
{
    unsigned fixedAxis = 0, fixedEnd = 0;
    ts::BinaryNet actual;
    std::shared_ptr<const ts::Reference> reference;
    std::array<std::string, 2> sourceCuts;
    double maximumMm = 0;
    std::size_t patches = 0;
};
struct PcurveBoundaryCurveAccess
{
    static std::shared_ptr<const PcurveBoundaryCurveWitness> Make(std::shared_ptr<const PcurveBoundaryCurveData> data)
    {
        return std::shared_ptr<const PcurveBoundaryCurveWitness>(new PcurveBoundaryCurveWitness(std::move(data)));
    }
};
PcurveBoundaryCurveWitness::PcurveBoundaryCurveWitness(std::shared_ptr<const PcurveBoundaryCurveData> data) : m_data(std::move(data))
{
}
unsigned PcurveBoundaryCurveWitness::FixedAxis() const
{
    return m_data->fixedAxis;
}
unsigned PcurveBoundaryCurveWitness::FixedEnd() const
{
    return m_data->fixedEnd;
}
double PcurveBoundaryCurveWitness::MaximumMm() const
{
    return m_data->maximumMm;
}
std::size_t PcurveBoundaryCurveWitness::Patches() const
{
    return m_data->patches;
}
std::array<std::string, 2> PcurveBoundaryCurveWitness::SourceInterval() const
{
    return m_data->sourceCuts;
}
std::array<double, 2> PcurveBoundaryCurveWitness::NativeInterval() const
{
    return { m_data->actual.basis.knotsU.front(), m_data->actual.basis.knotsU.back() };
}
Result<void> PcurveBoundaryCurveWitness::Verify(const SmSurface& surface, const SplineChartCallbacks& cb) const
{
    BC_VALUE(current, ReadBoundary(surface, m_data->fixedAxis, m_data->fixedEnd, cb));
    BC_VALUE(same, Same(m_data->actual, *current, cb));
    if (!*same)
    {
        return Refuse("PcurveBoundaryDefinitionChanged", Status::PostcheckFailure);
    }
    return {};
}
namespace construction
{
namespace
{
class Builder : private GeometryConstruction
{
public:

    explicit Builder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }
    Result<std::shared_ptr<const PcurveBoundaryCurveWitness>> Make(
        Index pcurve,
        Index use,
        const PendingBoundaryPcurveLineWindow& pending,
        const SmSurface& surface,
        const SplineChartCallbacks& cb
    )
    {
        const auto& window = pending.ProposedWindow();
        if (!m_context || !m_report.units || m_report.units->lengthToMm.Numerator() <= 0)
        {
            return tl::unexpected(Fail("GeneralPcurveBoundaryConstructionContextMissing", pcurve, Status::BuildFailure));
        }
        BC_CHECK(Native(cb));
        if (surface.GetContext() != m_context)
        {
            return tl::unexpected(Fail("GeneralPcurveBoundaryContextMismatch", pcurve));
        }
        if (window.maximumRoundingUpperUv != std::array<double, 2>{ 0, 0 } || window.hasUvRounding)
        {
            return tl::unexpected(Fail("GeneralPcurveBoundaryZeroUvErrorRequired", pcurve, Status::Indeterminate));
        }
        const auto fixedAxis = unsigned(pending.FixedAxis()), fixedEnd = pending.FixedEnd();
        if (fixedAxis > 1 || fixedEnd > 1)
        {
            return tl::unexpected(Fail("GeneralPcurveBoundaryAxis", pcurve));
        }
        const auto basis = Ref(pcurve, "basis_surface");
        const auto& rows = F(basis, "control_points_list").children;
        if (rows.empty() || rows[0].children.empty() || rows.size() > 4096 / rows[0].children.size())
        {
            return tl::unexpected(Fail("GeneralPcurveBoundarySourceCapacity", basis, Status::ResourceLimit));
        }
        const auto nu = rows.size(), nv = rows[0].children.size();
        const auto count = fixedAxis ? nu : nv;
        const ts::Limits limits{ m_options.sourceLimits.values, 4096, 1234 };
        const ts::Charge charge = [&](std::size_t work) -> Result<void>
        {
            BC_CHECK(m_execution.SpendExactGeometryWork(m_report, work, use));
            if (!SurfaceProofNumericEnvironment())
            {
                return Refuse("PcurveBoundaryNumericEnvironment");
            }
            return {};
        };
        const auto text = [&](const step_source::Value& value) -> Result<std::string>
        {
            if (value.text.size() > limits.tokenCharacters)
            {
                return tl::unexpected(Fail("GeneralPcurveBoundarySourceTokenCapacity", basis, Status::ResourceLimit));
            }
            BC_CHECK(charge(value.text.size() + 1));
            return value.text;
        };
        BC_CHECK(charge(count * 4));
        ts::Source source;
        source.countU = count;
        source.lengthToMm = { std::to_string(m_report.units->lengthToMm.Numerator()), std::to_string(m_report.units->lengthToMm.Denominator()) };
        auto degree = Degree(basis, fixedAxis ? "u_degree" : "v_degree");
        if (!degree)
        {
            return tl::unexpected(std::move(degree.error()));
        }
        source.u.degree = *degree;
        auto multiplicities = Multiplicities(basis, fixedAxis ? "u_multiplicities" : "v_multiplicities");
        if (!multiplicities)
        {
            return tl::unexpected(std::move(multiplicities.error()));
        }
        for (ULONG i = 0; i < multiplicities->GetSize(); ++i)
        {
            source.u.multiplicities.push_back((*multiplicities)[i]);
        }
        for (const auto& knot : F(basis, fixedAxis ? "u_knots" : "v_knots").children)
        {
            BC_VALUE(token, text(knot));
            source.u.knots.push_back(std::move(*token));
        }
        const bool rational = Is(basis, "RATIONAL_B_SPLINE_SURFACE");
        for (std::size_t k = 0; k < count; ++k)
        {
            const auto i = fixedAxis ? k : (fixedEnd ? nu - 1 : 0);
            const auto j = fixedAxis ? (fixedEnd ? nv - 1 : 0) : k;
            if (rows[i].children.size() != nv)
            {
                return tl::unexpected(Fail("GeneralPcurveBoundarySourceShape", basis));
            }
            const auto point = m_source->ReferenceTarget(rows[i].children[j]);
            const auto& xyz = F(point, "coordinates").children;
            if (xyz.size() != 3)
            {
                return tl::unexpected(Fail("GeneralPcurveBoundarySourceDimension", point));
            }
            BC_VALUE(x, text(xyz[0]));
            BC_VALUE(y, text(xyz[1]));
            BC_VALUE(z, text(xyz[2]));
            source.controls.push_back({ std::move(*x), std::move(*y), std::move(*z) });
            if (rational)
            {
                BC_VALUE(weight, text(F(basis, "weights_data").children.at(i).children.at(j)));
                source.weights.push_back(std::move(*weight));
            }
        }
        auto data = std::make_shared<PcurveBoundaryCurveData>();
        data->fixedAxis = fixedAxis;
        data->fixedEnd = fixedEnd;
        data->sourceCuts = { source.u.knots.front(), source.u.knots.back() };
        BC_VALUE(actual, ReadBoundary(surface, fixedAxis, fixedEnd, cb));
        data->actual = std::move(*actual);
        if (data->actual.basis.degreeU != source.u.degree || data->actual.basis.countU != source.countU)
        {
            return tl::unexpected(Fail("GeneralPcurveBoundaryDegreeOrCountChanged", basis));
        }
        BC_VALUE(reference, ts::CaptureOriginalReference(source, limits, charge));
        data->reference = std::move(*reference);
        // The whole-boundary LINE has exact stored UV endpoint values, so
        // Elift=0. This single source-boundary certificate spends the original
        // definitionMm ONCE, including original coordinates, units and knots.
        const auto allowance = m_options.quality.definitionMm;
        BC_VALUE(
            proof,
            ts::CertifyCurveTrim(
                data->reference,
                data->sourceCuts,
                data->actual,
                { std::to_string(allowance.Numerator()), std::to_string(allowance.Denominator()) },
                limits,
                charge
            )
        );
        if (!proof->established || !proof->maximumMm)
        {
            return tl::unexpected(Fail("GeneralPcurveBoundaryDefinitionUnestablished", pcurve, Status::Indeterminate));
        }
        data->maximumMm = *proof->maximumMm;
        data->patches = proof->patches;
        return PcurveBoundaryCurveAccess::Make(std::move(data));
    }
};
} // namespace
Result<std::shared_ptr<const PcurveBoundaryCurveWitness>> CertifySourcePcurveBoundary(
    const GeometryConstruction& inputs,
    std::size_t pcurve,
    std::size_t use,
    const PendingBoundaryPcurveLineWindow& pending,
    const SmSurface& surface,
    const SplineChartCallbacks& callbacks
)
{
    auto result = Builder(inputs).Make(pcurve, use, pending, surface, callbacks);
    if (!result && !result.error().record)
    {
        result.error().record = use;
    }
    return result;
}
} // namespace construction
#undef BC_CHECK
#undef BC_VALUE
} // namespace step_import::detail
