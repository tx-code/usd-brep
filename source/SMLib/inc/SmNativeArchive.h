// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef SM_NATIVE_ARCHIVE_H
#define SM_NATIVE_ARCHIVE_H

#include <SmTArray.h>
#include <SmTypes.h>

class SmBrep;
class SmContext;
class SmCurve;
class SmDatabaseIO;
class SmSurface;

// Native part codec with stored-definition semantics. Unlike the legacy
// SmBrepData reader, this entry disables analytic recognition, UV regeneration
// and version-triggered healing in MakeTopologyFromData. It retains stored
// geometry classes, NURBS/UV parameters and positive BRep work tolerances;
// it does not certify source equivalence or native validity. Callers must run
// their own acceptance checks. The native file format is unchanged.
class SM_EXPORT SmNativeArchive
{
public:

    // Output arrays must be empty. On failure they remain empty; nonempty
    // inputs are rejected unchanged. On success the caller owns all returned
    // objects and must destroy them before crContext. File I/O is synchronous;
    // callers provide cancellation/serialization around this native boundary.
    static SmStatus ReadPartFromFile(
        const SmContext& crContext,
        const TCHAR* cInputFileName,
        SmTArray<SmCurve*>& r3DCurves,
        SmTArray<SmSurface*>& rSurfaces,
        SmTArray<long>& rBooleanTreeNodes,
        SmTArray<SmBrep*>& rBreps,
        SmFileType eType = SM_ASCII
    );

    // Same ownership/publication contract for an already opened native stream.
    // The caller owns the database and its stream for the duration of the call.
    static SmStatus ReadPartFromDB(
        const SmContext& crContext,
        SmTArray<SmCurve*>& r3DCurves,
        SmTArray<SmSurface*>& rSurfaces,
        SmTArray<long>& rBooleanTreeNodes,
        SmTArray<SmBrep*>& rBreps,
        SmDatabaseIO& rDB
    );
};

#endif
