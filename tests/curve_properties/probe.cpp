// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmBSplineCurve.h>
#include <SmCircle.h>
#include <SmContext.h>
#include <SmEllipse.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <nurbs.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void Require(bool condition, const char* stage)
{
    if (!condition)
    {
        throw std::runtime_error(stage);
    }
}
double Read()
{
    double value = 0;
    Require(bool(std::cin >> value) && std::isfinite(value), "finite_input");
    return value;
}
void Point(const SmPoint3d& value)
{
    Require(std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z), "finite_output");
    std::cout << '[' << value.x << ',' << value.y << ',' << value.z << ']';
}
void Interval(const SmExtent1d& value)
{
    std::cout << '[' << value.GetMin() << ',' << value.GetMax() << ']';
}
void Box(const SmExtent3d& value)
{
    std::cout << '[';
    Point(value.GetMin());
    std::cout << ',';
    Point(value.GetMax());
    std::cout << ']';
}
void Evaluate(const SmCurve& curve, double parameter, SmBoolean fromLeft = FALSE)
{
    SmVector3d result[3];
    // FALSE preserves actual zero derivatives; no tolerance-sized substitute tangent.
    Require(curve.Evaluate(parameter, 2, fromLeft, result, FALSE) == SM_SUCCESS, "evaluate");
    std::cout << '[';
    for (int i = 0; i < 3; ++i)
    {
        if (i)
        {
            std::cout << ',';
        }
        Point(result[i]);
    }
    std::cout << ']';
}
SmExtent3d Bounds(const SmCurve& curve, const std::vector<SmExtent1d>& intervals)
{
    SmExtent3d result;
    for (const auto& interval : intervals)
    {
        SmExtent3d part;
        Require(curve.CalculateTightBoundingBox(interval, &part) == SM_SUCCESS, "tight_bounds");
        result.Union(part, result);
    }
    return result;
}
void Run()
{
    std::string version, kind;
    Require(bool(std::cin >> version >> kind) && (version == "curve-properties-v1" || version == "curve-properties-v2"), "protocol");
    const bool advanced = version == "curve-properties-v2";
    const bool periodic = kind == "periodic_nurbs";
    const double first = Read(), last = Read(), lower = Read(), upper = Read();
    const bool periodicWindow = periodic || kind == "circle" || kind == "ellipse";
    Require(last > first && lower >= 0 && lower < 1 && upper <= (periodicWindow ? lower + 1 : 1) && lower < upper, "domain");
    // The context outlives all owning curves; no geometry escapes the worker process.
    SmContext context;
    std::unique_ptr<SmBSplineCurve> curve;
    if (advanced)
    {
        Require(kind == "nurbs" || periodic, "spline_kind");
        const double degreeValue = Read(), countValue = Read(), knotCountValue = Read();
        Require(degreeValue >= 1 && degreeValue <= 3 && degreeValue == std::floor(degreeValue), "degree");
        Require(countValue >= degreeValue + 1 && countValue <= 33 && countValue == std::floor(countValue), "control_count");
        Require(knotCountValue == countValue + degreeValue + 1, "knot_count");
        const auto degree = static_cast<short>(degreeValue);
        std::vector<gw_CPOINT> points;
        for (int i = 0; i < static_cast<int>(countValue); ++i)
        {
            const double x = Read(), y = Read(), z = Read(), weight = Read();
            Require(weight > 0, "positive_weight");
            points.push_back({ x * weight, y * weight, z * weight, weight });
        }
        std::vector<double> knots;
        for (int i = 0; i < static_cast<int>(knotCountValue); ++i)
        {
            const double t = Read();
            Require(t >= first && t <= last && (knots.empty() || t >= knots.back()), "knot_order");
            knots.push_back(t);
        }
        curve.reset(new (context) SmBSplineCurve(
            3,
            static_cast<ULONG>(points.size() - 1),
            static_cast<ULONG>(knots.size() - 1),
            degree,
            points.data(),
            knots.data(),
            &context
        ));
    }
    else if (kind == "bezier")
    {
        const double degreeValue = Read();
        Require(degreeValue >= 1 && degreeValue <= 3 && degreeValue == std::floor(degreeValue), "degree");
        const auto degree = static_cast<short>(degreeValue);
        std::vector<gw_CPOINT> points;
        for (int i = 0; i <= degree; ++i)
        {
            const double x = Read(), y = Read(), z = Read(), weight = Read();
            Require(weight > 0, "positive_weight");
            points.push_back({ x * weight, y * weight, z * weight, weight });
        }
        std::vector<double> knots;
        knots.insert(knots.end(), degree + 1, first);
        knots.insert(knots.end(), degree + 1, last);
        curve.reset(new (context) SmBSplineCurve(3, degree, 2 * degree + 1, degree, points.data(), knots.data(), &context));
    }
    else
    {
        Require(kind == "circle" || kind == "ellipse", "kind");
        const double cx = Read(), cy = Read(), cz = Read();
        const double a = Read(), b = Read(), frameValue = Read();
        Require(a > 0 && b > 0 && (frameValue == 0 || frameValue == 1), "conic_input");
        const SmPoint3d center(cx, cy, cz);
        const SmVector3d x = frameValue == 0 ? SmVector3d(1, 0, 0) : SmVector3d(.6, .8, 0);
        const SmVector3d y = frameValue == 0 ? SmVector3d(0, 1, 0) : SmVector3d(-.48, .36, .8);
        if (kind == "circle")
        {
            curve.reset(new (context) SmCircle(center, x, y, SmExtent1d(0, 360), a, 3, &context));
        }
        else
        {
            curve.reset(new (context) SmEllipse(center, x, y, SmExtent1d(0, 360), a, b, 3, &context));
        }
        Require(curve->EditParameterization(SmExtent1d(first, last)) == SM_SUCCESS, "initial_domain_edit");
    }
    std::vector<double> samples;
    const double countValue = Read();
    Require(countValue >= 2 && countValue <= (advanced ? 256 : 65) && countValue == std::floor(countValue), "sample_count");
    for (int i = 0; i < static_cast<int>(countValue); ++i)
    {
        const double q = Read();
        Require(advanced ? q >= first && q <= last : q >= 0 && q <= 1, "sample_parameter");
        samples.push_back(q);
    }
    std::string extra;
    Require(!(std::cin >> extra), "trailing_input");
    const auto domain = curve->GetNaturalInterval();
    std::vector<SmExtent1d> intervals;
    if (advanced || kind == "bezier")
    {
        intervals.emplace_back(domain.Evaluate(lower), domain.Evaluate(std::min(upper, 1.0)));
        if (upper > 1)
        {
            intervals.emplace_back(domain.GetMin(), domain.Evaluate(upper - 1));
        }
    }
    else
    {
        SmTArray<SmExtent1d> native;
        Require(curve->ConvertIvlFromSTEPToNURBS(SmExtent1d(360 * lower, 360 * upper), native) == SM_SUCCESS, "angular_interval_conversion");
        for (ULONG i = 0; i < native.GetSize(); ++i)
        {
            intervals.push_back(native[i]);
        }
    }
    Require(!intervals.empty(), "nonempty_intervals");
    // Query before editing to exercise any populated bounding/evaluation caches.
    const auto bounds = Bounds(*curve, intervals);
    SmCurve* reversedRaw = nullptr;
    SmCurve* editedRaw = nullptr;
    Require(curve->Copy(context, reversedRaw) == SM_SUCCESS && reversedRaw, "reverse_copy");
    std::unique_ptr<SmCurve> reversed(reversedRaw);
    Require(curve->Copy(context, editedRaw) == SM_SUCCESS && editedRaw, "domain_copy");
    std::unique_ptr<SmCurve> edited(editedRaw);
    SmExtent1d ignored;
    Require(reversed->ReverseParameterization(intervals.front(), ignored) == SM_SUCCESS, "reverse");
    const SmExtent1d editedDomain(11, 11 + 2 * (last - first));
    Require(edited->EditParameterization(editedDomain) == SM_SUCCESS, "edit_domain");
    std::vector<SmExtent1d> reversedIntervals, editedIntervals;
    for (const auto& interval : intervals)
    {
        reversedIntervals.emplace_back(first + last - interval.GetMax(), first + last - interval.GetMin());
        editedIntervals.emplace_back(11 + 2 * (interval.GetMin() - first), 11 + 2 * (interval.GetMax() - first));
    }
    // The returned interval is independently checked by Python, rather than used
    // to construct the expected reflection and accidentally hide a mapping bug.
    std::cout << "{\"domain\":";
    Interval(domain);
    std::cout << ",\"reversed_interval\":";
    Interval(ignored);
    std::cout << ",\"native_interval\":";
    Interval(intervals.front());
    std::cout << ",\"native_intervals\":[";
    for (std::size_t i = 0; i < intervals.size(); ++i)
    {
        if (i)
        {
            std::cout << ',';
        }
        Interval(intervals[i]);
    }
    std::cout << ']';
    if (periodic)
    {
        std::cout << ",\"periodic\":" << (curve->IsPeriodic(domain) ? "true" : "false");
    }
    std::cout << ",\"edited_domain\":";
    Interval(edited->GetNaturalInterval());
    std::cout << ",\"bounds\":";
    Box(bounds);
    std::cout << ",\"reverse_bounds\":";
    Box(Bounds(*reversed, reversedIntervals));
    std::cout << ",\"edited_bounds\":";
    Box(Bounds(*edited, editedIntervals));
    std::cout << ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        if (i)
        {
            std::cout << ',';
        }
        const double t = advanced ? samples[i] : domain.Evaluate(samples[i]);
        const double reverseParameter = first + last - t, editedParameter = 11 + 2 * (t - first);
        std::cout << "{\"parameter\":" << t << ",\"reverse_parameter\":" << reverseParameter << ",\"edited_parameter\":" << editedParameter
                  << ",\"value\":";
        Evaluate(*curve, t);
        std::cout << ",\"point\":";
        SmPoint3d point;
        Require(curve->EvaluatePoint(t, point) == SM_SUCCESS, "evaluate_point");
        Point(point);
        std::cout << ",\"reverse\":";
        // Reversal exchanges the two one-sided limits at a C1 conic span join.
        Evaluate(*reversed, reverseParameter, TRUE);
        std::cout << ",\"edited\":";
        Evaluate(*edited, editedParameter);
        if (advanced)
        {
            // bFromLeft means the point is at the LEFT of its selected span:
            // TRUE selects the mathematical right limit, FALSE the left limit.
            // Preserve this existing public convention at repeated knots.
            std::cout << ",\"right_value\":";
            Evaluate(*curve, t, TRUE);
            std::cout << ",\"reverse_left\":";
            Evaluate(*reversed, reverseParameter, FALSE);
            std::cout << ",\"edited_right\":";
            Evaluate(*edited, editedParameter, TRUE);
        }
        if (kind == "circle" || kind == "ellipse")
        {
            double angle = 0, roundtrip = 0;
            Require(curve->ConvertTFromNURBSToSTEP(t, angle) == SM_SUCCESS, "native_to_angle");
            Require(curve->ConvertTFromSTEPToNURBS(angle, roundtrip) == SM_SUCCESS, "angle_to_native");
            std::cout << ",\"angle\":" << angle << ",\"roundtrip\":" << roundtrip;
        }
        std::cout << '}';
    }
    std::cout << "]}\n";
}
} // namespace
int main()
{
    std::cin.imbue(std::locale::classic());
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(17);
    try
    {
        Run();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "curve_property_probe: " << error.what() << '\n';
        return 2;
    }
}
