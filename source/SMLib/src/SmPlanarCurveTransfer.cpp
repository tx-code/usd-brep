// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StdAfx.h"

#include <SmBrep.h>
#include <SmCurve.h>
#include <SmEdge.h>
#include <SmPlanarCurveTransfer.h>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef SM_PLANAR_TRANSFER_TESTING
namespace
{
thread_local long failBeforeLink = -1;
thread_local bool throwBeforeLink = false, failAfterConstruction = false;
} // namespace
#endif

struct SmPlanarCurveTransfer::Data
{
    struct Entry
    {
        SmCurve* curve;
        SmObject* originalOwner;
        SmEdge* edge = nullptr;
    };
    SmTArray<SmCurve*> inputs;
    std::vector<Entry> entries;
    std::unordered_map<SmCurve*, std::size_t> indices;
    SmBrep* brep = nullptr;
    Data* previous = nullptr;
    SmTArray<SmBoolean>* transfers = nullptr;
    ULONG first = 0;
    bool valid = true;
    static thread_local Data* active;
};
thread_local SmPlanarCurveTransfer::Data* SmPlanarCurveTransfer::Data::active = nullptr;

SmPlanarCurveTransfer::SmPlanarCurveTransfer(const SmTArray<SmCurve*>& inputs)
{
    std::unique_ptr<Data> data(new Data);
    data->inputs = inputs;
    data->entries.reserve(inputs.GetSize());
    data->indices.reserve(inputs.GetSize());
    for (ULONG i = 0; i < inputs.GetSize(); ++i)
    {
        SmCurve* curve = inputs[i];
        // Repeated identity cannot have two independent ownership transfers.
        // Reject before construction, preserving every caller-owned input.
        if (!curve || !data->indices.emplace(curve, i).second)
        {
            data->valid = false;
        }
        data->entries.push_back({ curve, curve ? curve->GetOwner() : nullptr, nullptr });
    }
    m_data = data.release();
}
SmPlanarCurveTransfer::~SmPlanarCurveTransfer()
{
    delete m_data;
}
SmStatus SmPlanarCurveTransfer::Create(
    SmBrep& brep,
    SmRegion* region,
    double tolerance,
    SmTArray<SmFace*>& faces,
    SmTArray<SmBoolean>* transfers,
    ULONG first
)
{
    faces.ReSet();
    if (!m_data->valid || m_data->brep || (transfers && (first > transfers->GetSize() || m_data->inputs.GetSize() > transfers->GetSize() - first)))
    {
        return SM_ERR_INVALID_INPUT;
    }
    m_data->brep = &brep;
    m_data->transfers = transfers;
    m_data->first = first;
    if (transfers)
    {
        for (ULONG i = 0; i < m_data->inputs.GetSize(); ++i)
        {
            (*transfers)[first + i] = FALSE;
        }
    }
    // A scoped, thread-local observation preserves existing virtual signatures.
    // Nested calls restore the preceding scope, including exception unwinding;
    // records match both the destination BRep and the original curve identity.
    struct Scope
    {
        Data* data;
        explicit Scope(Data* value) : data(value)
        {
            data->previous = Data::active;
            Data::active = data;
        }
        ~Scope()
        {
            Data::active = data->previous;
        }
    } scope(m_data);
    const SmStatus status = brep.CreatePlanarFacesWith3DCurves(region, m_data->inputs, tolerance, faces);
#ifdef SM_PLANAR_TRANSFER_TESTING
    if (status == SM_SUCCESS && failAfterConstruction)
    {
        return SM_ERR;
    }
#endif
    if (status == SM_SUCCESS)
    {
        // The consuming planar contract requires every original input. A
        // filtered curve is not transferred merely because geometry succeeded.
        for (const auto& entry : m_data->entries)
        {
            if (!entry.edge)
            {
                return SM_ERR;
            }
        }
    }
    return status;
}
bool SmPlanarCurveTransfer::Transferred(ULONG input) const
{
    return m_data->entries.at(input).edge != nullptr;
}
bool SmPlanarCurveTransfer::AnyTransferred() const
{
    for (const auto& entry : m_data->entries)
    {
        if (entry.edge)
        {
            return true;
        }
    }
    return false;
}
void SmPlanarCurveTransfer::DetachInputs()
{
    // These are the actual recipient edges, not a scan of the BRep's edge list.
    // Notifications are suppressed only because the private candidate will be
    // destroyed immediately. This is not general rollback on a retained BRep.
    for (std::size_t i = 0; i < m_data->entries.size(); ++i)
    {
        auto& entry = m_data->entries[i];
        if (entry.edge)
        {
            entry.edge->SetCurve(nullptr, FALSE, FALSE, FALSE);
            entry.curve->SetOwner(entry.originalOwner);
            entry.edge = nullptr;
            if (m_data->transfers)
            {
                (*m_data->transfers)[m_data->first + static_cast<ULONG>(i)] = FALSE;
            }
        }
    }
}
void SmPlanarCurveTransfer::Adopted(SmBrep& brep, SmCurve* curve, SmEdge* edge) noexcept
{
    Data* data = Data::active;
    if (data && data->brep == &brep)
    {
        const auto found = data->indices.find(curve);
        if (found != data->indices.end())
        {
            data->entries[found->second].edge = edge;
            // Publish to the pre-sized caller ledger before any later throw or
            // return. Primitive cleanup may destroy the recipient before unwinding.
            if (data->transfers)
            {
                (*data->transfers)[data->first + static_cast<ULONG>(found->second)] = TRUE;
            }
        }
    }
}
void SmPlanarCurveTransfer::Returned(SmBrep& brep, SmCurve* curve, SmEdge* edge) noexcept
{
    Data* data = Data::active;
    if (data && data->brep == &brep)
    {
        const auto found = data->indices.find(curve);
        if (found != data->indices.end() && data->entries[found->second].edge == edge)
        {
            data->entries[found->second].edge = nullptr;
            if (data->transfers)
            {
                (*data->transfers)[data->first + static_cast<ULONG>(found->second)] = FALSE;
            }
        }
    }
}
#ifdef SM_PLANAR_TRANSFER_TESTING
void SmPlanarCurveTransfer::FailBeforeLinkAfter(long transfers, bool allocationFailure) noexcept
{
    failBeforeLink = transfers;
    throwBeforeLink = allocationFailure;
}
void SmPlanarCurveTransfer::FailAfterConstruction(bool fail) noexcept
{
    failAfterConstruction = fail;
}
SmStatus SmPlanarCurveTransfer::BeforeLink(SmBrep& brep)
{
    if (Data::active && Data::active->brep == &brep && failBeforeLink >= 0)
    {
        if (failBeforeLink-- == 0)
        {
            if (throwBeforeLink)
            {
                throw std::bad_alloc();
            }
            return SM_ERR;
        }
    }
    return SM_SUCCESS;
}
#endif
