// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepViewAuxiliary.h"

#include <SmApiQueries.h>
#include <SmBrep.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmPoly.h>
#include <SmVertex.h>
#include <unordered_map>

namespace step_view
{
// These indices identify live topology of THIS independent display copy, not
// STEP entity IDs. Never infer them from triangle or tessellation-point order.
class DisplayTopology
{
public:

    explicit DisplayTopology(SmBrep& body)
    {
        body.GetFaces(m_faces);
        body.GetEdges(m_edges);
        body.GetVertices(m_vertices);
        Need(m_faces.GetSize() <= 100000 && m_edges.GetSize() <= 200000 && m_vertices.GetSize() <= 200000, "DisplayTopologyLimit");
        for (ULONG i = 0; i < m_faces.GetSize(); ++i)
        {
            m_faceIndices.emplace(m_faces[i], i);
        }
        for (ULONG i = 0; i < m_edges.GetSize(); ++i)
        {
            m_edgeIndices.emplace(m_edges[i], i);
        }
    }
    std::int32_t Face(const SmPolyFace& triangle)
    {
        const auto found = m_faceIndices.find(triangle.GetOriginalFace());
        // SmPoly.h permits GetOriginalFace as a label even with invalid back
        // pointers. ConvertToPolyBrep remaps these labels to the caller's faces
        // with SM_INDEXING, then deliberately clears the back-pointer flag.
        // Resolve by membership in this still-live display copy only; never
        // dereference the label or claim its other topology pointers are valid.
        if (found == m_faceIndices.end())
        {
            faceLabelsComplete = false;
            return -1;
        }
        return static_cast<std::int32_t>(found->second);
    }
    std::uint32_t Edge(const SmEdge* edge) const
    {
        const auto found = m_edgeIndices.find(edge);
        Need(found != m_edgeIndices.end(), "DisplayEdgeProvenanceUnavailable");
        return found->second;
    }
    void Write(const std::filesystem::path& output)
    {
        std::string text;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        Need(
            Writer::Publish(
                32 * 1024 * 1024,
                text,
                [&](Writer& writer)
                {
                    writer.Add(
                        "{\"format\":\"brep-view-topology.v1\",\"units\":\"mm\","
                        "\"identity_scope\":\"NativeDisplayCopy\",\"face_labels_complete\":"
                    );
                    writer.Bool(faceLabelsComplete);
                    writer.Add(
                        ",\"face_area_relative_accuracy\":0.0001,\"edge_length_absolute_accuracy_mm\":0.00001,"
                        "\"continuous_error_bound_established\":false,\"faces\":["
                    );
                    for (ULONG i = 0; i < m_faces.GetSize(); ++i)
                    {
                        if (i)
                        {
                            writer.Add(",");
                        }
                        double area = 0;
                        const bool budget = std::chrono::steady_clock::now() < deadline;
                        const auto status = budget ? SmApiFaceComputeArea(m_faces[i], 1e-4, area) : SM_ERR_INVALID_INPUT;
                        const bool available = budget && status == SM_SUCCESS && std::isfinite(area) && area >= 0;
                        writer.Add("{\"id\":");
                        writer.Number(i);
                        writer.Add(",\"area_mm2\":");
                        if (available)
                        {
                            Scalar(writer, area);
                        }
                        else
                        {
                            writer.Add("null");
                        }
                        writer.Add(",\"measurement_status\":");
                        writer.String(available ? "Available" : budget ? "NativeQueryFailed" : "MeasurementBudget");
                        writer.Add("}");
                    }
                    writer.Add("],\"edges\":[");
                    for (ULONG i = 0; i < m_edges.GetSize(); ++i)
                    {
                        if (i)
                        {
                            writer.Add(",");
                        }
                        double length = 0;
                        const bool budget = std::chrono::steady_clock::now() < deadline;
                        const auto status = budget ? SmApiEdgeComputeLength(m_edges[i], 1e-5, length) : SM_ERR_INVALID_INPUT;
                        const bool available = budget && status == SM_SUCCESS && std::isfinite(length) && length >= 0;
                        writer.Add("{\"id\":");
                        writer.Number(i);
                        writer.Add(",\"length_mm\":");
                        if (available)
                        {
                            Scalar(writer, length);
                        }
                        else
                        {
                            writer.Add("null");
                        }
                        writer.Add(",\"measurement_status\":");
                        writer.String(available ? "Available" : budget ? "NativeQueryFailed" : "MeasurementBudget");
                        writer.Add("}");
                    }
                    writer.Add("],\"vertices\":[");
                    for (ULONG i = 0; i < m_vertices.GetSize(); ++i)
                    {
                        if (i)
                        {
                            writer.Add(",");
                        }
                        const auto point = m_vertices[i]->GetPoint();
                        writer.Add("{\"id\":");
                        writer.Number(i);
                        writer.Add(",\"point_mm\":");
                        Vector(writer, { point.x, point.y, point.z });
                        writer.Add("}");
                    }
                    writer.Add("]}");
                }
            ),
            "DisplayTopologyReportLimit"
        );
        std::ofstream file(output / "topology.json", std::ios::binary);
        file << text;
        file.close();
        Need(bool(file), "DisplayTopologyWriteFailed");
    }
    bool faceLabelsComplete = true;

private:

    SmTArray<SmFace*> m_faces;
    SmTArray<SmEdge*> m_edges;
    SmTArray<SmVertex*> m_vertices;
    std::unordered_map<const SmFace*, std::uint32_t> m_faceIndices;
    std::unordered_map<const SmEdge*, std::uint32_t> m_edgeIndices;
};
} // namespace step_view
