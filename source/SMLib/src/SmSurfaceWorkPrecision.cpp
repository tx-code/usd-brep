// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StdAfx.h"

#include <SmAttribute.h>
#include <SmBSplineCurve.h>
#include <SmCacheMgr.h>
#include <SmCrvOnSurf.h>
#include <SmFace.h>
#include <SmSurfOfRevolution.h>
#include <SmSurfaceWorkPrecision.h>
#include <SmTol.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>

namespace
{
bool SupportedOwner(const SmSurface& surface)
{
    if (surface.GetType() != SmSurfOfRevolution_TYPE || !surface.GetContext())
    {
        return false;
    }
    // Follow only immediate reciprocal ownership. In particular, do not use
    // GetFace(): malformed surface-owner chains may contain cycles. Native
    // seam construction owns a deep copy of this same registered definition;
    // its numerical resolution belongs to that copy through later Copy calls.
    if (const auto* owner = surface.GetOwner())
    {
        if (const auto* face = dynamic_cast<const SmFace*>(owner))
        {
            if (face->GetContext() != surface.GetContext() || face->GetSurface() != &surface)
            {
                return false;
            }
        }
        else
        {
            const auto* curve = dynamic_cast<const SmCrvOnSurf*>(owner);
            if (!curve || curve->GetType() != SmCrvOnSurf_TYPE || !curve->OwnsBaseSurface() || curve->GetContext() != surface.GetContext() ||
                curve->GetBaseSurface() != &surface)
            {
                return false;
            }
            const auto* uv = curve->GetUVCurve();
            if (!uv || uv->GetDim() != 2 || uv->GetContext() != surface.GetContext() || uv->GetOwner() != curve)
            {
                return false;
            }
        }
    }
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    const auto& revolution = static_cast<const SmSurfOfRevolution&>(surface);
    return revolution.GetCanonical(generator, origin, axis) == SM_SUCCESS && generator && generator->GetContext() == surface.GetContext() &&
           generator->GetOwner() == &surface;
}
void Store(SmGenericAttribute& attribute, double value)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (unsigned i = 0; i < 4; ++i)
    {
        attribute.SetLongElement(i + 1, static_cast<long>((bits >> (16 * i)) & 0xffff));
    }
}
void Invalidate(SmSurface& surface)
{
    // These are derived caches made under an earlier work resolution, not
    // defining coefficients or retained approximate-pole observations.
    SmCacheMgr::DeleteObjectsCache(SM_OC_SURFACE, &surface);
    SmCacheMgr::DeleteObjectsCache(SM_OC_TRIMSRF, &surface);
}
} // namespace

SmStatus SmSurfaceWorkPrecision::Get(const SmSurface& surface, double& value)
{
    value = 0;
    const auto* attribute = surface.FindAttribute(AttributeId);
    if (!attribute)
    {
        return SM_SUCCESS;
    }
    SmAttribute* attributeStorage[4];
    SmTArray<SmAttribute*> attributes(4, attributeStorage);
    surface.FindSlewOfAttributes(AttributeId, attributes);
    SmAObject* userStorage[4];
    SmTArray<SmAObject*> users(4, userStorage);
    attribute->GetUsers(users);
    if (!SupportedOwner(surface) || attributes.GetSize() != 1 || users.GetSize() != 1 || users[0] != &surface ||
        attribute->GetContext() != surface.GetContext() || attribute->GetBehavior() != SM_AB_COPY ||
        attribute->GetType() != SmGenericAttribute_TYPE || !dynamic_cast<const SmGenericAttribute*>(attribute) ||
        attribute->GetNumLongElements() != 5 || attribute->GetNumDoubleElements() || attribute->GetNumCharacterElements())
    {
        return SM_ERR;
    }
    const auto* words = attribute->GetLongElementsAddress();
    if (!words || words[0] != 1)
    {
        return SM_ERR;
    }
    std::uint64_t bits = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        if (words[i + 1] < 0 || words[i + 1] > 0xffff)
        {
            return SM_ERR;
        }
        bits |= static_cast<std::uint64_t>(words[i + 1]) << (16 * i);
    }
    const double decoded = std::bit_cast<double>(bits);
    if (!std::isfinite(decoded) || decoded <= 0)
    {
        return SM_ERR;
    }
    value = decoded;
    return SM_SUCCESS;
}

SmStatus SmSurfaceWorkPrecision::GetEffective(const SmSurface& surface, double& value)
{
    value = 0;
    double requested = 0;
    const auto status = Get(surface, requested);
    if (status != SM_SUCCESS)
    {
        return status;
    }
    // SmTol remains the pre-policy geometric zone, including the existing
    // face or standalone convention. Only explicit numerical work callers
    // consume this separate value; source coincidence checks retain SmTol.
    const double legacy = SmTol::GetZoneTol3d(&surface);
    value = requested > 0 ? std::min(legacy, requested) : legacy;
    return SM_SUCCESS;
}

SmStatus SmSurfaceWorkPrecision::Set(SmSurface& surface, double value)
{
    if (!std::isfinite(value) || value < 0)
    {
        return SM_ERR;
    }
    auto* previous = surface.FindAttribute(AttributeId);
    double current = 0;
    if (Get(surface, current) != SM_SUCCESS)
    {
        return SM_ERR;
    }
    if (value == 0)
    {
        if (previous)
        {
            surface.RemoveAttribute(previous);
            Invalidate(surface);
        }
        return SM_SUCCESS;
    }
    if (!SupportedOwner(surface))
    {
        return SM_ERR;
    }
    if (previous)
    {
        // Get established a unique, single-user COPY payload. Rewriting its
        // fixed five words allocates nothing and cannot affect another surface.
        Store(*static_cast<SmGenericAttribute*>(previous), value);
    }
    else
    {
        SmTArray<long> words;
        words.Add(1);
        for (unsigned i = 0; i < 4; ++i)
        {
            words.Add(0);
        }
        SmTArray<double> doubles;
        SmTArray<char> characters;
        std::unique_ptr<SmGenericAttribute> next(new (*surface.GetContext()) SmGenericAttribute(AttributeId, SM_AB_COPY, words, doubles, characters));
        Store(*next, value);
        try
        {
            surface.AddAttribute(next.get());
        }
        catch (...)
        {
            // AddAttribute can install its pointer before user-list allocation.
            // Retain one cleanup owner and preserve the actual exception.
            if (surface.FindAttribute(AttributeId) == next.get())
            {
                next.release();
            }
            throw;
        }
        next.release();
    }
    Invalidate(surface);
    double stored = 0;
    return Get(surface, stored) == SM_SUCCESS && stored == value ? SM_SUCCESS : SM_ERR;
}
