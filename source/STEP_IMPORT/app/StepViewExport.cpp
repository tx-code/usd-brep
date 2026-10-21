// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
// Isolated display worker: no native handle crosses the process boundary.
#include "StepImport.h"
#include "StepSourceReport.h"
#include "StepViewAuxiliary.h"
#include "StepViewTopology.h"

#include <SmApiBrep.h>
#include <SmBrep.h>
#include <SmEdge.h>
#include <SmPoly.h>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace
{
using Writer = step_source::report::Writer;
void Progress(const char* phase, std::size_t completed = 0, std::size_t total = 0)
{
    // Counts describe only the named operation. Native call budgets are not a
    // denominator for import work; meshing has no measurable total here.
    std::cout << "@brep-view:" << phase << ',' << completed << ',' << total << '\n' << std::flush;
}
void Good(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
template <class T>
void Network(std::ostream& out, T value)
{
    // Legacy VTK binary scalars are big endian; coordinates retain all 64 bits.
    std::array<char, sizeof(T)> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        bytes[bytes.size() - 1 - i] = static_cast<char>((value >> (8 * i)) & 255);
    }
    out.write(bytes.data(), bytes.size());
}
void Point(std::ostream& out, const SmPoint3d& p)
{
    for (double v : { p.x, p.y, p.z })
    {
        Good(std::isfinite(v), "NonfiniteMeshCoordinate");
        Network(out, std::bit_cast<std::uint64_t>(v));
    }
}
void Mesh(SmBrep& body, const std::filesystem::path& output, std::size_t& triangles, std::size_t& points)
{
    SmTessellationParams params;
    params.dChordHeightTol = .01; // Display mm, independent of import acceptance/work tolerances.
    params.dCurveAngleTolDeg = params.dSurfaceAngleTolDeg = 15.;
    SmPolyBrep* raw = nullptr;
    Progress("mesh");
    const auto status = SmApiTessellate(&body, raw, params, FALSE);
    std::unique_ptr<SmPolyBrep> mesh(raw);
    Good(status == SM_SUCCESS && mesh, "TessellationFailed"); // Never request a partial-face fallback.
    SmTArray<SmPolyVertex*> vertices;
    SmTArray<SmPolyFace*> faces;
    mesh->GetPolyVertices(vertices);
    mesh->GetPolyFaces(faces);
    points = vertices.GetSize();
    triangles = faces.GetSize();
    step_view::DisplayTopology topology(body);
    Good(points && triangles && triangles <= 2000000 && points <= std::size_t(INT32_MAX), "DisplayMeshLimit");
    std::unordered_map<SmPolyVertex*, std::uint32_t> indices;
    indices.reserve(points);
    Progress("mesh-points", 0, points);
    std::ofstream out(output / "mesh.vtk", std::ios::binary);
    out << "# vtk DataFile Version 3.0\nBRep Viewer display mesh\nBINARY\nDATASET POLYDATA\nPOINTS " << points << " double\n";
    for (ULONG i = 0; i < vertices.GetSize(); ++i)
    {
        indices.emplace(vertices[i], i);
        Point(out, vertices[i]->GetPoint());
        if ((i + 1) % 4096 == 0 || i + 1 == points)
        {
            Progress("mesh-points", i + 1, points);
        }
    }
    out << "\nPOLYGONS " << triangles << ' ' << triangles * 4 << '\n';
    Progress("mesh-faces", 0, triangles);
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        auto loop = faces[i]->GetOuterPolyLoop();
        Good(loop != nullptr, "MissingMeshLoop");
        SmTArray<SmPolyEdge*> edges;
        loop->GetPolyEdges(edges);
        Good(edges.GetSize() == 3, "UnexpectedNontriangleMesh");
        Network(out, std::uint32_t(3));
        for (ULONG j = 0; j < 3; ++j)
        {
            Network(out, indices.at(edges[j]->GetStartPolyVertex()));
        }
        if ((i + 1) % 4096 == 0 || i + 1 == triangles)
        {
            Progress("mesh-faces", i + 1, triangles);
        }
    }
    out << "\nCELL_DATA " << triangles << "\nSCALARS TopoFace int 1\nLOOKUP_TABLE default\n";
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        Network(out, static_cast<std::uint32_t>(topology.Face(*faces[i])));
    }
    out << '\n';
    out.close();
    Good(bool(out), "MeshWriteFailed");
    SmTArray<SmPoint3d> samples;
    SmTArray<ULONG> counts;
    SmTArray<SmEdge*> edges;
    Progress("boundary");
    Good(SmApiTessellateBoundaries(&body, 5., samples, counts, edges) == SM_SUCCESS, "BoundaryTessellationFailed");
    Good(counts.GetSize() == edges.GetSize(), "DisplayEdgeProvenanceCount");
    std::size_t lines = 0, entries = 0;
    for (ULONG i = 0; i < counts.GetSize(); ++i)
    {
        if (counts[i] > 1)
        {
            ++lines;
            entries += counts[i] + 1;
        }
    }
    Good(samples.GetSize() <= std::size_t(INT32_MAX) && entries <= std::size_t(INT32_MAX), "DisplayBoundaryLimit");
    std::ofstream wire(output / "edges.vtk", std::ios::binary);
    Progress("boundary-points", 0, samples.GetSize());
    wire << "# vtk DataFile Version 3.0\nBRep boundary display samples\nBINARY\nDATASET POLYDATA\nPOINTS " << samples.GetSize() << " double\n";
    for (ULONG i = 0; i < samples.GetSize(); ++i)
    {
        Point(wire, samples[i]);
        if ((i + 1) % 4096 == 0 || i + 1 == samples.GetSize())
        {
            Progress("boundary-points", i + 1, samples.GetSize());
        }
    }
    wire << "\nLINES " << lines << ' ' << entries << '\n';
    Progress("boundary-lines", 0, counts.GetSize());
    std::uint32_t offset = 0;
    for (ULONG i = 0; i < counts.GetSize(); ++i)
    {
        if (counts[i] > 1)
        {
            Network(wire, std::uint32_t(counts[i]));
            for (ULONG j = 0; j < counts[i]; ++j)
            {
                Network(wire, offset + j);
            }
        }
        offset += counts[i];
        if ((i + 1) % 4096 == 0 || i + 1 == counts.GetSize())
        {
            Progress("boundary-lines", i + 1, counts.GetSize());
        }
    }
    wire << "\nCELL_DATA " << lines << "\nSCALARS TopoEdge int 1\nLOOKUP_TABLE default\n";
    for (ULONG i = 0; i < counts.GetSize(); ++i)
    {
        if (counts[i] > 1)
        {
            Network(wire, topology.Edge(edges[i]));
        }
    }
    wire << '\n';
    wire.close();
    Good(bool(wire), "BoundaryWriteFailed");
    Progress("measure");
    topology.Write(output);
}
void Publish(
    const std::filesystem::path& output,
    const std::string& status,
    const std::string& key,
    const step_import::Report& report,
    const step_import::Statistics* info,
    std::size_t triangles,
    std::size_t points,
    bool auxiliary = false
)
{
    std::string json;
    Good(
        Writer::Publish(
            65536,
            json,
            [&](Writer& w)
            {
                w.Add(
                    info ? "{\"format\":\"brep-view-scene.v3\",\"topology\":\"topology.json\",\"status\":" :
                           "{\"format\":\"brep-view-scene.v1\",\"status\":"
                );
                w.String(status);
                w.Add(",\"key\":");
                w.String(key);
                w.Add(",\"source_sha256\":");
                w.String(report.sourceSha256);
                w.Add(",\"triangles\":");
                w.Number(triangles);
                w.Add(",\"points\":");
                w.Number(points);
                if (info)
                {
                    // v3 always declares whether the associated payload is
                    // required. Losing its path cannot become a body-only scene.
                    w.Add(",\"auxiliary_required\":");
                    w.Bool(auxiliary);
                    if (auxiliary)
                    {
                        w.Add(",\"auxiliary\":\"auxiliary.json\"");
                    }
                    w.Add(",\"role\":");
                    w.String(info->shapeRole);
                    w.Add(",\"faces\":");
                    w.Number(info->faces);
                    w.Add(",\"edges\":");
                    w.Number(info->edges);
                    w.Add(",\"vertices\":");
                    w.Number(info->vertices);
                    w.Add(",\"mesh\":\"mesh.vtk\",\"boundary\":\"edges.vtk\",\"units\":\"mm\"");
                }
                w.Add(",\"diagnostics\":[");
                for (std::size_t i = 0; i < report.diagnostics.size() && i < 5; ++i)
                {
                    if (i)
                    {
                        w.Add(",");
                    }
                    w.Add("{\"key\":");
                    w.String(report.diagnostics[i].source.key);
                    w.Add(",\"message\":");
                    w.String(report.diagnostics[i].source.message);
                    w.Add("}");
                }
                w.Add("]}");
            }
        ),
        "SceneReportLimit"
    );
    std::ofstream file(output / "scene.json.tmp", std::ios::binary);
    file << json;
    file.close();
    Good(bool(file), "SceneReportWriteFailed");
    // Only the complete manifest publishes. Mesh fragments remain private on
    // errors/hard cancellation; the application owns the whole request directory.
    std::filesystem::rename(output / "scene.json.tmp", output / "scene.json");
}
int Run(
    const std::filesystem::path& input,
    const std::filesystem::path& output,
    const std::string& profile,
    bool normalize,
    bool restrictDomain,
    bool strict
)
{
    Good(std::filesystem::is_directory(output) && std::filesystem::is_empty(output), "FreshOutputDirectoryRequired");
    step_import::Outcome result;
    try
    {
        auto options = step_import::Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = profile == "development"          ? step_import::Quality::GeneralDevelopment() :
                          profile == "tolerance-study"      ? step_import::Quality::GeneralToleranceStudy() :
                          profile == "work-tolerance-study" ? step_import::Quality::GeneralWorkToleranceStudy() :
                                                              throw std::invalid_argument("UnknownViewerProfile");
        // Application-selected development preview policy, not a production
        // SDK default. No schema/carrier assumption or automatic tolerance retry.
        options.sourceLimits = { 64 * 1024 * 1024, 1000000, 2000000, 2000000, 128, 1024 * 1024, 16 * 1024 * 1024, 16 * 1024 * 1024, 64 };
        options.semanticLimits.dependencyVisits = 10000000;
        options.nativeLimits.nativeCalls = 1000000;
        options.nativeLimits.reportBytes = 32 * 1024 * 1024;
        options.nativeLimits.processPrivateBytes = 2ULL * 1024 * 1024 * 1024;
        options.execution.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        const char* lastPhase = "";
        options.execution.progress = [&](step_import::Progress p)
        {
            const char* phase = nullptr;
            switch (p.phase)
            {
                case step_import::Phase::BeforeCapture:
                    phase = "capture";
                    break;
                case step_import::Phase::Captured:
                    phase = "check";
                    break;
                case step_import::Phase::BeforeNativeCall:
                    phase = "build";
                    break;
                case step_import::Phase::NativeReady:
                case step_import::Phase::BeforeReport:
                    phase = "report";
                    break;
                default:
                    break;
            }
            if (phase && std::string_view(lastPhase) != phase)
            {
                Progress(phase);
                lastPhase = phase;
            }
        };
        if (!strict)
        {
            options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
            options.headerNamePolicy = step_source::HeaderNamePolicy::PreserveOpaqueBytes;
            options.dataStringPolicy = step_source::DataStringPolicy::PreserveOpaqueBytes;
        }
        options.normalizePeriodicFaces = normalize;
        options.restrictUnusedSupportDomains = restrictDomain;
        result = step_import::ImportFile(input, options);
        std::ofstream report(output / "import-report.json", std::ios::binary);
        report << result.report.json;
        report.close();
        Good(bool(report), "ImportReportWriteFailed");
        if (!result.model)
        {
            Publish(output, step_import::StatusName(result.status), "ImportFailed", result.report, nullptr, 0, 0);
            return 1;
        }
        // CopyBrepForModeling explicitly selects the body component. Complete
        // CopyForModeling still refuses mixed requests; we retain the owning
        // source snapshot and export EVERY associated role before publication.
        step_view::AuxiliaryExport auxiliaryExport(
            [](auto completed, auto total)
            {
                Progress("auxiliary", completed, total);
            }
        );
        const bool auxiliary = auxiliaryExport.Write(*result.model, output);
        const auto info = result.model->Info();
        Progress("copy");
        auto copy = result.model->CopyBrepForModeling();
        Good(copy.status == step_import::Status::Accepted && bool(copy.model), "DisplayCopyFailed");
        std::size_t triangles = 0, points = 0;
        copy.model->Edit(
            [&](SmBrep& body)
            {
                Mesh(body, output, triangles, points);
            }
        );
        Progress("publish");
        Publish(output, "Ready", "", result.report, &info, triangles, points, auxiliary);
        return 0;
    }
    catch (const std::exception& error)
    {
        if (!std::filesystem::exists(output / "scene.json"))
        {
            Publish(output, "DisplayFailure", error.what(), result.report, nullptr, 0, 0);
        }
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    if (argc != 7)
    {
        return 2;
    }
    try
    {
        // The parent assigns the process to its resource/cleanup job before
        // permitting input capture or native allocations. EOF never starts work.
        std::string start;
        Good(bool(std::getline(std::cin, start)) && start == "start", "StartHandshakeRequired");
        for (int i = 4; i < 7; ++i)
        {
            Good(std::wstring_view(argv[i]) == L"0" || std::wstring_view(argv[i]) == L"1", "InvalidFlag");
        }
        return Run(argv[1], argv[2], std::filesystem::path(argv[3]).string(), argv[4][0] == L'1', argv[5][0] == L'1', argv[6][0] == L'1');
    }
    catch (...)
    {
        return 2;
    }
}
#else
int main(int argc, char** argv)
{
    if (argc != 7)
    {
        return 2;
    }
    try
    {
        std::string start;
        Good(bool(std::getline(std::cin, start)) && start == "start", "StartHandshakeRequired");
        for (int i = 4; i < 7; ++i)
        {
            Good(std::string_view(argv[i]) == "0" || std::string_view(argv[i]) == "1", "InvalidFlag");
        }
        return Run(argv[1], argv[2], argv[3], argv[4][0] == '1', argv[5][0] == '1', argv[6][0] == '1');
    }
    catch (...)
    {
        return 2;
    }
}
#endif
