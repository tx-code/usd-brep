// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepAssemblyImport.h"
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
#include <type_traits>
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
        return WriteGroups(model.Wireframes(), model.SupportingGeometry(), model.Info().boundsMm, DefinitionId(*model.Source()), output);
    }
    bool Write(const step_import::MaterialCollectionSnapshot& model, const std::filesystem::path& output)
    {
        const auto wires = model.Wireframes();
        const auto supporting = model.SupportingGeometry();
        if (wires.empty() && supporting.empty())
        {
            return false;
        }
        step_import::Bounds bounds{ Point{ INFINITY, INFINITY, INFINITY }, Point{ -INFINITY, -INFINITY, -INFINITY } };
        for (std::size_t i = 0; i < model.BodyCount(); ++i)
        {
            const auto b = model.Body(i)->Info().boundsMm;
            for (std::size_t k = 0; k < 3; ++k)
            {
                bounds[0][k] = std::min(bounds[0][k], b[0][k]);
                bounds[1][k] = std::max(bounds[1][k], b[1][k]);
            }
        }
        return WriteGroups(wires, supporting, bounds, DefinitionId(*model.Source()), output);
    }
    std::size_t PrimitiveCount() const
    {
        return m_total;
    }
    static std::string DefinitionId(const step_semantics::Snapshot& source)
    {
        for (const auto& role : source.PartDefinition().roles)
        {
            if (role.name == "definition")
            {
                return source.Source().Records()[role.recordIndex].id;
            }
        }
        throw std::logic_error("AssociatedDefinitionSourceMissing");
    }
    bool WriteGroups(
        const std::vector<step_import::WireframeView>& wires,
        const std::vector<step_import::SupportingGeometryView>& support,
        const step_import::Bounds& bounds,
        const std::string& definitionId,
        const std::filesystem::path& output
    )
    {
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
                    writer.Add(",\"source_definition_id\":");
                    writer.String(definitionId);
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

    std::size_t WriteAssembly(const step_import::AssemblySnapshot& model, const std::filesystem::path& output)
    {
        std::size_t instances = 0, groups = 0;
        for (std::size_t i = 0; i < model.InstanceCount(); ++i)
        {
            if (const auto wire = model.WireframeModel(model.Instance(i).definitionIndex))
            {
                ++instances;
                for (const auto& view : wire->Wireframes())
                {
                    ++groups;
                    const auto info = view.Info();
                    m_total += info.curves.size() + info.points.size();
                }
            }
        }
        if (!instances)
        {
            return 0;
        }
        Need(groups <= 10000 && m_total <= 10000, "AuxiliaryPrimitiveLimit");
        std::string json;
        Need(
            Writer::Publish(
                16 * 1024 * 1024,
                json,
                [&](Writer& writer)
                {
                    m_points = m_completed = 0;
                    writer.Add(
                        "{\"format\":\"brep-view-wires.v1\",\"units\":\"mm\",\"coordinate_scope\":\"AssemblyRoot\","
                        "\"continuous_error_bound_established\":false,\"curve_chord_observation_mm\":0.01,\"source_sha256\":"
                    );
                    writer.String(model.Source()->Inputs().SourceFields().Source().Sha256());
                    writer.Add(",\"groups\":[");
                    bool comma = false;
                    for (std::size_t i = 0; i < model.InstanceCount(); ++i)
                    {
                        const auto node = model.Instance(i);
                        const auto wire = model.WireframeModel(node.definitionIndex);
                        if (!wire)
                        {
                            continue;
                        }
                        for (const auto& view : wire->Wireframes())
                        {
                            if (comma)
                            {
                                writer.Add(",");
                            }
                            comma = true;
                            Group(writer, view, "DefinitionWireframe", &node);
                            writer.Add(",\"instance\":");
                            writer.Number(i);
                            writer.Add(",\"definition\":");
                            writer.Number(node.definitionIndex);
                            writer.Add(",\"source_definition_id\":");
                            writer.String(model.SourceDefinitionId(node.definitionIndex));
                            writer.Add(",\"set_id\":");
                            writer.String(view.Info().setId);
                            writer.Add("}");
                        }
                    }
                    writer.Add("]}");
                }
            ),
            "AuxiliaryReportLimit"
        );
        CheckDeadline();
        std::ofstream file(output / "wireframes.json", std::ios::binary);
        file << json;
        file.close();
        Need(bool(file), "AuxiliaryWriteFailed");
        return instances;
    }

private:

    void CheckDeadline() const
    {
        Need(std::chrono::steady_clock::now() < m_deadline, "AuxiliaryPreviewDeadline");
    }
    void Tick()
    {
        Need(++m_evaluations <= 500000, "AuxiliaryEvaluationLimit");
        CheckDeadline();
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
    static Point ToRoot(const Point& point, const step_import::AssemblyInstanceInfo* node)
    {
        if (!node)
        {
            return point;
        }
        // The checked assembly contract currently admits rigid placements.
        // Only display values are transformed; source definitions, native
        // parameter domains and shared owners remain in local coordinates.
        Point result = node->translationToRootMm;
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j)
            {
                result[i] += node->rotationToRoot[i][j] * point[j];
            }
        }
        Need(
            std::all_of(
                result.begin(),
                result.end(),
                [](double v)
                {
                    return std::isfinite(v);
                }
            ),
            "AuxiliaryTransformRange"
        );
        return result;
    }
    template <class View>
    void Group(Writer& writer, const View& view, const char* role, const step_import::AssemblyInstanceInfo* node = nullptr)
    {
        const auto info = view.Info();
        writer.Add("{\"role\":");
        writer.String(role);
        writer.Add(",\"material_boundary\":false,\"representation_id\":");
        writer.String(info.representationId);
        writer.Add(",\"relationship_id\":");
        if (node)
        {
            writer.Add("null");
        }
        else
        {
            writer.String(info.relationshipId);
        }
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
            Vector(writer, ToRoot(info.points[i].pointMm, node));
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
                return Sample{ t, ToRoot(view.EvaluateCurve(i, t), node) };
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
            if constexpr (std::is_same_v<View, step_import::WireframeView>)
            {
                if (node)
                {
                    // A display polyline nominates an item but cannot provide
                    // its length. Query the native defining curve with an
                    // explicit mm stopping control; rigid placement preserves
                    // that length. Query failure is unavailable, never zero.
                    Tick();
                    std::optional<double> length;
                    try
                    {
                        length = view.CurveLength(i, 1e-5);
                    }
                    catch (const std::runtime_error&)
                    {
                    }
                    // Even the final query may exhaust the display deadline.
                    // A late answer must not become a Ready scene merely
                    // because no subsequent evaluator call checks the clock.
                    CheckDeadline();
                    writer.Add(",\"length_mm\":");
                    if (length)
                    {
                        Scalar(writer, *length);
                    }
                    else
                    {
                        writer.Add("null");
                    }
                    writer.Add(",\"measurement_status\":");
                    writer.String(length ? "Available" : "NativeQueryFailed");
                    writer.Add(",\"length_absolute_accuracy_mm\":0.00001");
                }
            }
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
