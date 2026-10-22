// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportUnits.h"

#include "StepImportExecution.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <charconv>
#include <gsl/assert>
#include <limits>
#include <stdexcept>
#include <utility>

namespace step_import::detail
{
struct UnitRatioAccess
{
    static ExactUnitRatio Make(std::string numerator, std::string denominator)
    {
        ExactUnitRatio result;
        result.m_numerator = std::move(numerator);
        result.m_denominator = std::move(denominator);
        return result;
    }
};
namespace
{
// This is an exact-arithmetic capacity, not a geometric tolerance or a source
// validity rule. Fixed checked storage bounds every operation and never wraps.
// Canonical contract: STEP_IMPORT/README.md, conversion length normalization.
// Keep the heavyweight dependency in this translation unit for incremental builds.
using Integer = boost::multiprecision::number<
    boost::multiprecision::cpp_int_backend<4096, 4096, boost::multiprecision::signed_magnitude, boost::multiprecision::checked, void>>;
constexpr std::int64_t DecimalPowerCapacity = 1233; // 10^1233 fits 4096 magnitude bits.
struct WideRatio
{
    Integer numerator = 0, denominator = 1;
};
[[noreturn]] void Capacity()
{
    throw std::overflow_error("ExactUnitArithmeticCapacityExceeded");
}
Integer Gcd(Integer a, Integer b, const std::function<void()>& boundary)
{
    if (a < 0)
    {
        a = -a;
    }
    while (b != 0)
    {
        boundary();
        Integer remainder = a % b;
        a = std::move(b);
        b = std::move(remainder);
    }
    return a;
}
WideRatio Decimal(std::string_view text, const std::function<void()>& boundary)
{
    // Input is a lexically validated source REAL. Preserve the original token in
    // the source owner; parsing here establishes only an exact physical ratio.
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+'))
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto exponentAt = text.find_first_of("Ee");
    const auto mantissa = text.substr(0, exponentAt);
    std::int64_t exponent = 0;
    if (exponentAt != std::string_view::npos)
    {
        auto power = text.substr(exponentAt + 1);
        if (!power.empty() && power.front() == '+')
        {
            power.remove_prefix(1);
        }
        const auto parsed = std::from_chars(power.data(), power.data() + power.size(), exponent);
        if (parsed.ec != std::errc{} || parsed.ptr != power.data() + power.size())
        {
            Capacity();
        }
    }
    std::string digits;
    digits.reserve(mantissa.size());
    std::int64_t fractional = 0;
    bool dot = false;
    for (std::size_t i = 0; i < mantissa.size(); ++i)
    {
        if (i % 256 == 0)
        {
            boundary();
        }
        const char c = mantissa[i];
        if (c == '.')
        {
            dot = true;
            continue;
        }
        Expects(c >= '0' && c <= '9');
        digits += c;
        fractional += dot ? 1 : 0;
    }
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos)
    {
        return {};
    }
    const auto last = digits.find_last_not_of('0');
    const auto trailing = static_cast<std::int64_t>(digits.size() - last - 1);
    // Mantissa length is bounded by the source capture. Bound the exponent
    // before subtracting its fractional digits, including hostile huge powers.
    const auto margin = static_cast<std::int64_t>(digits.size()) + DecimalPowerCapacity;
    if (exponent < -margin || exponent > margin)
    {
        Capacity();
    }
    exponent = exponent - fractional + trailing;
    if (exponent < -DecimalPowerCapacity || exponent > DecimalPowerCapacity || last - first + 1 > 1234)
    {
        Capacity();
    }
    WideRatio result;
    for (auto i = first; i <= last; ++i)
    {
        if ((i - first) % 64 == 0)
        {
            boundary();
        }
        result.numerator *= 10;
        result.numerator += digits[i] - '0';
    }
    auto& expanded = exponent >= 0 ? result.numerator : result.denominator;
    for (std::int64_t i = 0; i < (exponent >= 0 ? exponent : -exponent); ++i)
    {
        if (i % 64 == 0)
        {
            boundary();
        }
        expanded *= 10;
    }
    const auto divisor = Gcd(result.numerator, result.denominator, boundary);
    result.numerator /= divisor;
    result.denominator /= divisor;
    if (negative)
    {
        result.numerator = -result.numerator;
    }
    return result;
}
WideRatio Multiply(const WideRatio& a, const WideRatio& b, const std::function<void()>& boundary)
{
    const auto x = Gcd(a.numerator, b.denominator, boundary);
    const auto y = Gcd(b.numerator, a.denominator, boundary);
    // Cross-cancel before multiplying so representable products do not fail
    // just because their unreduced cross-products exceed the fixed capacity.
    return { (a.numerator / x) * (b.numerator / y), (a.denominator / y) * (b.denominator / x) };
}
ExactUnitRatio Evidence(const WideRatio& value)
{
    return UnitRatioAccess::Make(value.numerator.str(), value.denominator.str());
}
WideRatio ReadEvidence(const ExactUnitRatio& value)
{
    return { Integer(std::string(value.Numerator())), Integer(std::string(value.Denominator())) };
}
Fraction NativeRatio(const WideRatio& value)
{
    const Integer maximum = std::numeric_limits<std::int64_t>::max();
    if (value.numerator <= 0 || value.numerator > maximum || value.denominator <= 0 || value.denominator > maximum)
    {
        Capacity();
    }
    return { value.numerator.convert_to<std::int64_t>(), value.denominator.convert_to<std::int64_t>() };
}
[[noreturn]] void Refuse(Status status, const char* key, std::size_t record, const char* message)
{
    throw Failure{ status, key, message, record, "GeneralBrep" };
}
} // namespace
ResolvedLengthScale ResolveLengthScale(const step_semantics::Snapshot& source, std::size_t root, const std::function<void()>& boundary)
{
    Expects(bool(boundary));
    boundary();
    const auto count = source.Source().Records().size();
    Expects(root < count);
    // Dense source indices bound lookup and cycle bookkeeping by the captured
    // graph. Avoid O(chain length * all SI definitions), recursion, and label-
    // based selection. SI entries are borrowed from the owning source snapshot.
    std::vector<const step_semantics::SiUnit*> si(count, nullptr);
    for (const auto& definition : source.UnitContext().siDefinitions)
    {
        boundary();
        Expects(definition.recordIndex < count);
        si[definition.recordIndex] = &definition;
    }
    std::vector<unsigned char> active(count);
    std::vector<ConversionUnitDefinition> path;
    auto unit = root;
    ResolvedLengthScale result;
    WideRatio scale;
    try
    {
        for (;;)
        {
            boundary();
            Expects(unit < count);
            if (active[unit])
            {
                Refuse(Status::Unsupported, "GeneralCyclicUnitDefinition", unit, "Canonical physical scale requires an acyclic SI-rooted chain");
            }
            if (const auto terminal = si[unit])
            {
                if (terminal->quantity != "Length" || terminal->siName != "METRE")
                {
                    Refuse(Status::Unsupported, "GeneralConversionQuantityUnsupported", unit, "Length scale requires a nominal SI length root");
                }
                scale = Decimal("1E" + std::to_string(terminal->canonicalPower10), boundary);
                break;
            }
            if (!source.IsA(unit, "LENGTH_UNIT") || !source.IsA(unit, "CONVERSION_BASED_UNIT"))
            {
                Refuse(
                    Status::Unsupported,
                    "GeneralConversionBasisUnsupported",
                    unit,
                    "Only nominal conversion length units ending at SI METRE are supported"
                );
            }
            active[unit] = 1;
            const auto factor = source.Field(unit, "conversion_factor").ReferenceTarget();
            boundary();
            const auto& value = source.Field(factor, "value_component").Value();
            const auto base = source.Field(factor, "unit_component").ReferenceTarget();
            // Source WHERE/dimensional checks were already run. This is the
            // additional project prerequisite for a positive, reversible
            // physical length scale, not an invented AP214 positivity clause.
            Expects(value.kind == step_source::ValueKind::Typed && value.children.size() == 1);
            const auto numeric = Decimal(value.children.front().text, boundary);
            if (numeric.numerator <= 0)
            {
                Refuse(Status::Unsupported, "GeneralUnitScaleNotPositive", factor, "Every physical conversion factor must be positive");
            }
            path.push_back({ unit, factor, base, value.text, Evidence(numeric), {} });
            unit = base;
        }
        while (!path.empty())
        {
            boundary();
            auto definition = std::move(path.back());
            path.pop_back();
            unit = definition.unit;
            scale = Multiply(ReadEvidence(definition.factorValue), scale, boundary);
            definition.scaleMm = Evidence(scale);
            result.definitions.push_back(std::move(definition));
        }
        // Source factors and base-to-root observations may be wider than the
        // native representation. Narrow only the final canonical mm ratio;
        // no factor is rounded and no intermediate is applied to geometry.
        unit = root;
        result.scaleMm = NativeRatio(scale);
    }
    catch (const std::overflow_error&)
    {
        // Capacity exhaustion is target numerical evidence not established;
        // never round the source factor or relabel it a source-schema error.
        Refuse(
            Status::Indeterminate,
            "GeneralUnitScaleRange",
            unit,
            "Exact conversion exceeds bounded unit arithmetic or final native ratio capacity"
        );
    }
    boundary();
    return result;
}
} // namespace step_import::detail
