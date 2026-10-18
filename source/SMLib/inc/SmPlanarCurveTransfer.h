// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <SmTArray.h>

class SmBrep;
class SmCurve;
class SmEdge;
class SmFace;
class SmRegion;

// Internal planar-construction seam, not a new SM_API/Python contract. The
// record owns no geometry; callers keep their existing failure policies.
class SM_EXPORT SmPlanarCurveTransfer
{
public:

    explicit SmPlanarCurveTransfer(const SmTArray<SmCurve*>& inputs);
    ~SmPlanarCurveTransfer();
    SmPlanarCurveTransfer(const SmPlanarCurveTransfer&) = delete;
    SmPlanarCurveTransfer& operator=(const SmPlanarCurveTransfer&) = delete;

    SmStatus Create(
        SmBrep& brep,
        SmRegion* region,
        double tolerance,
        SmTArray<SmFace*>& faces,
        SmTArray<SmBoolean>* transfers = nullptr,
        ULONG first = 0
    );
    bool Transferred(ULONG input) const;
    bool AnyTransferred() const;
    void DetachInputs(); // Before destroying a private failed candidate; restores original owners.

    // Called at actual attachment/rollback, including before the edge is in the
    // BRep list. Storage and lookup are prepared before entering construction.
    static void Adopted(SmBrep& brep, SmCurve* curve, SmEdge* edge) noexcept;
    static void Returned(SmBrep& brep, SmCurve* curve, SmEdge* edge) noexcept;
#ifdef SM_PLANAR_TRANSFER_TESTING
    static void FailBeforeLinkAfter(long transfers, bool allocationFailure = false) noexcept;
    static void FailAfterConstruction(bool fail) noexcept;
    static SmStatus BeforeLink(SmBrep& brep);
#endif

private:

    struct Data;
    Data* m_data;
};
