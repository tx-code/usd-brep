// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportConicalApexProof.h"

#include <SmCircle.h>
#include <SmCone.h>
#include <SmContext.h>
#include <SmPlane.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
int main()
{
    try
    {
        SmContext context;
        const SmAxis2Placement frame(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0));
        SmCone* coneRaw = nullptr;
        Require(SmCone::CreateCanonical(context, frame, 3., 0., 3., coneRaw) == SM_SUCCESS && coneRaw, "public cone");
        std::unique_ptr<SmCone> cone(coneRaw);
        SmCircle* ringRaw = nullptr;
        Require(SmCircle::CreateCanonical(context, frame, 3., ringRaw) == SM_SUCCESS && ringRaw, "public circular boundary");
        std::unique_ptr<SmCircle> ring(ringRaw);
        SmPlane* planeRaw = nullptr;
        Require(SmPlane::CreateCanonical(context, frame, planeRaw) == SM_SUCCESS && planeRaw, "public nonparticipating base plane");
        std::unique_ptr<SmPlane> plane(planeRaw);
        const Point original{ .00001, 0, 3 };
        const std::array<Point, 1> vertices{ Point{ 3, 0, 0 } };
        const std::array<const SmCurve*, 1> curves{ ring.get() };
        const std::array<const SmSurface*, 1> surfaces{ plane.get() };
        const ConicalApexProofLimits limits{ Fraction(1, 1000), Fraction(1, 1000), Fraction(1, 1000000000), 4096 };
        std::size_t work = 0;
        std::size_t capacity = 100000;
        const ConicalApexProofCallbacks callbacks{
            []
            {
                return Result<void>{};
            },
            [&](std::size_t amount) -> Result<void>
            {
                if (amount > capacity - work)
                {
                    return tl::unexpected(Failure{ Status::ResourceLimit, "PublicProofWorkLimit", "public test work exhausted", {}, "Repair" });
                }
                work += amount;
                return {};
            }
        };
        const auto run = [&](const auto& points, const auto& faces, const auto& selectedLimits)
        {
            work = 0;
            return ProveConicalApexPointCorrection(original, *cone, *ring, points, curves, faces, selectedLimits, callbacks);
        };
        auto proof = run(vertices, surfaces, limits);
        if (!proof)
        {
            throw std::runtime_error(proof.error().key);
        }
        Require(
            proof && *proof && (**proof).originalMm == original &&
                std::hypot((**proof).correctedMm[0], (**proof).correctedMm[1], (**proof).correctedMm[2] - 3) < 1e-13,
            "unique actual native apex agrees with the independent cone"
        );
        Require(
            (**proof).displacementUpperMm >= .00001 && (**proof).displacementUpperMm <= .001 && (**proof).localProtectionLowerMm > 2.9,
            "outward move bound and conservative analytic clearance"
        );
        const auto exactWork = work;
        capacity = exactWork;
        Require(bool(run(vertices, surfaces, limits)), "exact proof-work capacity completes");
        capacity = exactWork - 1;
        const auto exhausted = run(vertices, surfaces, limits);
        Require(
            !exhausted && exhausted.error().status == Status::ResourceLimit && exhausted.error().key == "PublicProofWorkLimit",
            "one less proof unit propagates the caller's exact failure"
        );
        capacity = 100000;
        auto tight = limits;
        tight.maximumDisplacementMm = Fraction(1, 1000000);
        Require(!run(vertices, surfaces, tight), "out-of-budget movement cannot be proposed");
        const std::array<Point, 1> nearVertex{ Point{ .0001, 0, 3 } };
        Require(!run(nearVertex, surfaces, limits), "another close vertex protects small features");
        const SmAxis2Placement thinFrame(SmPoint3d(0, 0, 2.9999), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0));
        SmPlane* thinRaw = nullptr;
        Require(SmPlane::CreateCanonical(context, thinFrame, thinRaw) == SM_SUCCESS && thinRaw, "public nearby thin-wall plane");
        std::unique_ptr<SmPlane> thin(thinRaw);
        const std::array<const SmSurface*, 1> thinFaces{ thin.get() };
        Require(!run(vertices, thinFaces, limits), "face interiors are protected even when ring and vertices are far away");
        const std::array<const SmSurface*, 1> unknown{ nullptr };
        Require(!run(vertices, unknown, limits), "unknown required clearance cannot become a passed empty set");
        auto tooFewControls = limits;
        tooFewControls.maximumControls = 1;
        const std::array<const SmSurface*, 1> boundedSurface{ cone.get() };
        const auto controlLimited = run(vertices, boundedSurface, tooFewControls);
        Require(
            !controlLimited && controlLimited.error().status == Status::ResourceLimit && controlLimited.error().key == "ConicalApexHullControlLimit",
            "configured control exhaustion is not a source or clearance error"
        );
        auto coarse = limits;
        coarse.maximumWorkToleranceMm = Fraction(1, 10);
        Require(!run(vertices, surfaces, coarse), "existing work tolerance also obeys local 1-percent protection");
        auto stopped = callbacks;
        stopped.beforeNative = []() -> Result<void>
        {
            return tl::unexpected(Failure{ Status::Cancelled, "PublicCancel", "public cancellation", {}, "Repair" });
        };
        const auto cancelled = ProveConicalApexPointCorrection(original, *cone, *ring, vertices, curves, surfaces, limits, stopped);
        Require(
            !cancelled && cancelled.error().status == Status::Cancelled && cancelled.error().key == "PublicCancel",
            "cancellation is not a geometric failure"
        );
        const auto none = ProveConicalApexPointCorrection((**proof).correctedMm, *cone, *ring, vertices, curves, surfaces, limits, callbacks);
        Require(none && !*none, "zero displacement is normal absence, not a repaired qualification");
        Require(original == Point{ .00001, 0, 3 }, "proof never mutates the original point");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
