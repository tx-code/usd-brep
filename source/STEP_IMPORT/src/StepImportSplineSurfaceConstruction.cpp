// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportSplineSurfaceConstruction.h"

#include "StepImportGeometryConstruction.h"

#include <SmBSplineSurface.h>
namespace step_import::detail::construction
{
namespace
{
class SplineSurfaceBuilder : private GeometryConstruction
{
public:

    explicit SplineSurfaceBuilder(const GeometryConstruction& inputs) : GeometryConstruction(inputs)
    {
    }
    Result<std::unique_ptr<SmSurface>> Make(Index g, Index use)
    {
        SmTArray<SmPoint3d> cp;
        // Source rows are U, entries V. SMLib uses the same U-major layout.
        for (const auto& row : F(g, "control_points_list").children)
        {
            for (const auto& ref : row.children)
            {
                auto pointResult = Position(m_source->ReferenceTarget(ref));
                if (!pointResult)
                {
                    return tl::unexpected(std::move(pointResult.error()));
                }
                const auto& point = *pointResult;
                cp.Add(P(point));
            }
        }
        auto um = Multiplicities(g, "u_multiplicities");
        if (!um)
        {
            return tl::unexpected(std::move(um.error()));
        }
        auto vm = Multiplicities(g, "v_multiplicities");
        if (!vm)
        {
            return tl::unexpected(std::move(vm.error()));
        }
        auto ukResult = Numbers(g, "u_knots");
        if (!ukResult)
        {
            return tl::unexpected(std::move(ukResult.error()));
        }
        auto vkResult = Numbers(g, "v_knots");
        if (!vkResult)
        {
            return tl::unexpected(std::move(vkResult.error()));
        }
        auto weights = SplineWeights(g, true, cp);
        if (!weights)
        {
            return tl::unexpected(std::move(weights.error()));
        }
        SmBSplineSurface* surface = nullptr;
        if (auto operation = Step(g); !operation)
        {
            return tl::unexpected(std::move(operation.error()));
        }
        auto vDegree = Degree(g, "v_degree");
        if (!vDegree)
        {
            return tl::unexpected(std::move(vDegree.error()));
        }
        auto uDegree = Degree(g, "u_degree");
        if (!uDegree)
        {
            return tl::unexpected(std::move(uDegree.error()));
        }
        const auto createdStatus = SmBSplineSurface::CreateCanonical(
            *m_context,
            *uDegree,
            *vDegree,
            cp,
            SM_SF_UNSPECIFIED,
            *um,
            *vm,
            *ukResult,
            *vkResult,
            SM_KT_UNSPECIFIED,
            weights->GetSize() ? &*weights : nullptr,
            nullptr,
            surface
        );
        std::unique_ptr<SmSurface> owned(surface);
        if (auto nativeStatusCheck = Good(createdStatus); !nativeStatusCheck)
        {
            return tl::unexpected(std::move(nativeStatusCheck.error()));
        }
        if (weights->GetSize())
        {
            if (auto businessCheck = Need(bool(surface), "GeneralSurfaceAllocation", g); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            ULONG u = 0, v = 0;
            double* net = nullptr;
            if (auto nativeStatusCheck = Good(surface->GetControlPointsPointer(u, v, net)); !nativeStatusCheck)
            {
                return tl::unexpected(std::move(nativeStatusCheck.error()));
            }
            if (auto businessCheck = Need(
                    u == F(g, "control_points_list").children.size() && v == F(g, "control_points_list").children[0].children.size(),
                    "GeneralRationalNetShapeChanged",
                    g
                );
                !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
            if (auto businessCheck = VerifyRationalNet(g, use, cp, *weights, net, u * v, surface->IsRational()); !businessCheck)
            {
                return tl::unexpected(std::move(businessCheck.error()));
            }
        }
        return owned;
    }
};
} // namespace
Result<std::unique_ptr<SmSurface>> MakeSourceSplineSurface(const GeometryConstruction& inputs, std::size_t geometry, std::size_t use)
{
    return SplineSurfaceBuilder(inputs).Make(geometry, use);
}
} // namespace step_import::detail::construction
