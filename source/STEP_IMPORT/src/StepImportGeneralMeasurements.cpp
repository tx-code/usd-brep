// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportGeneralMeasurements.h"

#include "StepImportNativeData.h"

#include <SmAttribute.h>
#include <SmFace.h>
#include <SmFaceuse.h>
#include <SmRegion.h>
#include <SmShell.h>
#include <algorithm>
#include <map>
#include <utility>

namespace step_import::detail
{
namespace
{
double AreaEstimate(const SmExtent3d& box)
{
    const auto size = box.GetSize();
    const double largest = std::max({ size.x, size.y, size.z });
    return largest * largest / 100.0;
}
Point P(const SmVector3d& value)
{
    return { value.x, value.y, value.z };
}
} // namespace

SmStatus IntegrateGeneralSolid(
    const ModelData& model,
    const Point& origin,
    const std::function<void(std::size_t)>& beforeFace,
    GeneralSolidIntegrals& output
)
{
    if (!model.brep || !model.material || !beforeFace || !model.brep->IsManifoldSolid())
    {
        return SM_ERR_INVALID_INPUT;
    }

    // Match the existing uniform-density mass query's eligibility. GeneralBuilder
    // creates attribute-free geometry; never reinterpret a later weighted model.
    ULONG massAttribute = SM_AI_MASS_PROPERTIES;
    SmTArray<SmFace*> attributedFaces;
    SmTArray<SmEdge*> attributedEdges;
    SmTArray<SmVertex*> attributedVertices;
    model.brep->GetFaces(attributedFaces, &massAttribute);
    model.brep->GetEdges(attributedEdges, &massAttribute);
    model.brep->GetVertices(attributedVertices, &massAttribute);
    if (attributedFaces.GetSize() || attributedEdges.GetSize() || attributedVertices.GetSize())
    {
        return SM_ERR_INVALID_INPUT;
    }

    SmTArray<SmFace*> nativeFaces;
    model.brep->GetFaces(nativeFaces);
    if (nativeFaces.GetSize() != model.faces.size())
    {
        return SM_ERR_INVALID_INPUT;
    }
    std::map<const SmFace*, std::size_t> sourceOrder;
    for (std::size_t i = 0; i < model.faces.size(); ++i)
    {
        if (!model.faces[i].face || !sourceOrder.emplace(model.faces[i].face, i).second)
        {
            return SM_ERR_INVALID_INPUT;
        }
    }

    // Keep the original mass query's common origin and area scale, and the old
    // face-centroid query's local origin/scale. The smaller scale cannot loosen
    // either query's existing area-derived integration control.
    SmExtent3d modelBox;
    SmStatus status = model.brep->CalculateBoundingBox(modelBox);
    if (status != SM_SUCCESS)
    {
        return status;
    }
    const double modelEstimate = AreaEstimate(modelBox);
    const SmPoint3d volumeOrigin(origin[0], origin[1], origin[2]);
    constexpr double relativeAccuracy = 1e-4;
    constexpr ULONG groups = SM_PPF_AREA | SM_PPF_AREA_FIRST_MOMENTS | SM_PPF_VOLUME | SM_PPF_VOLUME_FIRST_MOMENTS;
    GeneralSolidIntegrals result;
    result.faces.resize(model.faces.size());
    SmVector3d volumeFirst(0, 0, 0);

    // Retain the canonical BRep face order for model accumulation. Per-face
    // results are put back in source order; native identities are never changed.
    for (ULONG i = 0; i < nativeFaces.GetSize(); ++i)
    {
        auto* face = nativeFaces[i];
        const auto found = sourceOrder.find(face);
        if (found == sourceOrder.end())
        {
            return SM_ERR_INVALID_INPUT;
        }
        beforeFace(found->second);
        auto* up = face->GetUpwardFaceuse();
        auto* down = up ? up->GetMate() : nullptr;
        if (!up || !down || !up->GetShell() || !down->GetShell())
        {
            return SM_ERR_INVALID_INPUT;
        }
        auto* upRegion = up->GetShell()->GetRegion();
        auto* downRegion = down->GetShell()->GetRegion();
        if (!upRegion || !downRegion)
        {
            return SM_ERR_INVALID_INPUT;
        }
        auto* materialSide = upRegion == model.material ? up : downRegion == model.material ? down : nullptr;
        auto* opposite = materialSide == up ? downRegion : upRegion;
        if (!materialSide || model.material->IsVoid() || !opposite->IsVoid())
        {
            return SM_ERR_INVALID_INPUT;
        }

        SmExtent3d faceBox;
        status = face->CalculateBoundingBox(faceBox);
        if (status != SM_SUCCESS)
        {
            return status;
        }
        const SmPoint3d areaOrigin = faceBox.GetMid();
        double area = 0, volume = 0;
        SmTArray<SmVector3d> moments;
        status = face->ComputePreciseProperties(
            materialSide->GetOrientation(),
            relativeAccuracy,
            volumeOrigin,
            areaOrigin,
            std::min(modelEstimate, AreaEstimate(faceBox)),
            0.0,
            area,
            volume,
            moments,
            groups
        );
        if (status != SM_SUCCESS)
        {
            return status;
        }
        result.areaMm2 += area;
        result.volumeMm3 += volume;
        volumeFirst += moments[4]; // Geometric dV first moments; density is one.
        result.faces[found->second] = { area, P(moments[0]), P(areaOrigin) };
    }
    // Preserve the uniform-density mass API's nondegenerate-volume guard.
    if (!SM_IS_VALID_DOUBLE(result.volumeMm3) || result.volumeMm3 < SM_EFF_ZERO)
    {
        return SM_ERR_INVALID_INPUT;
    }
    result.centroidMm = P(volumeOrigin + volumeFirst / result.volumeMm3);
    output = std::move(result);
    return SM_SUCCESS;
}
} // namespace step_import::detail
