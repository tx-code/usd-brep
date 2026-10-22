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
void Mesh(SmBrep& body, const std::filesystem::path& output, std::size_t& triangles, std::size_t& points, step_view::DisplayBudget* budget = nullptr)
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
    step_view::DisplayTopology topology(body, budget);
    Good(points && triangles && triangles <= 2000000 && points <= std::size_t(INT32_MAX), "DisplayMeshLimit");
    if (budget)
    {
        budget->meshPoints += points;
        budget->meshCells += triangles;
        Good(budget->meshPoints <= 6000000 && budget->meshCells <= 2000000, "DisplayMeshLimit");
    }
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
    if (budget)
    {
        budget->boundaryPoints += samples.GetSize();
        budget->boundaryCells += lines;
        Good(budget->boundaryPoints <= 6000000 && budget->boundaryCells <= 2000000, "DisplayBoundaryLimit");
    }
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
    topology.Write(output, budget);
}
struct BodyScene
{
    std::string directory, sourceShapeId;
    step_import::Statistics info;
    std::size_t triangles = 0, points = 0;
};
void Publish(
    const std::filesystem::path& output,
    const std::string& status,
    const std::string& key,
    const step_import::Report& report,
    const step_import::Statistics* info,
    std::size_t triangles,
    std::size_t points,
    bool auxiliary = false,
    std::string_view nativeStatus = {},
    std::string_view schema = "DeclaredOnly",
    std::string_view carrier = "DeclaredOnly",
    std::size_t reportMiB = 16,
    bool fileNameText = false,
    bool asciiInvariant = false,
    bool asciiApplied = false,
    const std::vector<BodyScene>* bodyScenes = nullptr
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
                    bodyScenes ? "{\"format\":\"brep-view-scene.v4\",\"status\":" :
                    info       ? "{\"format\":\"brep-view-scene.v3\",\"topology\":\"topology.json\",\"status\":" :
                                 "{\"format\":\"brep-view-scene.v1\",\"status\":"
                );
                w.String(status);
                w.Add(",\"native_import_status\":");
                w.String(nativeStatus);
                w.Add(",\"import_report_encoding\":");
                w.String(report.encoding == step_import::ReportEncoding::CompactV2 ? "CompactV2" : "ExpandedV1");
                w.Add(",\"requested_schema_interpretation\":");
                w.String(schema);
                w.Add(",\"requested_carrier_interpretation\":");
                w.String(carrier);
                w.Add(",\"report_budget_mib\":");
                w.Number(reportMiB);
                w.Add(",\"preserve_opaque_file_name_text\":");
                w.Bool(fileNameText);
                w.Add(",\"assume_ascii_invariant_data\":");
                w.Bool(asciiInvariant);
                w.Add(",\"ascii_invariant_data_applied\":");
                w.Bool(asciiApplied);
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
                    w.Add(",\"units\":\"mm\"");
                    if (!bodyScenes)
                    {
                        w.Add(",\"mesh\":\"mesh.vtk\",\"boundary\":\"edges.vtk\"");
                    }
                    else
                    {
                        w.Add(",\"body_count\":");
                        w.Number(bodyScenes->size());
                        w.Add(",\"body_scenes\":[");
                        for (std::size_t i = 0; i < bodyScenes->size(); ++i)
                        {
                            if (i)
                            {
                                w.Add(",");
                            }
                            const auto& body = (*bodyScenes)[i];
                            w.Add("{\"ordinal\":");
                            w.Number(i);
                            w.Add(",\"source_shape_id\":");
                            w.String(body.sourceShapeId);
                            w.Add(",\"format\":\"brep-view-scene.v3\",\"units\":\"mm\",\"auxiliary_required\":false,\"mesh\":");
                            w.String(body.directory + "/mesh.vtk");
                            w.Add(",\"boundary\":");
                            w.String(body.directory + "/edges.vtk");
                            w.Add(",\"topology\":");
                            w.String(body.directory + "/topology.json");
                            w.Add(",\"faces\":");
                            w.Number(body.info.faces);
                            w.Add(",\"edges\":");
                            w.Number(body.info.edges);
                            w.Add(",\"vertices\":");
                            w.Number(body.info.vertices);
                            w.Add(",\"triangles\":");
                            w.Number(body.triangles);
                            w.Add(",\"points\":");
                            w.Number(body.points);
                            w.Add("}");
                        }
                        w.Add("]");
                    }
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
    bool strict,
    const std::string& schema,
    const std::string& carrier,
    const std::string& reportBudget,
    bool fileNameText,
    bool asciiInvariant
)
{
    Good(std::filesystem::is_directory(output) && std::filesystem::is_empty(output), "FreshOutputDirectoryRequired");
    Good(
        reportBudget == "16" || reportBudget == "32" || reportBudget == "64" || reportBudget == "128" || reportBudget == "256",
        "InvalidReportBudget"
    );
    const std::size_t reportMiB = reportBudget == "16"  ? 16 :
                                  reportBudget == "32"  ? 32 :
                                  reportBudget == "64"  ? 64 :
                                  reportBudget == "128" ? 128 :
                                                          256;
    Good(!strict || !fileNameText, "FileNameTextRequiresInteroperability");
    Good(!strict || !asciiInvariant, "AsciiInvariantDataRequiresInteroperability");
    bool asciiApplied = false;
    step_import::PartOutcome result;
    std::string nativeStatus; // Empty until the core actually returns an outcome.
    try
    {
        auto options = step_import::Options::PlanarFixture();
        options.generalBrep = true;
        options.quality = profile == "development"          ? step_import::Quality::GeneralDevelopment() :
                          profile == "tolerance-study"      ? step_import::Quality::GeneralToleranceStudy() :
                          profile == "work-tolerance-study" ? step_import::Quality::GeneralWorkToleranceStudy() :
                                                              throw std::invalid_argument("UnknownViewerProfile");
        // Application-selected development preview policy, not a production
        // SDK default. Source assumptions require explicit arguments; no retry.
        options.sourceLimits = { 64 * 1024 * 1024, 1000000, 2000000, 2000000, 128, 1024 * 1024, 16 * 1024 * 1024, 16 * 1024 * 1024, 64 };
        options.semanticLimits.dependencyVisits = 10000000;
        options.nativeLimits.nativeCalls = 1000000;
        // Explicit capacity only: never retry with a larger budget, and never
        // alter source/geometry/working-tolerance or repair policies here.
        options.sourceLimits.reportBytes = reportMiB * 1024 * 1024;
        options.nativeLimits.reportBytes = std::max<std::size_t>(32, reportMiB) * 1024 * 1024;
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
            options.headerNamePolicy = fileNameText ? step_source::HeaderNamePolicy::PreserveFileNameText :
                                                      step_source::HeaderNamePolicy::PreserveOpaqueBytes;
            // Transport only an explicit application choice. Never infer this
            // interpretation from raw bytes or retry after an opaque-name refusal.
            options.dataStringPolicy = asciiInvariant ? step_source::DataStringPolicy::PreserveAsciiInvariantBytes :
                                                        step_source::DataStringPolicy::PreserveOpaqueBytes;
        }
        // Match the core's closed policy enums, never infer an interpretation
        // from file content or a failed default import. Strict mode cannot gain
        // a carrier/schema exception through this transport entry point.
        Good(!strict || (schema == "DeclaredOnly" && carrier == "DeclaredOnly"), "InterpretationRequiresInteroperability");
        if (schema == "Ap214NameOnlyAs2010")
        {
            options.schemaInterpretation = step_semantics::SchemaInterpretation::Ap214NameOnlyAs2010;
        }
        else if (schema == "UnregisteredAp214As2010")
        {
            options.schemaInterpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
        }
        else
        {
            Good(schema == "DeclaredOnly", "UnknownSchemaInterpretation");
        }
        if (carrier == "BlankLevelAsCorrectedEncoding")
        {
            options.carrierInterpretation = step_source::CarrierInterpretation::BlankLevelAsCorrectedEncoding;
        }
        else if (carrier == "Level1AsCorrectedEncoding")
        {
            options.carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncoding;
        }
        else if (carrier == "Level1AsCorrectedEncodingWithLowercaseHex")
        {
            options.carrierInterpretation = step_source::CarrierInterpretation::Level1AsCorrectedEncodingWithLowercaseHex;
        }
        else
        {
            Good(carrier == "DeclaredOnly", "UnknownCarrierInterpretation");
        }
        options.normalizePeriodicFaces = normalize;
        options.restrictUnusedSupportDomains = restrictDomain;
        // The application selects compact evidence; model acceptance and report
        // capacity stay identical. Dispatch is based on the one checked source,
        // not a second read or a retry after single-body failure.
        options.reportEncoding = step_import::ReportEncoding::CompactV2;
        result = step_import::ImportPartFile(input, options);
        const auto singleAlternative = std::get_if<std::shared_ptr<const step_import::ModelSnapshot>>(&result.part);
        const auto collectionAlternative = std::get_if<std::shared_ptr<const step_import::MaterialCollectionSnapshot>>(&result.part);
        const auto model = singleAlternative ? *singleAlternative : nullptr;
        const auto collection = collectionAlternative ? *collectionAlternative : nullptr;
        asciiApplied = (model && model->Source()->Source().HasAsciiInvariantDataAssumption()) ||
                       (collection && collection->Source()->Source().HasAsciiInvariantDataAssumption());
        nativeStatus = step_import::StatusName(result.status);
        std::ofstream report(output / "import-report.json", std::ios::binary);
        report << result.report.json;
        report.close();
        Good(bool(report), "ImportReportWriteFailed");
        if (!model && !collection)
        {
            Publish(
                output,
                step_import::StatusName(result.status),
                "ImportFailed",
                result.report,
                nullptr,
                0,
                0,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied
            );
            return 1;
        }
        if (collection)
        {
            step_view::DisplayBudget budget;
            std::vector<BodyScene> scenes;
            step_import::Statistics total;
            total.shapeRole = "MaterialSolidCollection";
            for (std::size_t i = 0; i < collection->BodyCount(); ++i)
            {
                Progress("bodies", i, collection->BodyCount());
                auto body = collection->Body(i);
                BodyScene scene{ "body-" + std::to_string(i), collection->SourceShapeId(i), body->Info(), 0, 0 };
                const auto directory = output / scene.directory;
                Good(std::filesystem::create_directory(directory), "DisplayBodyDirectoryFailed");
                auto copy = body->CopyBrepForModeling();
                Good(copy.status == step_import::Status::Accepted && bool(copy.model), "DisplayCopyFailed");
                copy.model->Edit(
                    [&](SmBrep& native)
                    {
                        Mesh(native, directory, scene.triangles, scene.points, &budget);
                    }
                );
                total.faces += scene.info.faces;
                total.edges += scene.info.edges;
                total.vertices += scene.info.vertices;
                scenes.push_back(std::move(scene));
            }
            Progress("bodies", collection->BodyCount(), collection->BodyCount());
            Publish(
                output,
                "Ready",
                "",
                result.report,
                &total,
                budget.meshCells,
                budget.meshPoints,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied,
                &scenes
            );
            return 0;
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
        const bool auxiliary = auxiliaryExport.Write(*model, output);
        const auto info = model->Info();
        Progress("copy");
        auto copy = model->CopyBrepForModeling();
        Good(copy.status == step_import::Status::Accepted && bool(copy.model), "DisplayCopyFailed");
        std::size_t triangles = 0, points = 0;
        copy.model->Edit(
            [&](SmBrep& body)
            {
                Mesh(body, output, triangles, points);
            }
        );
        Progress("publish");
        Publish(
            output,
            "Ready",
            "",
            result.report,
            &info,
            triangles,
            points,
            auxiliary,
            nativeStatus,
            schema,
            carrier,
            reportMiB,
            fileNameText,
            asciiInvariant,
            asciiApplied
        );
        return 0;
    }
    catch (const std::exception& error)
    {
        if (!std::filesystem::exists(output / "scene.json"))
        {
            Publish(
                output,
                "DisplayFailure",
                error.what(),
                result.report,
                nullptr,
                0,
                0,
                false,
                nativeStatus,
                schema,
                carrier,
                reportMiB,
                fileNameText,
                asciiInvariant,
                asciiApplied
            );
        }
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    if (argc != 7 && argc != 9 && argc != 10 && argc != 11 && argc != 12)
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
        if (argc >= 11)
        {
            Good(std::wstring_view(argv[10]) == L"0" || std::wstring_view(argv[10]) == L"1", "InvalidHeaderFlag");
        }
        if (argc == 12)
        {
            Good(std::wstring_view(argv[11]) == L"0" || std::wstring_view(argv[11]) == L"1", "InvalidDataFlag");
        }
        return Run(
            argv[1],
            argv[2],
            std::filesystem::path(argv[3]).string(),
            argv[4][0] == L'1',
            argv[5][0] == L'1',
            argv[6][0] == L'1',
            argc >= 9 ? std::filesystem::path(argv[7]).string() : "DeclaredOnly",
            argc >= 9 ? std::filesystem::path(argv[8]).string() : "DeclaredOnly",
            argc >= 10 ? std::filesystem::path(argv[9]).string() : "16",
            argc >= 11 && argv[10][0] == L'1',
            argc == 12 && argv[11][0] == L'1'
        );
    }
    catch (...)
    {
        return 2;
    }
}
#else
int main(int argc, char** argv)
{
    if (argc != 7 && argc != 9 && argc != 10 && argc != 11 && argc != 12)
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
        if (argc >= 11)
        {
            Good(std::string_view(argv[10]) == "0" || std::string_view(argv[10]) == "1", "InvalidHeaderFlag");
        }
        if (argc == 12)
        {
            Good(std::string_view(argv[11]) == "0" || std::string_view(argv[11]) == "1", "InvalidDataFlag");
        }
        return Run(
            argv[1],
            argv[2],
            argv[3],
            argv[4][0] == '1',
            argv[5][0] == '1',
            argv[6][0] == '1',
            argc >= 9 ? argv[7] : "DeclaredOnly",
            argc >= 9 ? argv[8] : "DeclaredOnly",
            argc >= 10 ? argv[9] : "16",
            argc >= 11 && argv[10][0] == '1',
            argc == 12 && argv[11][0] == '1'
        );
    }
    catch (...)
    {
        return 2;
    }
}
#endif
