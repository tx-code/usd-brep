// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmAssertArray.h>
#include <SmAttribute.h>
#include <SmBSplineCurve.h>
#include <SmBSplineSurface.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmCrvOnSurf.h>
#include <SmCurveClass.h>
#include <SmDatabaseIO.h>
#include <SmEdge.h>
#include <SmFace.h>
#include <SmNativeArchive.h>
#include <SmPlane.h>
#include <SmSurfOfRevolution.h>
#include <SmSurfaceWorkPrecision.h>
#include <SmTol.h>
#include <SmVertex.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <nurbs.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
namespace Work = SmSurfaceWorkPrecision;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
std::unique_ptr<SmSurfOfRevolution> Definition(SmContext& context)
{
    // Public near-axis frustum: precision metadata must leave this generating
    // net untouched. Record the ordinary native classification independently;
    // a nonzero analytic radius alone does not prescribe native Test5's result.
    gw_CPOINT controls[] = { { 1, 0, 1024, 1 }, { 0x1p-30, 0, 1025, 1 } };
    double knots[] = { 0, 0, 1, 1 };
    std::unique_ptr<SmBSplineCurve> generator(new (context) SmBSplineCurve(3, 1, 3, 1, controls, knots, &context));
    SmSurfOfRevolution* raw = nullptr;
    const auto status = SmSurfOfRevolution::CreateCanonical(context, generator.get(), SmPoint3d(0, 0, 1024), SmVector3d(0, 0, 1), raw, FALSE);
    std::unique_ptr<SmSurfOfRevolution> surface(raw);
    if (surface && generator->GetOwner() == surface.get())
    {
        generator.release();
    }
    Require(status == SM_SUCCESS && surface && !generator, "public SOR construction adopts its generator");
    return surface;
}
bool Valid(const SmSurface& surface)
{
    SmAssertArray findings;
    return surface.AssertValid(&findings, SM_LEVEL_2, SM_WALK) != FALSE && findings.GetSize() == 0;
}
std::vector<double> GeneratorNet(const SmSurfOfRevolution& surface)
{
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    Require(surface.GetCanonical(generator, origin, axis) == SM_SUCCESS && generator, "stored generator exists");
    Require(generator->GetOwner() == &surface && generator->GetContext() == surface.GetContext(), "reciprocal generator ownership");
    ULONG count = 0;
    double* values = nullptr;
    Require(generator->GetControlPointsPointer(count, values) == SM_SUCCESS && count == 2 && values, "complete generator net");
    return { values, values + std::size_t(count) * 4 };
}
void Check(const SmSurfOfRevolution& surface, double raw, double scale = 1, double textRounding = 0)
{
    double actual = -1;
    Require(Work::Get(surface, actual) == SM_SUCCESS && actual == raw, "exact raw scalar belongs to this surface");
    const auto* owner = dynamic_cast<const SmFace*>(surface.GetOwner());
    const double existing = owner ? double(owner->GetTolerance()) : double(SmTol::GetZoneTol3d());
    Require(
        double(SmTol::GetZoneTol3d(&surface)) == existing && double(SmTol::GetApproxTol3d(&surface)) == .5 * existing,
        "registered numerical precision never changes native coincidence or approximation zones"
    );
    Require(
        Work::GetEffective(surface, actual) == SM_SUCCESS && actual == std::min(existing, raw),
        "the separate effective numerical resolution cannot exceed its legacy zone"
    );
    Require(Valid(surface), "all original native surface LEVEL2 predicates pass");
    const auto net = GeneratorNet(surface);
    const double expected[] = { scale, 0, 1024 * scale, 1, 0x1p-30 * scale, 0, 1025 * scale, 1 };
    for (unsigned i = 0; i < 8; ++i)
    {
        Require(std::abs(net[i] - expected[i]) <= textRounding, "work precision preserves every defining generator coefficient");
    }
    // Independent equation uses STEP degrees and unchanged generator t.
    for (double v : { 0., .25, .75, 1. })
    {
        for (double u : { 0., 90., 180. })
        {
            SmPoint3d point;
            Require(surface.EvaluateSTEPPoint(SmPoint2d(u, v), point) == SM_SUCCESS, "actual STEP point evaluation");
            const double radius = 1 + (0x1p-30 - 1) * v;
            const double radians = u * std::numbers::pi / 180;
            Require(
                std::abs(point.x - scale * radius * std::cos(radians)) <= 2e-12 * scale &&
                    std::abs(point.y - scale * radius * std::sin(radians)) <= 2e-12 * scale &&
                    std::abs(point.z - scale * (1024 + v)) <= 2e-12 * scale,
                "unchanged public frustum equation"
            );
        }
    }
}
std::unique_ptr<SmGenericAttribute> Attribute(SmContext& context, double value, SmAttributeBehaviorType behavior = SM_AB_COPY)
{
    const auto bits = std::bit_cast<std::uint64_t>(value);
    SmTArray<long> words;
    words.Add(1);
    for (unsigned i = 0; i < 4; ++i)
    {
        words.Add(static_cast<long>((bits >> (16 * i)) & 0xffff));
    }
    SmTArray<double> doubles;
    SmTArray<char> chars;
    return std::unique_ptr<SmGenericAttribute>(new (context) SmGenericAttribute(Work::AttributeId, behavior, words, doubles, chars));
}
class TestFace : public SmFace
{
public:

    TestFace() = default;
    ~TestFace() override = default;
};
class BadFrame : public SmAxis2Placement
{
public:

    explicit BadFrame(bool nonfinite)
    {
        if (nonfinite)
        {
            m_vOrigin.x = std::numeric_limits<double>::infinity();
        }
        else
        {
            m_vXAxis = SmVector3d(2, 0, 0);
        }
    }
};
void InvalidMetadata(SmContext& context, double precision)
{
    auto surface = Definition(context);
    Require(Work::Set(*surface, precision) == SM_SUCCESS, "install a public work precision");
    auto* original = surface->FindAttribute(Work::AttributeId);
    auto duplicate = Attribute(context, precision);
    surface->AddAttribute(duplicate.get());
    double value = 0;
    SmSurface* copied = nullptr;
    Require(
        Work::Get(*surface, value) != SM_SUCCESS && Work::GetEffective(*surface, value) != SM_SUCCESS && value == 0 && !Valid(*surface) &&
            surface->Copy(context, copied) != SM_SUCCESS && !copied,
        "duplicate policy refuses before Copy can normalize IDs"
    );
    surface->RemoveAttribute(duplicate.get(), TRUE);
    auto other = Definition(context);
    other->AddAttribute(original);
    Require(Work::Get(*surface, value) != SM_SUCCESS && Work::Get(*other, value) != SM_SUCCESS, "shared COPY policy refuses on every user");
    Require(
        Work::GetEffective(*surface, value) != SM_SUCCESS && Work::GetEffective(*other, value) != SM_SUCCESS && value == 0,
        "shared policy cannot select an effective resolution"
    );
    other->RemoveAttribute(original);
    auto* generic = static_cast<SmGenericAttribute*>(original);
    for (const auto bad : { std::pair<ULONG, long>{ 0, 2 }, { 1, -1 }, { 1, 65536 } })
    {
        const long saved = generic->GetLongElementsAddress()[bad.first];
        generic->SetLongElement(bad.first, bad.second);
        Require(
            Work::Get(*surface, value) != SM_SUCCESS && Work::GetEffective(*surface, value) != SM_SUCCESS && value == 0 && !Valid(*surface),
            "version and word range are validated by both raw and effective queries"
        );
        generic->SetLongElement(bad.first, saved);
    }
    Require(Work::Set(*surface, 0) == SM_SUCCESS, "clear valid policy");
    for (double bad : { 0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
    {
        auto malformed = Attribute(context, bad);
        surface->AddAttribute(malformed.get());
        Require(
            Work::Get(*surface, value) != SM_SUCCESS && Work::GetEffective(*surface, value) != SM_SUCCESS && value == 0 && !Valid(*surface),
            "encoded nonpositive or nonfinite scalar refuses both queries"
        );
        Require(surface->Transform(SmAxis2Placement()) != SM_SUCCESS, "even identity Transform rejects invalid metadata");
        surface->RemoveAttribute(malformed.get(), TRUE);
    }
    auto reference = Attribute(context, precision, SM_AB_REFERENCE);
    surface->AddAttribute(reference.get());
    Require(Work::Get(*surface, value) != SM_SUCCESS, "reference behavior cannot carry a mutable physical scalar");
    surface->RemoveAttribute(reference.get(), TRUE);
    Require(Work::Set(*surface, precision) == SM_SUCCESS, "restore valid precision");
    surface->SetOwner(surface.get());
    Require(Work::Get(*surface, value) != SM_SUCCESS && !Valid(*surface), "cyclic fake owner refuses without following a chain");
    Require(Work::GetEffective(*surface, value) != SM_SUCCESS && value == 0, "effective query refuses a cyclic owner before calling SmTol");
    Require(std::isfinite(double(SmTol::GetZoneTol3d(surface.get()))), "invalid policy tolerance fallback terminates");
    surface->SetOwner(nullptr);
    Check(*surface, precision);
}
void FaceOwnership(SmContext& context, double precision)
{
    auto surface = Definition(context);
    // Default initialization preserves the context written by native new;
    // value initialization of this defaulted test class would zero it again.
    std::unique_ptr<TestFace> face(new (surface.get()) TestFace);
    face->SetTolerance(.001, FALSE, FALSE);
    face->SetSurface(surface.get());
    surface.release(); // The real reciprocal face owns this exact surface now.
    auto& owned = *static_cast<SmSurfOfRevolution*>(face->GetSurface());
    const double topologyTolerance = face->GetTolerance();
    const double surfaceZone = SmTol::GetZoneTol3d(&owned);
    const double approximationZone = SmTol::GetApproxTol3d(&owned);
    const double coincidenceZone = SmTol::GetXSectTol3d(&owned, &owned);
    SmCurveClassification missing;
    Require(
        face->CurveOnClassify(FALSE, missing) == SM_ERR_INVALID_INPUT && missing.GetCurve() == nullptr,
        "missing defining classification curve returns an input error, never dereferences null"
    );
    Require(Work::Set(owned, precision) == SM_SUCCESS, "a true owning face permits a local geometry policy");
    Check(owned, precision);
    Require(
        double(face->GetTolerance()) == topologyTolerance && double(SmTol::GetZoneTol3d(&owned)) == surfaceZone &&
            double(SmTol::GetApproxTol3d(&owned)) == approximationZone && double(SmTol::GetXSectTol3d(&owned, &owned)) == coincidenceZone,
        "face topology, geometry approximation and coincidence tolerances all stay unchanged"
    );
    auto unrelated = Definition(context);
    Require(unrelated->SetOwner(face.get()) == nullptr, "prepare a public false backlink control");
    Require(Work::Set(*unrelated, precision) != SM_SUCCESS, "a fake face owner cannot grant the policy");
    unrelated->SetOwner(nullptr);
}
class Observer : public SmNotifyCallback
{
public:

    SmSurface* target = nullptr;
    unsigned action = 0;
    bool fired = false;
    double replacement = 0;
    SmStatus Execute(SmNotifyOperation operation, SmObject* object, SmObject*, SmObject*, SmObject*) override
    {
        if (!fired && object == target && operation == SM_NO_PRE_EDIT)
        {
            fired = true;
            if (action == 3)
            {
                throw std::domain_error("PublicSurfaceObserverException");
            }
            if (action == 4)
            {
                target->SetOwner(target);
            }
            else
            {
                Require(Work::Set(*target, action == 1 ? 0 : replacement) == SM_SUCCESS, "observer can edit its own policy");
            }
        }
        return SM_SUCCESS;
    }
};
class ObserverScope
{
public:

    ObserverScope(SmContext& context, SmNotifyCallback& observer) : context(context), previous(context.GetUserNotifyCallback())
    {
        context.SetUserNotifyCallback(&observer);
    }
    ~ObserverScope()
    {
        context.SetUserNotifyCallback(previous);
    }

private:

    SmContext& context;
    SmNotifyCallback* previous;
};
void WrongSurfaceClass(SmContext& context, double precision)
{
    SmPlane* raw = nullptr;
    Require(SmPlane::CreateCanonical(context, SmAxis2Placement(), raw) == SM_SUCCESS && raw, "wrong-class plane construction");
    std::unique_ptr<SmPlane> plane(raw);
    std::unique_ptr<SmBSplineSurface> spline(new (context) SmBSplineSurface(*plane));
    Require(Valid(*plane) && Valid(*spline), "unregistered plane and plain B-spline pass original LEVEL2");
    for (SmSurface* surface : { static_cast<SmSurface*>(plane.get()), static_cast<SmSurface*>(spline.get()) })
    {
        auto attribute = Attribute(context, precision);
        surface->AddAttribute(attribute.get());
        double value = 0;
        SmAssertArray findings;
        Require(
            Work::Get(*surface, value) != SM_SUCCESS && Work::GetEffective(*surface, value) != SM_SUCCESS && value == 0 &&
                !surface->AssertValid(&findings, SM_LEVEL_2, SM_WALK) && findings.GetSize() > 0,
            "native LEVEL2 rejects well-formed precision metadata on every unsupported surface class"
        );
        surface->RemoveAttribute(attribute.get(), TRUE);
        Require(Valid(*surface), "removing the synthetic defect restores original LEVEL2 behavior");
    }
}
template <class Surface>
SmFace* AddPatch(SmBrep& brep, std::unique_ptr<Surface>& surface)
{
    SmFace* face = nullptr;
    const auto status = brep.CreateFaceFromSurface(surface.get(), SmExtent2d(.125, .125, .375, .75), face);
    if (surface->GetOwner())
    {
        surface.release();
    }
    Require(status == SM_SUCCESS && face && !surface, "public patch adopts its unchanged surface");
    return face;
}
struct BrepState
{
    std::vector<const SmObject*> identities;
    std::vector<double> definitions;
    bool operator==(const BrepState&) const = default;
};
BrepState Capture(const SmBrep& brep)
{
    BrepState state;
    state.definitions.push_back(brep.GetTolerance());
    SmTArray<SmFace*> faces;
    SmTArray<SmEdge*> edges;
    SmTArray<SmVertex*> vertices;
    brep.GetFaces(faces);
    brep.GetEdges(edges);
    brep.GetVertices(vertices);
    state.definitions.insert(state.definitions.end(), { double(faces.GetSize()), double(edges.GetSize()), double(vertices.GetSize()) });
    for (ULONG i = 0; i < faces.GetSize(); ++i)
    {
        const auto* face = faces[i];
        const auto* surface = face->GetSurface();
        const auto* spline = dynamic_cast<const SmBSplineSurface*>(surface);
        Require(spline, "public conversion fixture has native NURBS-backed surfaces");
        state.identities.insert(state.identities.end(), { face, surface, surface->GetOwner(), surface->FindAttribute(Work::AttributeId) });
        state.definitions.push_back(face->GetTolerance());
        ULONG countU = 0, countV = 0, knotsU = 0, knotsV = 0;
        double *controls = nullptr, *u = nullptr, *v = nullptr;
        Require(
            spline->GetControlPointsPointer(countU, countV, controls) == SM_SUCCESS && controls &&
                spline->GetKnotsPointers(knotsU, knotsV, u, v) == SM_SUCCESS && u && v,
            "complete stored surface definition before conversion"
        );
        state.definitions.insert(
            state.definitions.end(),
            { double(surface->GetType()),
              double(countU),
              double(countV),
              double(knotsU),
              double(knotsV),
              double(spline->GetDegree(SM_SP_U)),
              double(spline->GetDegree(SM_SP_V)) }
        );
        state.definitions.insert(state.definitions.end(), controls, controls + std::size_t(countU) * countV * 4);
        state.definitions.insert(state.definitions.end(), u, u + knotsU);
        state.definitions.insert(state.definitions.end(), v, v + knotsV);
        double value = 0;
        Require(Work::Get(*surface, value) == SM_SUCCESS, "captured policy remains valid");
        state.definitions.push_back(value);
        if (const auto* revolution = dynamic_cast<const SmSurfOfRevolution*>(surface))
        {
            const auto generator = GeneratorNet(*revolution);
            state.definitions.insert(state.definitions.end(), generator.begin(), generator.end());
        }
    }
    for (ULONG i = 0; i < edges.GetSize(); ++i)
    {
        const auto* edge = edges[i];
        state.identities.insert(state.identities.end(), { edge, edge->GetCurve() });
        state.definitions.insert(
            state.definitions.end(),
            { edge->GetInterval().GetMin(), edge->GetInterval().GetMax(), double(edge->GetTolerance()) }
        );
    }
    for (ULONG i = 0; i < vertices.GetSize(); ++i)
    {
        const auto* vertex = vertices[i];
        const auto point = vertex->GetPoint();
        state.identities.push_back(vertex);
        state.definitions.insert(state.definitions.end(), { point.x, point.y, point.z, double(vertex->GetTolerance()) });
    }
    return state;
}
class NotificationCount : public SmNotifyCallback
{
public:

    unsigned count = 0;
    SmStatus Execute(SmNotifyOperation, SmObject*, SmObject*, SmObject*, SmObject*) override
    {
        ++count;
        return SM_SUCCESS;
    }
};
SmStatus Convert(SmBrep& brep, unsigned operation)
{
    if (operation == 0)
    {
        return brep.TurnToNURBS();
    }
    if (operation == 1)
    {
        return brep.TurnToNURBSAndSubdivide(2, .5);
    }
    const SmVector3d nonuniform(2, 3, 2);
    return brep.Transform(SmAxis2Placement(), &nonuniform, TRUE);
}
void ConversionRefusal(SmContext& context, double precision)
{
    std::unique_ptr<SmBrep> brep(new (context) SmBrep());
    SmPlane* rawPlane = nullptr;
    Require(SmPlane::CreateCanonical(context, SmAxis2Placement(), rawPlane) == SM_SUCCESS && rawPlane, "conversion plane construction");
    std::unique_ptr<SmPlane> plane(rawPlane);
    AddPatch(*brep, plane); // A preceding unrelated surface must not be converted first.
    auto surface = Definition(context);
    Require(Work::Set(*surface, precision) == SM_SUCCESS, "conversion refusal retains a valid registered SOR");
    auto* original = surface.get();
    const auto* policy = surface->FindAttribute(Work::AttributeId);
    AddPatch(*brep, surface);
    Check(*original, precision);
    const auto before = Capture(*brep);
    for (unsigned operation = 0; operation < 3; ++operation)
    {
        NotificationCount observer;
        SmStatus status = SM_SUCCESS;
        {
            ObserverScope scope(context, observer);
            status = Convert(*brep, operation);
        }
        Require(status == SM_ERR_INVALID_INPUT && observer.count == 0, "registered conversion refuses before any notification");
        Require(
            Capture(*brep) == before,
            "rejected conversion preserves all surface identities, nets, knots, vertices, edges and topology tolerances"
        );
        Require(original->FindAttribute(Work::AttributeId) == policy, "refusal neither drops nor replaces the original policy attribute");
        Check(*original, precision);
    }
    for (unsigned operation = 0; operation < 3; ++operation)
    {
        std::unique_ptr<SmBrep> ordinary(new (context) SmBrep());
        // Subdivision operates on the complete surface domain, so its normal
        // success control uses a finite plane rather than canonical infinity.
        std::unique_ptr<SmPlane> input(
            new (context) SmPlane(SmPoint3d(0, 0, 0), SmVector3d(1, 0, 0), SmVector3d(0, 1, 0), SmVector2d(1, 1), SmExtent2d(0, 0, 1, 1), &context)
        );
        AddPatch(*ordinary, input);
        Require(Convert(*ordinary, operation) == SM_SUCCESS, "unregistered conversion, subdivision and nonuniform transform retain success");
        SmTArray<SmSurface*> surfaces;
        ordinary->GetSurfaces(surfaces);
        Require(
            surfaces.GetSize() == 1 && surfaces[0]->GetType() == SmBSplineSurface_TYPE && Valid(*surfaces[0]),
            "ordinary conversion yields a valid plain B-spline"
        );
    }
}
void Notifications(SmContext& context, double precision)
{
    for (unsigned action = 1; action <= 4; ++action)
    {
        auto surface = Definition(context);
        Require(Work::Set(*surface, precision) == SM_SUCCESS, "notification control policy");
        Observer observer;
        observer.target = surface.get();
        observer.action = action;
        observer.replacement = precision / 2;
        bool actualException = false;
        SmStatus status = SM_SUCCESS;
        const SmVector3d scale(2, 2, 2);
        {
            ObserverScope scope(context, observer);
            try
            {
                status = surface->Transform(SmAxis2Placement(), &scale);
            }
            catch (const std::domain_error& error)
            {
                actualException = std::string(error.what()) == "PublicSurfaceObserverException";
            }
        }
        Require(
            observer.fired && (action == 3 ? actualException : status != SM_SUCCESS),
            "observer edits refuse and actual exceptions retain identity"
        );
        if (action == 4)
        {
            surface->SetOwner(nullptr);
        }
        double value = 0;
        Require(
            Work::Get(*surface, value) == SM_SUCCESS && value == (action == 1 ? 0 :
                                                                  action == 2 ? precision / 2 :
                                                                                precision),
            "failed transform does not overwrite the observer's policy edit"
        );
        Require(GeneratorNet(*surface)[0] == 1, "pre-notification refusal does not transform the definition");
    }
}
void RigidTransform(SmContext& context, double precision)
{
    auto surface = Definition(context);
    Require(Work::Set(*surface, precision) == SM_SUCCESS, "rigid transform policy");
    const std::array<SmPoint2d, 3> parameters{ SmPoint2d(0, .25), SmPoint2d(90, .75), SmPoint2d(180, 1) };
    std::array<SmPoint3d, 3> original;
    for (unsigned i = 0; i < parameters.size(); ++i)
    {
        Require(surface->EvaluateSTEPPoint(parameters[i], original[i]) == SM_SUCCESS, "pre-transform point");
    }
    const SmAxis2Placement frame(SmPoint3d(8, -4, 2), SmVector3d(0, 1, 0), SmVector3d(-1, 0, 0));
    Require(surface->Transform(frame) == SM_SUCCESS, "registered SOR rigid transform succeeds");
    double value = 0;
    Require(
        Work::Get(*surface, value) == SM_SUCCESS && value == precision && Valid(*surface),
        "rigid motion keeps the physical scalar and native validity"
    );
    for (unsigned i = 0; i < parameters.size(); ++i)
    {
        SmPoint3d expected, actual;
        frame.TransformPoint(original[i], expected);
        Require(
            surface->EvaluateSTEPPoint(parameters[i], actual) == SM_SUCCESS && actual.DistanceBetween(expected) <= 2e-12,
            "rigid transform keeps the complete STEP parameter law"
        );
    }
}
std::unique_ptr<SmCrvOnSurf> PublicSeam(SmContext& context, double precision)
{
    // A regular public frustum isolates ownership from the near-pole numerical
    // fixture. The native seam builder copies this exact definition, flag 2.
    gw_CPOINT controls[] = { { 0, 3, 0, 1 }, { 0, 2, 4, 1 } };
    double knots[] = { -2, -2, 8, 8 };
    std::unique_ptr<SmBSplineCurve> generator(new (context) SmBSplineCurve(3, 1, 3, 1, controls, knots, &context));
    SmSurfOfRevolution* rawSurface = nullptr;
    const auto created = SmSurfOfRevolution::CreateCanonical(context, generator.get(), SmPoint3d(), SmVector3d(0, 0, 1), rawSurface, FALSE);
    std::unique_ptr<SmSurfOfRevolution> surface(rawSurface);
    if (surface && generator->GetOwner() == surface.get())
    {
        generator.release();
    }
    Require(created == SM_SUCCESS && surface && !generator, "public regular seam surface owns its original generator");
    Require(Work::Set(*surface, precision) == SM_SUCCESS, "seam starts from an explicitly registered surface");
    SmCrvOnSurf* rawSeam = nullptr;
    const auto status = surface->CreateSeamCurve(context, SM_SP_U, rawSeam, TRUE);
    std::unique_ptr<SmCrvOnSurf> seam(rawSeam);
    Require(
        status == SM_SUCCESS && seam && seam->GetBaseSurface() != surface.get() &&
            seam->GetBaseSurface()->FindAttribute(Work::AttributeId) != surface->FindAttribute(Work::AttributeId),
        "native seam creation owns an independent registered surface and COPY attribute"
    );
    return seam; // The independent source is destroyed before the seam is used.
}
void CheckSeam(const SmCurve& curve, double precision)
{
    const auto* seam = dynamic_cast<const SmCrvOnSurf*>(&curve);
    Require(seam && seam->GetType() == SmCrvOnSurf_TYPE && seam->OwnsBaseSurface(), "actual COS owns the derived support");
    const auto* surface = dynamic_cast<const SmSurfOfRevolution*>(seam->GetBaseSurface());
    const auto* uv = seam->GetUVCurve();
    Require(
        surface && surface->GetOwner() == seam && surface->GetContext() == seam->GetContext() && uv && uv->GetOwner() == seam &&
            uv->GetContext() == seam->GetContext() && uv->GetDim() == 2,
        "derived seam support and UV have complete reciprocal ownership"
    );
    double actual = 0;
    Require(Work::Get(*surface, actual) == SM_SUCCESS && actual == precision, "derived support inherits the exact scalar");
    Require(
        double(SmTol::GetZoneTol3d(surface)) == double(SmTol::GetZoneTol3d()) && Work::GetEffective(*surface, actual) == SM_SUCCESS &&
            actual == std::min(double(SmTol::GetZoneTol3d()), precision) && Valid(*surface),
        "derived support separates work resolution from native zones and keeps complete LEVEL2 checks"
    );
    const std::vector<double> expected{ 0, 3, 0, 1, 0, 2, 4, 1 };
    Require(GeneratorNet(*surface) == expected, "derived support retains every original generator coefficient");
    SmAssertArray findings;
    Require(seam->AssertValid(&findings, SM_LEVEL_2, SM_WALK) && findings.GetSize() == 0, "the complete seam passes native LEVEL2");
    for (double fraction : { 0., .25, .5, .75, 1. })
    {
        const double t = curve.GetNaturalInterval().Evaluate(fraction);
        SmPoint3d parameter, expectedPoint, actualPoint;
        Require(
            uv->EvaluatePoint(t, parameter) == SM_SUCCESS &&
                surface->EvaluatePoint(SmPoint2d(parameter.x, parameter.y), expectedPoint) == SM_SUCCESS &&
                curve.EvaluatePoint(t, actualPoint) == SM_SUCCESS && actualPoint.DistanceBetween(expectedPoint) <= 1e-12,
            "seam keeps its exact copied UV/support parameter composition"
        );
    }
}
void SeamCopies(SmContext& context, double precision)
{
    auto seam = PublicSeam(context, precision);
    CheckSeam(*seam, precision);
    auto* surface = const_cast<SmSurface*>(seam->GetBaseSurface());
    for (ULONG flag : { 2UL, 3UL })
    {
        seam->SetOwnerFlag(flag);
        Require(seam->OwnsBaseSurface(), "both real owning discriminators are supported");
        CheckSeam(*seam, precision);
    }
    seam->SetOwnerFlag(2);
    for (ULONG flag : { 0UL, 1UL, 4UL })
    {
        seam->SetOwnerFlag(flag);
        double value = 0;
        const auto policyStatus = Work::Get(*surface, value);
        SmCurve* raw = nullptr;
        const auto copyStatus = seam->Copy(context, raw);
        std::unique_ptr<SmCurve> candidate(raw);
        seam->SetOwnerFlag(2); // Restore the actual child lifetime before assertions.
        Require(
            policyStatus != SM_SUCCESS && value == 0 && copyStatus != SM_SUCCESS && !candidate,
            "borrowed or invalid flags cannot fake owned support"
        );
    }
    auto* uv = const_cast<SmCurve*>(seam->GetUVCurve());
    auto* originalOwner = uv->SetOwner(nullptr);
    double value = 0;
    const auto missingBacklink = Work::Get(*surface, value);
    uv->SetOwner(originalOwner);
    Require(missingBacklink != SM_SUCCESS, "owned surface requires an actually owned UV child");
    SmContext otherContext;
    uv->SetContext(&otherContext);
    const auto wrongContext = Work::Get(*surface, value);
    uv->SetContext(&context);
    Require(wrongContext != SM_SUCCESS, "owned UV and surface must share the COS context");
    SmBSplineCurve* generator = nullptr;
    SmPoint3d origin;
    SmVector3d axis;
    Require(
        static_cast<const SmSurfOfRevolution*>(surface)->GetCanonical(generator, origin, axis) == SM_SUCCESS && generator,
        "derived support retains its owned generator"
    );
    auto* generatorOwner = generator->SetOwner(seam.get());
    const auto wrongGeneratorOwner = Work::Get(*surface, value);
    generator->SetOwner(generatorOwner);
    Require(wrongGeneratorOwner != SM_SUCCESS, "the generator must belong to its actual surface rather than the COS");
    auto unrelated = PublicSeam(context, precision);
    auto* supportOwner = surface->SetOwner(unrelated.get());
    const auto wrongSupportOwner = Work::Get(*surface, value);
    surface->SetOwner(supportOwner);
    Require(wrongSupportOwner != SM_SUCCESS, "an unrelated owning COS cannot supply a fake support backlink");
    CheckSeam(*seam, precision);
    SmContext copyContext;
    SmCurve* rawCopy = nullptr;
    Require(seam->Copy(copyContext, rawCopy) == SM_SUCCESS && rawCopy, "native seam virtual Copy accepts its real owned support");
    std::unique_ptr<SmCurve> copied(rawCopy);
    Require(
        static_cast<const SmCrvOnSurf*>(copied.get())->GetBaseSurface() != seam->GetBaseSurface() &&
            static_cast<const SmCrvOnSurf*>(copied.get())->GetUVCurve() != seam->GetUVCurve(),
        "copied seam owns independent complete definitions"
    );
    seam.reset();
    CheckSeam(*copied, precision);
    {
        auto classified = PublicSeam(context, precision);
        const auto* borrowedUV = classified->GetUVCurve();
        const auto* borrowedSurface = classified->GetBaseSurface();
        SmCurveClassification source(classified.get(), classified->GetNaturalInterval(), borrowedUV, .001, 0, nullptr, TRUE, TRUE, borrowedSurface);
        classified.release(); // The classification owns this exact source seam.
        SmCurveClassification destination;
        destination = source;
        const auto* destinationSeam = dynamic_cast<const SmCrvOnSurf*>(destination.GetCurve());
        Require(
            destinationSeam && destination.GetCurve() != source.GetCurve() && destination.GetAnyUVCurve1() == destinationSeam->GetUVCurve() &&
                destination.GetSurface1() == destinationSeam->GetBaseSurface(),
            "classification assignment owns its seam copy and redirects its borrowed definition views"
        );
        CheckSeam(*destination.GetCurve(), precision);
    }
}
void NestedArchiveRefusal(SmContext& context, double precision, const std::filesystem::path& directory)
{
    auto seam = PublicSeam(context, precision);
    CheckSeam(*seam, precision);
    for (const auto format : { SM_BINARY, SM_ASCII })
    {
        std::ostringstream bytes(std::ios::out | std::ios::binary);
        SmDatabaseIO database(format, static_cast<std::ostream*>(&bytes));
        Require(
            seam->WriteToDB(database, SM_CURRENT_DATABASE_VERSION) == SM_ERR_INVALID_INPUT && bytes.str().empty(),
            "inline COS archives explicitly refuse nested precision before writing any binary or text bytes"
        );
        CheckSeam(*seam, precision);
        for (const auto asSplines : { FALSE, TRUE })
        {
            SmTArray<SmCurve*> curves;
            SmTArray<SmSurface*> surfaces;
            SmTArray<long> trees;
            SmTArray<SmBrep*> bodies;
            curves.Add(seam.get()); // Borrowed for the synchronous failed write only.
            const auto path = directory / (format == SM_BINARY ? "unsupported-nested.smb" : "unsupported-nested.smt");
            Require(
                SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, bodies, format, TRUE, asSplines) == SM_ERR_INVALID_INPUT,
                "part writer propagates unsupported nested-policy failure instead of dropping its precision"
            );
            CheckSeam(*seam, precision);
        }
    }
}
void Archives(const std::filesystem::path& directory, bool ascii, double precision)
{
    SmContext context;
    auto surface = Definition(context);
    Require(Work::Set(*surface, precision) == SM_SUCCESS, "archive input policy");
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    surfaces.Add(surface.get()); // Write arrays borrow; surface remains the sole owner.
    const auto path = directory / (ascii ? "surface-precision.smt" : "surface-precision.smb");
    const auto format = ascii ? SM_ASCII : SM_BINARY;
    Require(
        SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, format, TRUE, FALSE) == SM_SUCCESS,
        "native surface archive write"
    );
    SmContext readContext;
    SmTArray<SmCurve*> readCurves;
    SmTArray<SmSurface*> readSurfaces;
    SmTArray<long> readTrees;
    SmTArray<SmBrep*> readBreps;
    SmObjsDelete<SmCurve*> deleteCurves(&readCurves);
    SmObjsDelete<SmSurface*> deleteSurfaces(&readSurfaces);
    SmObjsDelete<SmBrep*> deleteBreps(&readBreps);
    Require(
        SmNativeArchive::ReadPartFromFile(readContext, path.c_str(), readCurves, readSurfaces, readTrees, readBreps, format) == SM_SUCCESS &&
            readCurves.GetSize() == 0 && readSurfaces.GetSize() == 1 && readBreps.GetSize() == 0,
        "complete standalone surface archive read"
    );
    surface.reset();
    Require(readSurfaces[0]->GetType() == SmSurfOfRevolution_TYPE, "archive keeps the exact analytic class");
    Check(*static_cast<SmSurfOfRevolution*>(readSurfaces[0]), precision, 1, ascii ? 1e-12 : 0);
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public surface precision output directory required");
        const std::filesystem::path output(argv[1]);
        std::filesystem::create_directories(output);
        SmContext context, copyContext;
        const double global = SmTol::GetZoneTol3d();
        const double contextZone = SmTol::GetZoneTol3d(&context);
        const double precision = std::nextafter(1e-11, std::numeric_limits<double>::infinity());
        auto surface = Definition(context);
        const auto before = GeneratorNet(*surface);
        double value = -1;
        Require(Work::Get(*surface, value) == SM_SUCCESS && value == 0, "default policy is absent");
        const double defaultZone = SmTol::GetZoneTol3d(surface.get());
        const bool defaultValid = Valid(*surface);
        Require(Work::GetEffective(*surface, value) == SM_SUCCESS && value == defaultZone, "absent policy returns the legacy numerical resolution");
        ULONG defaultApprox = SM_SS_NONE;
        const auto defaultExact = surface->GetSingularities(nullptr, nullptr, &defaultApprox);
        Require(
            Work::Set(*surface, defaultZone * 2) == SM_SUCCESS && double(SmTol::GetZoneTol3d(surface.get())) == defaultZone &&
                Work::GetEffective(*surface, value) == SM_SUCCESS && value == defaultZone && Valid(*surface) == defaultValid,
            "a wider raw request preserves the original effective zone and complete native validity result"
        );
        ULONG widerApprox = SM_SS_NONE;
        Require(
            surface->GetSingularities(nullptr, nullptr, &widerApprox) == defaultExact && widerApprox == defaultApprox,
            "a wider request preserves recomputed exact and approximate singularity masks"
        );
        Require(Work::Set(*surface, precision) == SM_SUCCESS, "explicit local precision");
        Check(*surface, precision);
        Require(GeneratorNet(*surface) == before, "precision selection changes no defining data");
        SmPlane* rawPlane = nullptr;
        Require(SmPlane::CreateCanonical(context, SmAxis2Placement(), rawPlane) == SM_SUCCESS && rawPlane, "public unsupported plane control");
        std::unique_ptr<SmPlane> plane(rawPlane);
        Require(
            Work::Get(*plane, value) == SM_SUCCESS && value == 0 && Work::GetEffective(*plane, value) == SM_SUCCESS &&
                value == double(SmTol::GetZoneTol3d(plane.get())) && Work::Set(*plane, precision) != SM_SUCCESS,
            "only the exact SOR class accepts an explicit surface policy"
        );
        for (double invalid : { -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() })
        {
            Require(Work::Set(*surface, invalid) != SM_SUCCESS, "invalid setter inputs refuse");
            Check(*surface, precision);
        }
        SmSurface* rawCopy = nullptr;
        Require(surface->Copy(copyContext, rawCopy) == SM_SUCCESS && rawCopy, "independent registered surface copy");
        std::unique_ptr<SmSurface> copied(rawCopy);
        surface.reset();
        auto& copy = *static_cast<SmSurfOfRevolution*>(copied.get());
        Check(copy, precision);
        for (double extreme : { std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min() })
        {
            Require(Work::Set(copy, extreme) == SM_SUCCESS, "finite positive raw precision is retained");
            const SmVector3d scale = extreme == std::numeric_limits<double>::max() ? SmVector3d(2, 2, 2) : SmVector3d(1e-12, 1e-12, 1e-12);
            Require(
                copy.Transform(SmAxis2Placement(), &scale) != SM_SUCCESS && GeneratorNet(copy)[0] == 1,
                "unrepresentable scaled precision refuses before changing geometry"
            );
        }
        Require(Work::Set(copy, precision) == SM_SUCCESS, "restore precision after numeric preflight controls");
        for (const auto& scale : { SmVector3d(2, 3, 2), SmVector3d(1, -1, 1), SmVector3d(0, 0, 0) })
        {
            Require(copy.Transform(SmAxis2Placement(), &scale) != SM_SUCCESS, "nonuniform or nonpositive scale refuses before mutation");
            Check(copy, precision);
        }
        Require(copy.Transform(BadFrame(false)) != SM_SUCCESS && copy.Transform(BadFrame(true)) != SM_SUCCESS, "invalid rigid frame preflight");
        Check(copy, precision);
        const SmVector3d twice(2, 2, 2);
        Require(copy.Transform(SmAxis2Placement(), &twice) == SM_SUCCESS, "uniform scale changes physical precision once");
        Check(copy, precision * 2, 2);
        Require(Work::Set(copy, 0) == SM_SUCCESS && Work::Get(copy, value) == SM_SUCCESS && value == 0, "zero restores default precision");
        Require(
            Work::GetEffective(copy, value) == SM_SUCCESS && value == double(SmTol::GetZoneTol3d(&copy)),
            "clearing policy restores the legacy work resolution without changing SmTol"
        );
        InvalidMetadata(context, precision);
        WrongSurfaceClass(context, precision);
        FaceOwnership(context, precision);
        ConversionRefusal(context, precision);
        Notifications(context, precision);
        RigidTransform(context, precision);
        SeamCopies(context, precision);
        NestedArchiveRefusal(context, precision, output);
        Archives(output, false, precision);
        Archives(output, true, precision);
        Require(
            double(SmTol::GetZoneTol3d()) == global && double(SmTol::GetZoneTol3d(&context)) == contextZone,
            "global and context tolerances never change"
        );
        std::cout << "surface work precision kernel contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
