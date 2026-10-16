// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepPlanar.h"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Internal native-import interface. No native pointer or editing operation escapes this module.
namespace step_import
{
using Point = std::array<double, 3>;
using Bounds = std::array<Point, 2>;
using Fraction = step_planar::Rational;
namespace detail
{
struct ModelData;
struct Access;
} // namespace detail
struct Quality
{
    std::string id;
    Fraction definitionMm, consistencyMm, derivedUvMm;
    Fraction initialWorkToleranceMm, maximumWorkToleranceMm, toleranceGrowthMm;
    Fraction queryMm, areaMm2;
    static Quality PlanarFixture(); // Explicit synthetic-test candidate, never a production default.
};
struct Limits
{
    // Completed-call checkpoints for construction, UV installation, validators and measured queries.
    // Read-only coefficient/topology accessors are not individually charged. This is not a timeout or memory cap.
    std::size_t nativeCalls = 10000;
    std::size_t reportBytes = 16 * 1024 * 1024;
};
struct Options
{
    step_source::Limits sourceLimits{};
    step_semantics::Limits semanticLimits;
    step_planar::Limits planarLimits;
    Quality quality;
    Limits nativeLimits;
    static Options PlanarFixture();
};
struct Statistics
{
    std::size_t faces = 0, edges = 0, vertices = 0;
    double areaMm2 = 0, boundaryMm = 0;
    Bounds boundsMm{};
    Point centroidMm{}, normal{};
    std::array<double, 4> windowMm{}; // umin, vmin, umax, vmax; computational window, not a new trim.
    std::string shapeRole;
};
struct ClosestPoint
{
    Point pointMm{};
    double distanceMm = 0;
};
enum class PointClass
{
    Outside,
    Interior,
    Edge,
    Vertex
};
class FaceView
{
public:

    std::string SourceId() const;
    Point EvaluateSurface(double u, double v) const; // Rejects domain violations before native evaluation.
    ClosestPoint Closest(const Point& pointMm) const;
    PointClass ClassifyUV(double u, double v) const;

private:

    friend class ModelSnapshot;
    explicit FaceView(std::shared_ptr<detail::ModelData> data);
    std::shared_ptr<detail::ModelData> m_data;
};
class ModelSnapshot
{
public:

    Statistics Info() const;
    FaceView Face() const; // Owning view: retains the native model and its context after root release.
    std::shared_ptr<const step_semantics::Snapshot> Source() const;
    std::optional<double> MaterialVolumeMm3() const; // Open surface: NotApplicable, never a fabricated zero.

private:

    friend struct detail::Access;
    explicit ModelSnapshot(std::shared_ptr<detail::ModelData> data);
    std::shared_ptr<detail::ModelData> m_data;
};
struct ToleranceObservation
{
    std::string stage, kind, sourceId;
    double valueMm = 0; // Present observations only; an unexecuted stage has no rows.
};
struct EdgeMapping
{
    std::string edgeId, curveId, startVertexId, endVertexId, nativeMinVertexId, nativeMaxVertexId;
    bool sourceSameSense = true;
    Fraction sourceStartCoefficient, sourceEndCoefficient, sourceDirectionNormSquared;
    // Source parameter = coefficient / sqrt(norm squared); target curve parameter is [0,1].
};
struct UseMapping
{
    std::string orientedEdgeId, edgeId, startVertexId, endVertexId;
    bool forwardInNativeCurve = true, uvOwnerOnSelectedSide = false;
    std::array<double, 2> uvStartMm{}, uvEndMm{};
};
struct LoopMapping
{
    std::string faceId, boundId, loopId;
    bool outer = false, sourceOrientation = true;
    std::vector<UseMapping> uses; // Actual selected-side cyclic traversal; UV is stored on each edge's SAME member.
};
struct Report
{
    std::string sourceSha256, sourceStatus, semanticStatus, planarStatus;
    std::shared_ptr<const step_source::SourceSnapshot> source;
    std::shared_ptr<const std::string> captured;
    std::vector<step_semantics::Diagnostic> diagnostics;
    std::vector<ToleranceObservation> tolerances;
    std::vector<EdgeMapping> edges;
    std::vector<LoopMapping> loops;
    std::optional<Statistics> native;
    std::optional<double> definitionBoundMm, consistencyBoundMm, uvBoundMm;
    std::optional<std::size_t> pointerReports, deepReports;
    std::string kernelPath;
    std::size_t nativeCalls = 0;
    bool candidateDiscarded = false;
    std::string json; // Mandatory bounded report, completed before a model is published.
};
enum class Status
{
    Accepted,
    Rejected,
    Unsupported,
    Deferred,
    Indeterminate,
    ResourceLimit,
    BuildFailure,
    PostcheckFailure,
    SourceFailure
};
struct Outcome
{
    Status status = Status::BuildFailure;
    std::shared_ptr<const ModelSnapshot> model;
    Report report;
};
Outcome ImportBytes(std::string bytes, const Options& options);
Outcome ImportFile(const std::filesystem::path& path, const Options& options);
// Invalid options throw std::invalid_argument; allocation failure is not a qualified recoverable outcome.
const char* StatusName(Status status);
} // namespace step_import
