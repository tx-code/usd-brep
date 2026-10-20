// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepImport.h"
#include "StepSourceReport.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace step_view
{
using Writer = step_source::report::Writer;
using Point = step_import::Point;
inline void Need(bool condition, const char* key)
{
    if (!condition)
    {
        throw std::runtime_error(key);
    }
}
inline void Scalar(Writer& writer, double value)
{
    // Writer::Number is a size_t counter API. Match StepImportReport.cpp's
    // binary64 round-trip formatting explicitly; never truncate coordinates or
    // native parameters through an implicit floating-to-integer conversion.
    Need(std::isfinite(value), "AuxiliaryNonfiniteNumber");
    std::array<char, 64> buffer{};
    const auto end = std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general,
        std::numeric_limits<double>::max_digits10
    );
    Need(end.ec == std::errc{}, "AuxiliaryNumberCapacity");
    writer.Add(std::string_view(buffer.data(), static_cast<std::size_t>(end.ptr - buffer.data())));
}
inline void Vector(Writer& writer, const Point& point)
{
    writer.Add("[");
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Need(std::isfinite(point[axis]), "AuxiliaryNonfiniteCoordinate");
        if (axis)
        {
            writer.Add(",");
        }
        Scalar(writer, point[axis]);
    }
    writer.Add("]");
}
inline void Frame(Writer& writer, const step_import::CoordinateFrame& frame)
{
    writer.Add("{\"origin_mm\":");
    Vector(writer, frame.originMm);
    writer.Add(",\"x\":");
    Vector(writer, frame.x);
    writer.Add(",\"y\":");
    Vector(writer, frame.y);
    writer.Add(",\"z\":");
    Vector(writer, frame.z);
    writer.Add("}");
}
class AuxiliaryExport
{
public:

    explicit AuxiliaryExport(std::function<void(std::size_t, std::size_t)> progress) : m_progress(std::move(progress))
    {
    }

    bool Write(const step_import::ModelSnapshot& model, const std::filesystem::path& output)
    {
        const auto wires = model.Wireframes();
        const auto support = model.SupportingGeometry();
        if (wires.empty() && support.empty())
        {
            return false;
        }
        for (const auto& view : wires)
        {
            const auto info = view.Info();
            m_total += info.curves.size() + info.points.size();
        }
        for (const auto& view : support)
        {
            const auto info = view.Info();
            m_total += info.curves.size() + info.points.size() + info.frames.size() + info.planes.size();
        }
        Need(m_total <= 10000, "AuxiliaryPrimitiveLimit");
        const auto bounds = model.Info().boundsMm;
        const auto diagonal = std::hypot(bounds[1][0] - bounds[0][0], bounds[1][1] - bounds[0][1], bounds[1][2] - bounds[0][2]);
        Need(std::isfinite(diagonal) && diagonal > 0, "AuxiliaryPreviewScaleUnavailable");
        std::string json;
        Need(
            Writer::Publish(
                16 * 1024 * 1024,
                json,
                [&](Writer& writer)
                {
                    writer.Add(
                        "{\"format\":\"brep-view-auxiliary.v1\",\"units\":\"mm\",\"curve_chord_observation_mm\":0.01,"
                        "\"continuous_error_bound_established\":false,\"preview_scale_mm\":"
                    );
                    Scalar(writer, diagonal * .1);
                    writer.Add(",\"groups\":[");
                    bool comma = false;
                    for (const auto& view : wires)
                    {
                        if (comma)
                        {
                            writer.Add(",");
                        }
                        comma = true;
                        Group(writer, view, "Wireframe");
                        const auto info = view.Info();
                        writer.Add(",\"set_id\":");
                        writer.String(info.setId);
                        writer.Add(",\"bounds_mm\":[");
                        Vector(writer, info.boundsMm[0]);
                        writer.Add(",");
                        Vector(writer, info.boundsMm[1]);
                        writer.Add("]}");
                    }
                    for (const auto& view : support)
                    {
                        if (comma)
                        {
                            writer.Add(",");
                        }
                        comma = true;
                        Group(writer, view, "ConstructiveSupportingGeometry");
                        const auto info = view.Info();
                        writer.Add(",\"frames\":[");
                        for (std::size_t i = 0; i < info.frames.size(); ++i)
                        {
                            if (i)
                            {
                                writer.Add(",");
                            }
                            writer.Add("{\"source_id\":");
                            writer.String(info.frames[i].sourceId);
                            writer.Add(",\"frame\":");
                            Frame(writer, info.frames[i].frame);
                            writer.Add("}");
                            Completed();
                        }
                        writer.Add("],\"planes\":[");
                        for (std::size_t i = 0; i < info.planes.size(); ++i)
                        {
                            if (i)
                            {
                                writer.Add(",");
                            }
                            const auto& plane = info.planes[i];
                            writer.Add("{\"source_id\":");
                            writer.String(plane.sourceId);
                            writer.Add(",\"position_id\":");
                            writer.String(plane.positionId);
                            writer.Add(",\"source_bounded\":false,\"frame\":");
                            Frame(writer, plane.frame);
                            writer.Add(",\"native_domain\":[");
                            for (std::size_t component = 0; component < 4; ++component)
                            {
                                if (component)
                                {
                                    writer.Add(",");
                                }
                                Scalar(writer, plane.parameterDomain[component]);
                            }
                            writer.Add("]");
                            // The source PLANE is unbounded. These are only four query
                            // points for a marked display window, never a new trim,
                            // material face, or a reinterpretation of finite sentinels.
                            const auto half = diagonal * .6;
                            writer.Add(",\"preview_uv_mm\":[");
                            Scalar(writer, -half);
                            writer.Add(",");
                            Scalar(writer, -half);
                            writer.Add(",");
                            Scalar(writer, half);
                            writer.Add(",");
                            Scalar(writer, half);
                            writer.Add("],\"preview_corners_mm\":[");
                            std::size_t corner = 0;
                            for (const auto& uv :
                                 { std::array{ -half, -half }, std::array{ half, -half }, std::array{ half, half }, std::array{ -half, half } })
                            {
                                if (corner++)
                                {
                                    writer.Add(",");
                                }
                                Tick();
                                Vector(writer, view.EvaluatePlane(i, uv[0], uv[1]));
                            }
                            writer.Add("]}");
                            Completed();
                        }
                        writer.Add("]}");
                    }
                    writer.Add("]}");
                }
            ),
            "AuxiliaryReportLimit"
        );
        std::ofstream file(output / "auxiliary.json", std::ios::binary);
        file << json;
        file.close();
        Need(bool(file), "AuxiliaryWriteFailed");
        return true;
    }

private:

    void Tick()
    {
        Need(++m_evaluations <= 500000, "AuxiliaryEvaluationLimit");
        Need(std::chrono::steady_clock::now() < m_deadline, "AuxiliaryPreviewDeadline");
    }
    void Completed()
    {
        m_progress(++m_completed, m_total);
    }
    static double SegmentDistance(const Point& point, const Point& a, const Point& b)
    {
        double length2 = 0, projection = 0;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            length2 += (b[axis] - a[axis]) * (b[axis] - a[axis]);
            projection += (point[axis] - a[axis]) * (b[axis] - a[axis]);
        }
        Need(std::isfinite(length2) && std::isfinite(projection), "AuxiliaryPreviewNumericRange");
        const double t = length2 > 0 ? std::clamp(projection / length2, 0., 1.) : 0.;
        return std::hypot(point[0] - std::lerp(a[0], b[0], t), point[1] - std::lerp(a[1], b[1], t), point[2] - std::lerp(a[2], b[2], t));
    }
    template <class View>
    void Group(Writer& writer, const View& view, const char* role)
    {
        const auto info = view.Info();
        writer.Add("{\"role\":");
        writer.String(role);
        writer.Add(",\"material_boundary\":false,\"representation_id\":");
        writer.String(info.representationId);
        writer.Add(",\"relationship_id\":");
        writer.String(info.relationshipId);
        writer.Add(",\"points\":[");
        for (std::size_t i = 0; i < info.points.size(); ++i)
        {
            if (i)
            {
                writer.Add(",");
            }
            writer.Add("{\"source_id\":");
            writer.String(info.points[i].sourceId);
            writer.Add(",\"point_mm\":");
            Vector(writer, info.points[i].pointMm);
            writer.Add("}");
            Completed();
        }
        writer.Add("],\"curves\":[");
        for (std::size_t i = 0; i < info.curves.size(); ++i)
        {
            if (i)
            {
                writer.Add(",");
            }
            const auto& curve = info.curves[i];
            const auto lo = curve.parameterDomain[0], hi = curve.parameterDomain[1];
            Need(std::isfinite(lo) && std::isfinite(hi) && hi > lo, "AuxiliaryCurveInterval");
            struct Sample
            {
                double t;
                Point point;
            };
            const auto evaluate = [&](double t)
            {
                Tick();
                return Sample{ t, view.EvaluateCurve(i, t) };
            };
            std::vector<Sample> samples;
            const auto append = [&](const Sample& sample)
            {
                Need(++m_points <= 200000, "AuxiliarySampleLimit");
                samples.push_back(sample);
            };
            // Sampling is derived display evidence, not a continuous error
            // certificate. Seed the WHOLE native interval (also closed curves),
            // refine quarter/midpoint observations, and fail at any budget/depth
            // limit rather than publishing an unfinished curve or changing it.
            const auto refine = [&](auto&& self, Sample a, Sample b, unsigned depth) -> void
            {
                std::array<Sample, 3> inside{ evaluate(std::lerp(a.t, b.t, .25)),
                                              evaluate(std::midpoint(a.t, b.t)),
                                              evaluate(std::lerp(a.t, b.t, .75)) };
                double error = 0;
                for (const auto& sample : inside)
                {
                    error = std::max(error, SegmentDistance(sample.point, a.point, b.point));
                }
                if (error <= .01)
                {
                    append(b);
                }
                else
                {
                    Need(depth < 12 && inside[1].t > a.t && inside[1].t < b.t, "AuxiliaryCurveSubdivisionLimit");
                    self(self, a, inside[1], depth + 1);
                    self(self, inside[1], b, depth + 1);
                }
            };
            append(evaluate(lo));
            for (unsigned seed = 1; seed <= 32; ++seed)
            {
                refine(refine, samples.back(), evaluate(std::lerp(lo, hi, double(seed) / 32.)), 0);
            }
            if (!curve.sourceForwardInNative)
            {
                std::reverse(samples.begin(), samples.end());
            }
            writer.Add("{\"source_id\":");
            writer.String(curve.sourceId);
            writer.Add(",\"basis_id\":");
            writer.String(curve.basisId);
            writer.Add(",\"native_domain\":[");
            Scalar(writer, lo);
            writer.Add(",");
            Scalar(writer, hi);
            writer.Add("],\"parameter_space\":\"Native\",\"source_forward_in_native\":");
            writer.Add(curve.sourceForwardInNative ? "true" : "false");
            writer.Add(",\"parameters\":[");
            for (std::size_t n = 0; n < samples.size(); ++n)
            {
                if (n)
                {
                    writer.Add(",");
                }
                Scalar(writer, samples[n].t);
            }
            writer.Add("],\"polyline_mm\":[");
            for (std::size_t n = 0; n < samples.size(); ++n)
            {
                if (n)
                {
                    writer.Add(",");
                }
                Vector(writer, samples[n].point);
            }
            writer.Add("]}");
            Completed();
        }
        writer.Add("]"); // Caller closes the role-specific group after its extra members.
    }
    std::function<void(std::size_t, std::size_t)> m_progress;
    std::size_t m_evaluations = 0, m_points = 0, m_completed = 0, m_total = 0;
    std::chrono::steady_clock::time_point m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
};
} // namespace step_view
