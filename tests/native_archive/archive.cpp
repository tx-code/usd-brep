// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmAttribute.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmCone.h>
#include <SmContext.h>
#include <SmCylinder.h>
#include <SmDatabaseIO.h>
#include <SmEdge.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#include <SmNativeArchive.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
void Require(bool value, const char* reason)
{
    if (!value)
    {
        throw std::runtime_error(reason);
    }
}
struct Part
{
    // Owners are destroyed before their associated context, on every return.
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> cleanCurves{ &curves };
    SmObjsDelete<SmSurface*> cleanSurfaces{ &surfaces };
    SmObjsDelete<SmBrep*> cleanBreps{ &breps };
    SmStatus Read(const std::filesystem::path& path, SmFileType type, bool legacy = false)
    {
        if (legacy)
        {
            return SmBrepData::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, breps, type);
        }
        return SmNativeArchive::ReadPartFromFile(context, path.c_str(), curves, surfaces, trees, breps, type);
    }
    bool Empty() const
    {
        return !curves.GetSize() && !surfaces.GetSize() && !trees.GetSize() && !breps.GetSize();
    }
};
struct FailAtEnd : SmDatabaseIOFile
{
    SmStatus EndReading() override
    {
        return SM_ERR;
    }
};
void Curve(const SmCurve& a, const SmCurve& b, double textRounding)
{
    Require(a.GetType() == b.GetType() && a.GetDim() == b.GetDim() && a.GetDegree() == b.GetDegree(), "curve class/dimension/degree");
    const auto ca = dynamic_cast<const SmBSplineCurve*>(&a), cb = dynamic_cast<const SmBSplineCurve*>(&b);
    Require(ca && cb, "B-spline curve data");
    SmTArray<SmPoint3d> pa, pb;
    SmTArray<double> wa, wb, ka, kb;
    Require(ca->GetControlPolygon(pa, wa) == SM_SUCCESS && cb->GetControlPolygon(pb, wb) == SM_SUCCESS, "curve polygon");
    Require(ca->GetKnotsAll(ka) == SM_SUCCESS && cb->GetKnotsAll(kb) == SM_SUCCESS, "curve knots");
    Require(pa.GetSize() == pb.GetSize() && wa.GetSize() == wb.GetSize() && ka.GetSize() == kb.GetSize(), "curve counts");
    for (ULONG i = 0; i < pa.GetSize(); ++i)
    {
        Require(pa[i].DistanceBetween(pb[i]) <= textRounding, "curve control identity");
    }
    for (ULONG i = 0; i < wa.GetSize(); ++i)
    {
        Require(std::abs(wa[i] - wb[i]) <= textRounding, "curve weight identity");
    }
    for (ULONG i = 0; i < ka.GetSize(); ++i)
    {
        Require(ka[i] == kb[i], "curve knot identity");
    }
}
void Surface(const SmSurface& a, const SmSurface& b, double textRounding)
{
    Require(a.GetType() == b.GetType(), "surface class preservation");
    const auto sa = dynamic_cast<const SmBSplineSurface*>(&a), sb = dynamic_cast<const SmBSplineSurface*>(&b);
    Require(sa && sb && sa->GetDegree(SM_SP_U) == sb->GetDegree(SM_SP_U) && sa->GetDegree(SM_SP_V) == sb->GetDegree(SM_SP_V), "surface degrees");
    ULONG au = 0, av = 0, bu = 0, bv = 0;
    double *pa = nullptr, *pb = nullptr;
    Require(sa->GetControlPointsPointer(au, av, pa) == SM_SUCCESS && sb->GetControlPointsPointer(bu, bv, pb) == SM_SUCCESS, "surface polygon");
    Require(au == bu && av == bv, "surface control counts");
    for (ULONG i = 0; i < au * av * 4; ++i)
    {
        Require(std::abs(pa[i] - pb[i]) <= textRounding, "surface homogeneous control identity");
    }
    double *ka = nullptr, *la = nullptr, *kb = nullptr, *lb = nullptr;
    Require(sa->GetKnotsPointers(au, av, ka, la) == SM_SUCCESS && sb->GetKnotsPointers(bu, bv, kb, lb) == SM_SUCCESS, "surface knots");
    Require(au == bu && av == bv, "surface knot counts");
    for (ULONG i = 0; i < au; ++i)
    {
        Require(ka[i] == kb[i], "surface U knot identity");
    }
    for (ULONG i = 0; i < av; ++i)
    {
        Require(la[i] == lb[i], "surface V knot identity");
    }
    const auto da = sa->GetNaturalUVDomain(), db = sb->GetNaturalUVDomain();
    Require(
        da.GetMin().x == db.GetMin().x && da.GetMin().y == db.GetMin().y && da.GetMax().x == db.GetMax().x && da.GetMax().y == db.GetMax().y,
        "surface natural parameter domain"
    );
    // Exact stored coefficients and knots establish basis identity. These
    // evaluations additionally exercise analytic subclasses' parameter caches;
    // they are observations, not a sampled substitute for that identity check.
    for (unsigned u = 0; u < 5; ++u)
    {
        for (unsigned v = 0; v < 5; ++v)
        {
            SmPoint2d uv(da.GetMin().x + da.GetUInterval().GetLength() * u / 4., da.GetMin().y + da.GetVInterval().GetLength() * v / 4.);
            SmPoint3d x, y;
            Require(
                a.EvaluatePoint(uv, x) == SM_SUCCESS && b.EvaluatePoint(uv, y) == SM_SUCCESS && x.DistanceBetween(y) <= 1e-12,
                "same-UV evaluation"
            );
        }
    }
}
void Body(SmBrep& a, SmBrep& b, double textRounding)
{
    SmAssertArray pointers, deep;
    Require(
        b.ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize() && b.AssertValid(&deep, SM_LEVEL_2, SM_WALK) && !deep.GetSize(),
        "readback native validity"
    );
    Require(a.GetTolerance() == b.GetTolerance(), "stored work tolerance");
    SmBrepCompareResult topology;
    Require(
        a.CompareBreps(&b, topology) == SM_SUCCESS && topology.lFaceCountA == topology.lFaceCountB && topology.lEdgeCountA == topology.lEdgeCountB &&
            topology.lVertexCountA == topology.lVertexCountB && !topology.lCurveTypeMismatches && !topology.lSurfaceTypeMismatches &&
            topology.dMaxVertexGap <= textRounding,
        "stored topology and vertices"
    );
    const auto attr = b.FindAttribute(SM_AI_USER_1);
    Require(attr && attr->GetNumLongElements() == 1 && *attr->GetLongElementsAddress() == 42, "stored body attribute");
    SmTArray<SmEdge*> ea, eb;
    a.GetEdges(ea);
    b.GetEdges(eb);
    for (ULONG i = 0; i < ea.GetSize(); ++i)
    {
        Require(
            ea[i]->GetInterval().GetMin() == eb[i]->GetInterval().GetMin() && ea[i]->GetInterval().GetMax() == eb[i]->GetInterval().GetMax(),
            "edge parameter interval"
        );
        Curve(*ea[i]->GetCurve(), *eb[i]->GetCurve(), textRounding);
    }
    SmTArray<SmFace*> fa, fb;
    a.GetFaces(fa);
    b.GetFaces(fb);
    for (ULONG i = 0; i < fa.GetSize(); ++i)
    {
        Surface(*fa[i]->GetSurface(), *fb[i]->GetSurface(), textRounding);
        SmTArray<SmEdgeuse*> ua, ub;
        fa[i]->GetUpwardEdgeuses(ua);
        fb[i]->GetUpwardEdgeuses(ub);
        Require(ua.GetSize() == ub.GetSize(), "UV inventory");
        for (ULONG k = 0; k < ua.GetSize(); ++k)
        {
            Require(ua[k]->GetUVTrimCurve() && ub[k]->GetUVTrimCurve(), "UV presence");
            Curve(*ua[k]->GetUVTrimCurve(), *ub[k]->GetUVTrimCurve(), textRounding);
        }
    }
}
void Fixture(Part& part)
{
    SmCone* rawCone = nullptr;
    Require(SmCone::CreateCanonical(part.context, SmAxis2Placement(), 3., 5., 7., rawCone) == SM_SUCCESS, "cone fixture");
    std::unique_ptr<SmCone> cone(rawCone);
    std::unique_ptr<SmCylinder> cylinder(new (
        part.context
    ) SmCylinder(SmPoint3d(2, -3, 5), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), 3., 0., 180., 7., FALSE, FALSE, FALSE, &part.context));
    for (unsigned kind = 0; kind < 3; ++kind)
    {
        // A true rational conic basis, deliberately stored as a GENERIC spline,
        // beside analytic cone/cylinder controls. Legacy recognition promotes
        // the first to SmCone. Its shifted/scaled native UV must remain intact.
        std::unique_ptr<SmSurface> support;
        if (!kind)
        {
            support.reset(new (part.context) SmBSplineSurface(static_cast<const SmBSplineSurface&>(*cone)));
            Require(support->Reparameterize(SmExtent2d(-2, 3, 5, 11)) == SM_SUCCESS, "fixture parameterization");
        }
        else
        {
            SmSurface* raw = nullptr;
            Require((kind == 1 ? cone->Copy(part.context, raw) : cylinder->Copy(part.context, raw)) == SM_SUCCESS, "analytic fixture copy");
            support.reset(raw);
        }
        std::unique_ptr<SmBrep> brep(new (part.context) SmBrep());
        brep->SetTolerance(1e-6);
        const auto domain = support->GetNaturalUVDomain();
        SmFace* face = nullptr;
        const auto status = brep->CreateFaceFromSurface(support.get(), domain, face);
        if (support->GetOwner())
        {
            support.release();
        }
        Require(status == SM_SUCCESS && face, "fixture face");
        double a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
        Require(face->CreateUVTrimCurves(FALSE, nullptr, nullptr, a, b, c, d, e, f) == SM_SUCCESS, "fixture installed UV");
        SmAssertArray pointers, deep;
        Require(
            brep->ValidatePointers(&pointers) == SM_SUCCESS && !pointers.GetSize() && brep->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
                !deep.GetSize(),
            "fixture validity"
        );
        brep->AddAttribute(new (part.context) SmLongAttribute(SM_AI_USER_1, 42));
        part.breps.Add(brep.release());
    }
    SmSurface* standalone = nullptr;
    Require(cone->Copy(part.context, standalone) == SM_SUCCESS, "standalone surface");
    part.surfaces.Add(standalone);
    SmTArray<SmEdge*> edges;
    part.breps[0]->GetEdges(edges);
    SmCurve* curve = nullptr;
    Require(edges.GetSize() && edges[0]->GetCurve()->Copy(part.context, curve) == SM_SUCCESS, "standalone curve");
    part.curves.Add(curve);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "supply disposable output directory");
        const auto directory = std::filesystem::path(argv[1]);
        std::filesystem::create_directories(directory);
        Part original;
        Fixture(original);
        for (const auto type : { SM_BINARY, SM_ASCII })
        {
            // Binary archives must retain every stored coefficient exactly.
            // The unchanged ASCII codec emits 16 significant digits; its
            // carrier rounding allowance is below the existing 1 nm gate.
            const double textRounding = type == SM_BINARY ? 0. : 1e-12;
            const auto path = directory / (type == SM_BINARY ? "definitions.smb" : "definitions.smt");
            Require(
                SmBrepData::WritePartToFile(path.c_str(), original.curves, original.surfaces, original.trees, original.breps, type, TRUE, FALSE) ==
                    SM_SUCCESS,
                "native write without spline conversion"
            );
            Part read;
            Require(
                read.Read(path, type) == SM_SUCCESS && read.breps.GetSize() == 3 && read.curves.GetSize() == 1 && read.surfaces.GetSize() == 1,
                "complete preserving native read"
            );
            for (ULONG i = 0; i < read.breps.GetSize(); ++i)
            {
                Body(*original.breps[i], *read.breps[i], textRounding);
            }
            Curve(*original.curves[0], *read.curves[0], textRounding);
            Surface(*original.surfaces[0], *read.surfaces[0], textRounding);
            // A rejected nonempty destination retains its original ownership.
            const auto first = read.breps[0];
            Require(read.Read(path, type) != SM_SUCCESS && read.breps.GetSize() == 3 && read.breps[0] == first, "nonempty output precondition");
            if (type == SM_BINARY)
            {
                Part legacy;
                Require(legacy.Read(path, type, true) == SM_SUCCESS, "legacy reader compatibility");
                SmTArray<SmFace*> faces;
                legacy.breps[0]->GetFaces(faces);
                Require(faces.GetSize() == 1 && faces[0]->GetSurface()->GetType() == SmCone_TYPE, "legacy recognition remains explicit control");
                // Fail after all bodies have been constructed. The stream is
                // otherwise valid: this exercises cleanup/publication without
                // depending on legacy handling of arbitrary malformed bytes.
                for (unsigned attempt = 0; attempt < 20; ++attempt)
                {
                    Part failed;
                    FailAtEnd database;
                    Require(database.OpenFileForRead(path.c_str(), SM_BINARY) == SM_SUCCESS, "failure control input");
                    Require(
                        SmNativeArchive::ReadPartFromDB(failed.context, failed.curves, failed.surfaces, failed.trees, failed.breps, database) !=
                                SM_SUCCESS &&
                            failed.Empty(),
                        "failed read publishes no partial objects"
                    );
                }
            }
        }
        Part missing;
        Require(missing.Read(directory / "missing.smb", SM_BINARY) != SM_SUCCESS && missing.Empty(), "missing input publishes nothing");
        std::cout << "Native archive definitions, UV, legacy control and atomic failure contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
