// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportVertexProjectionProof.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace step_import;
using namespace step_import::detail;
namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
int main()
{
    try
    {
        const CircleProjectionGeometry circle{ { 1, 2, 3 }, { 0, 0, 2 }, 5 };
        const Fraction move{ 1, 1000 }, definition{ 1, 1000000000 };
        // Independent analytic witness: translated 3-4-5 radius, with nonunit
        // normal. The same ray's unique closest point is exactly (4,6,3).
        const auto proof = ProveCircleProjection({ 4.00006, 6.00008, 3.00002 }, circle, { 4, 6, 3 }, move, definition);
        Require(bool(proof), "unique translated 3-4-5 projection");
        const auto trueMovement = std::sqrt(0.0001 * 0.0001 + 0.00002 * 0.00002);
        Require(
            proof->displacementUpperMm >= trueMovement - 1e-15 && proof->displacementUpperMm < trueMovement + 1e-12,
            "displacement upper bound and accuracy"
        );
        Require(proof->projectionErrorUpperMm < 1e-9, "unique closest point enclosure");
        Require(!ProveCircleProjection({ 1, 2, 4 }, circle, { 6, 2, 3 }, move, definition), "axis projection is not unique");
        Require(
            !ProveCircleProjection({ 4.00006, 6.00008, 3 }, circle, { -2, -2, 3 }, move, definition),
            "another on-circle point is not the nearest point"
        );
        Require(!ProveCircleProjection({ 4, 6, 3 }, circle, { 4 + 1e-12, 6, 3 }, move, definition), "roundoff does not create a required repair");
        Require(
            !ProveCircleProjection({ 4.00006, 6.00008, 3 }, circle, { 4, 6, 3 }, Fraction{ 1, 100000 }, definition),
            "separate displacement budget"
        );
        auto invalid = circle;
        invalid.normal = { 0, 0, 0 };
        Require(!ProveCircleProjection({ 4.1, 6, 3 }, invalid, { 4, 6, 3 }, move, definition), "zero normal returns failure");
        invalid = circle;
        invalid.radiusMm = std::numeric_limits<double>::infinity();
        Require(!CircleDistanceLower({ 4, 6, 3 }, invalid), "nonfinite geometry returns failure");
        const auto circleDistance = CircleDistanceLower({ 1, 2, 15 }, circle);
        Require(circleDistance && *circleDistance > 13 - 1e-10 && *circleDistance <= 13, "5-12-13 circle clearance lower bound");
        const auto lineDistance = LineDistanceLower({ 2, 3, 4 }, { 0, 0, 0 }, { 0, 0, 2 });
        Require(lineDistance && *lineDistance > std::sqrt(13.) - 1e-10 && *lineDistance <= std::sqrt(13.), "infinite line clearance");
        const auto segmentDistance = SegmentDistanceLower({ 3, 4, 0 }, { 0, 0, 0 }, { 1, 0, 0 });
        Require(
            segmentDistance && *segmentDistance > std::sqrt(20.) - 1e-10 && *segmentDistance <= std::sqrt(20.),
            "clamped finite segment clearance"
        );
        const auto hullDistance = BoxDistanceLower({ 4, 5, 6 }, { Point{ 0, 0, 0 }, Point{ 1, 1, 1 } });
        Require(hullDistance && *hullDistance > std::sqrt(50.) - 1e-10 && *hullDistance <= std::sqrt(50.), "containing hull clearance");
        const auto touching = BoxDistanceLower({ 1, 1, 1 }, { Point{ 0, 0, 0 }, Point{ 1, 1, 1 } });
        Require(touching && *touching == 0, "touching hull has no proven clearance");
        const auto planeDistance = PlaneDistanceLower({ 4, 5, 6 }, { 0, 0, 1 }, { 0, 0, 2 });
        Require(planeDistance && *planeDistance > 5 - 1e-10 && *planeDistance <= 5, "nonunit plane normal clearance");
        const auto cylinderDistance = CylinderDistanceLower({ 1, 2, 15 }, circle);
        Require(cylinderDistance && *cylinderDistance > 5 - 1e-10 && *cylinderDistance <= 5, "cylinder axis clearance");
        const auto sphereDistance = SphereDistanceLower({ 1, 2, 16 }, circle.centerMm, 5);
        Require(sphereDistance && *sphereDistance > 8 - 1e-10 && *sphereDistance <= 8, "sphere clearance");
        const auto torusDistance = TorusDistanceLower({ 1, 2, 15 }, circle, 2);
        Require(torusDistance && *torusDistance > 11 - 1e-10 && *torusDistance <= 11, "5-12-13 torus tube clearance");
        std::cout << "bounded circle vertex projection contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
