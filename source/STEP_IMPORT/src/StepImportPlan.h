// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

namespace step_import::detail
{
using ExactPoint = step_planar::Vector;
struct Failure
{
    Status status;
    std::string key, message;
    std::optional<std::size_t> record;
    std::string stage = "D5";
};
struct VertexPlan
{
    std::size_t record;
    ExactPoint pointMm;
};
struct EdgePlan
{
    std::size_t record, minimumVertex, maximumVertex;
    ExactPoint minimumMm, maximumMm; // Points on the defining source LINE, not a fit through source vertices.
    Fraction sourceLengthSquared;
    EdgeMapping mapping;
};
struct UsePlan
{
    std::size_t edge, orientedEdge;
    bool forward;
};
struct LoopPlan
{
    std::size_t bound, loop;
    bool outer, sourceOrientation;
    std::vector<UsePlan> primaryUses, selectedUses;
};
struct FacePlan
{
    std::size_t face, plane;
    bool faceSameSense;
    ExactPoint originMm, x, y;
    Fraction xNormSquared, yNormSquared, sourceAreaSquared;
    ExactPoint sourceCentroid, sourceMinimum, sourceMaximum;
    std::vector<std::size_t> vertices, edges;
    std::vector<LoopPlan> loops; // Native convention: outer first; source identities remain separate from list order.
};
struct BoxProof
{
    ExactPoint origin, center, minimum, maximum;
    std::array<ExactPoint, 3> axes;
    std::array<std::size_t, 8> corners;
    Fraction volume;
};
struct Plan
{
    std::shared_ptr<const step_semantics::Snapshot> source;
    SourceShapeObservation sourceShape;
    std::size_t record, shell;
    UnitNormalization units;
    bool materialSolid = false, closedBoundary = false;
    std::vector<VertexPlan> vertices;
    std::vector<EdgePlan> edges;
    std::vector<FacePlan> faces;
    std::optional<BoxProof> box;
};
Plan MakePlan(const step_planar::Outcome& checked);
SourceDeclarations DescribeSource(const step_planar::Outcome& checked, const Quality& quality);
class Execution;
std::shared_ptr<ModelData> Build(const Plan& plan, const Options& options, Report& report, Execution& execution);
struct Access
{
    static std::shared_ptr<const ModelSnapshot> Snapshot(std::shared_ptr<ModelData> data);
};
void Serialize(const Outcome& outcome, const Quality& quality, const step_source::InspectOutcome& source, std::size_t limit, std::string& output);
} // namespace step_import::detail
