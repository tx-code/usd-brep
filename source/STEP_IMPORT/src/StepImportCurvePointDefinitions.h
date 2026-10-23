// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepImport.h"

#include <memory>
#include <span>
#include <type_traits>

class SmCurve;

namespace step_import::detail
{
namespace construction
{
class GeometryConstruction;
}

// Internal identities are deliberately distinct. Legacy public observations
// retain their size_t archive indices; query/archive adapters rebind explicitly.
// A strong index does not prove its range or membership in a particular owner.
struct SourceRecord
{
    explicit SourceRecord(std::size_t index) : index(index)
    {
    }
    std::size_t index;
};
struct CurveIndex
{
    explicit CurveIndex(std::size_t index) : index(index)
    {
    }
    std::size_t index;
};
struct PointIndex
{
    explicit PointIndex(std::size_t index) : index(index)
    {
    }
    std::size_t index;
};

class CurvePointDefinitions;

enum class SupportingAppendResult
{
    Appended,
    ExistingPlacement
};

class WireframeEvidence
{
public:

    double SourceResidualMm() const
    {
        return m_source;
    }
    double EndpointResidualMm() const
    {
        return m_endpoints;
    }

private:

    friend class CurvePointDefinitions;
    double m_source = 0, m_endpoints = 0;
};
class SupportingEvidence
{
public:

    std::optional<double> SourceResidualMm() const
    {
        return m_source;
    }
    std::optional<double> EndpointResidualMm() const
    {
        return m_endpoints;
    }

private:

    friend class CurvePointDefinitions;
    std::optional<double> m_source, m_endpoints;
};

// The snapshot owns this value. It exposes borrowed geometry and observations,
// never the owning containers. ModelData must empty it under NativeMutex before
// releasing its context, including when construction/publication fails.
class OwnedCurvePointDefinitions
{
public:

    OwnedCurvePointDefinitions() noexcept;
    ~OwnedCurvePointDefinitions();
    OwnedCurvePointDefinitions(OwnedCurvePointDefinitions&&) noexcept;
    OwnedCurvePointDefinitions& operator=(OwnedCurvePointDefinitions&&) noexcept;
    OwnedCurvePointDefinitions(const OwnedCurvePointDefinitions&) = delete;
    OwnedCurvePointDefinitions& operator=(const OwnedCurvePointDefinitions&) = delete;

    std::size_t CurveCount() const;
    const SmCurve& CurveAt(CurveIndex index) const;
    std::span<const WireframePointInfo> Points() const;

private:

    friend class CurvePointDefinitions;
    class CurveDefinition;
    std::vector<CurveDefinition> m_curves;
    std::vector<WireframePointInfo> m_points;
};

static_assert(!std::is_copy_constructible_v<OwnedCurvePointDefinitions>);
static_assert(std::is_nothrow_move_constructible_v<OwnedCurvePointDefinitions>);
static_assert(std::is_nothrow_move_assignable_v<OwnedCurvePointDefinitions>);
} // namespace step_import::detail
