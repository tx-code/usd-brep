// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmBrepData.h>
#include <SmContext.h>
#include <SmCurve.h>
#include <SmSurface.h>
#include <SmVertex.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace step_import;
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected, double tolerance = 1e-9)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, "independent linked geometry value");
}
void Near(Point actual, Point expected)
{
    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        Near(actual[axis], expected[axis]);
    }
}
Options General()
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    return options;
}
void Replace(std::string& text, std::string_view old, std::string_view replacement)
{
    const auto at = text.find(old);
    Require(at != std::string::npos, "public fixture edit target");
    text.replace(at, old.size(), replacement);
}
void Add(std::string& text, std::string_view records)
{
    const auto at = text.find("ENDSEC;", text.find("DATA;"));
    Require(at != std::string::npos, "public fixture data end");
    text.insert(at, records);
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Require(bool(file), "public fixture file");
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
void NativePartTolerance(const std::filesystem::path& archive)
{
    // Native-only control: no STEP reader or imported result supplies truth.
    // The origin used to collapse a saved positive BRep tolerance to zero;
    // a distant point exercised the opposite, size-derived tolerance growth.
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmBrep*> deleteBreps(&breps);
    for (const auto point : { Point{ 0, 0, 0 }, Point{ -7, -8, -9 }, Point{ 1e6, 0, 0 } })
    {
        std::unique_ptr<SmBrep> owner(new (context) SmBrep());
        owner->SetTolerance(1e-5, FALSE);
        SmShell* shell = nullptr;
        SmVertex* vertex = nullptr;
        Require(
            owner->MakeShellVertex(nullptr, SmPoint3d(point[0], point[1], point[2]), shell, vertex) == SM_SUCCESS,
            "native point control construction"
        );
        vertex->SetTolerance(1e-5, FALSE);
        breps.Add(owner.get());
        owner.release();
    }
    Require(
        SmBrepData::WritePartToFile(archive.c_str(), curves, surfaces, trees, breps, SM_BINARY, TRUE, FALSE) == SM_SUCCESS,
        "native point control write"
    );
    SmContext readContext;
    SmTArray<SmCurve*> readCurves;
    SmTArray<SmSurface*> readSurfaces;
    SmTArray<long> readTrees;
    SmTArray<SmBrep*> readBreps;
    SmObjsDelete<SmCurve*> deleteReadCurves(&readCurves);
    SmObjsDelete<SmSurface*> deleteReadSurfaces(&readSurfaces);
    SmObjsDelete<SmBrep*> deleteReadBreps(&readBreps);
    Require(
        SmBrepData::ReadPartFromFile(readContext, archive.c_str(), readCurves, readSurfaces, readTrees, readBreps, SM_BINARY) == SM_SUCCESS,
        "native point control read"
    );
    Require(readBreps.GetSize() == 3, "native point control inventory");
    for (ULONG i = 0; i < readBreps.GetSize(); ++i)
    {
        Require(readBreps[i]->GetTolerance() == breps[i]->GetTolerance(), "native part preserves stored work tolerance");
        SmAssertArray pointers, deep;
        Require(readBreps[i]->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0, "native point read pointers");
        Require(readBreps[i]->AssertValid(&deep, SM_LEVEL_2, SM_WALK) && deep.GetSize() == 0, "native point read level2");
    }
}
void Basic(const std::string& source, const std::filesystem::path& archive)
{
    auto result = ImportBytes(source, General());
    Require(result.status == Status::Accepted && bool(result.model), "complete linked native candidate");
    Require(
        result.model->Info().faces == 6 && result.model->Info().edges == 12 && result.model->Info().vertices == 8,
        "wireframe does not create body topology"
    );
    Near(*result.model->MaterialVolumeMm3(), 6000);
    Near(result.model->Info().boundsMm[0], { 2, -3, 5 });
    Near(result.model->Info().boundsMm[1], { 12, 17, 35 });
    const auto views = result.model->Wireframes();
    Require(views.size() == 1, "owning source set view");
    const auto info = views[0].Info();
    Require(info.representationId == "920" && info.relationshipId == "931" && info.setId == "910", "separate source roles");
    Require(
        info.curves.size() == 2 && info.points.size() == 1 && info.curves[0].sourceId == "912" && info.curves[1].sourceId == "940" &&
            info.points[0].sourceId == "911",
        "all set member identities"
    );
    const auto line = info.curves[0].parameterDomain, arc = info.curves[1].parameterDomain;
    Near(views[0].EvaluateCurve(0, line[0]), { 0, 0, 0 });
    Near(views[0].EvaluateCurve(0, line[1]), { 5, 0, 0 });
    Near(views[0].EvaluateCurve(0, (line[0] + line[1]) / 2), { 2.5, 0, 0 });
    Near(views[0].EvaluateCurve(1, arc[0]), { 2, 0, 0 });
    Near(views[0].EvaluateCurve(1, arc[1]), { 0, 2, 0 });
    Near(views[0].EvaluateCurve(1, (arc[0] + arc[1]) / 2), { std::sqrt(2.), std::sqrt(2.), 0 });
    Near(views[0].Closest({ 7, 8, 9 }).distanceMm, 0);
    Near(views[0].Closest({ -1, 0, 0 }).distanceMm, 1);
    Near(views[0].Closest({ std::sqrt(2.), std::sqrt(2.), 0 }).distanceMm, 0);
    // The point lies INSIDE the independent [2,12]x[-3,17]x[5,35] box.
    // Putting it in that BRep would change these material-boundary queries.
    Near(result.model->Closest({ 7, 8, 9 }).distanceMm, 4);
    Require(result.model->ClassifyMaterial({ 7, 8, 9 }) == MaterialPointClass::Inside, "wire point is not material boundary");
    Require(result.model->CopyForModeling().status == Status::Unsupported, "generic modeling copy cannot silently omit linked geometry");
    auto copy = result.model->CopyBrepForModeling();
    Require(bool(copy.model), "independent body modeling copy");
    copy.model->Edit(
        [](SmBrep& body)
        {
            SmTArray<SmVertex*> vertices;
            body.GetVertices(vertices);
            Require(vertices.GetSize() == 8, "modeling copy has selected body scope");
            body.SetTolerance(body.GetTolerance() * 1.1, FALSE);
        }
    );
    result.model->WriteNative(archive);
    Near(result.model->Closest({ 7, 8, 9 }).distanceMm, 4);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> breps;
    SmObjsDelete<SmCurve*> deleteCurves(&curves);
    SmObjsDelete<SmSurface*> deleteSurfaces(&surfaces);
    SmObjsDelete<SmBrep*> deleteBreps(&breps);
    Require(
        SmBrepData::ReadPartFromFile(context, archive.c_str(), curves, surfaces, trees, breps, SM_BINARY) == SM_SUCCESS,
        "independent full part reload"
    );
    Require(curves.GetSize() == 2 && breps.GetSize() == 2 && surfaces.GetSize() == 0 && trees.GetSize() == 0, "complete mixed archive inventory");
    SmTArray<SmVertex*> pointVertices;
    breps[1]->GetVertices(pointVertices);
    Require(pointVertices.GetSize() == 1, "isolated source point archive adapter");
    const auto point = pointVertices[0]->GetPoint();
    Near({ point.x, point.y, point.z }, { 7, 8, 9 });
    bool invalid = false;
    try
    {
        views[0].EvaluateCurve(0, std::numeric_limits<double>::infinity());
    }
    catch (const std::out_of_range&)
    {
        invalid = true;
    }
    Require(invalid, "curve domain rejects nonfinite parameter");
    result = {};
    Near(views[0].EvaluateCurve(0, line[1]), { 5, 0, 0 });
    Require(
        views[0].AppliedSourceInterpretation() == step_semantics::SchemaInterpretation::DeclaredOnly,
        "owning wireframe retains original source qualification"
    );
}
void IdentityAndSense(std::string source, const std::filesystem::path& directory)
{
    Replace(source, "(#911,#912,#940)", "(#911,#912,#940,#918)");
    Replace(source, "(#910,#904)", "(#910,#904,#919)");
    Add(source, "#918=CARTESIAN_POINT('',(7.,8.,9.));\n#919=GEOMETRIC_CURVE_SET('',(#912,#911));\n");
    auto result = ImportBytes(source, General());
    Require(result.status == Status::Accepted && result.model, "shared definitions and distinct equal points");
    const auto groups = result.model->Wireframes();
    Require(groups.size() == 2 && groups[0].Info().points.size() == 2, "source sets not merged");
    Require(groups[0].Info().points[0].nativeIndex != groups[0].Info().points[1].nativeIndex, "equal source points not welded");
    Require(groups[0].Info().curves[0].nativeIndex == groups[1].Info().curves[0].nativeIndex, "shared source definition has one native owner");
    result.model->WriteNative(directory / "shared-definitions.smb");
    Replace(source, "(#914),(#915),.T.,.CARTESIAN.", "(#915),(#914),.F.,.CARTESIAN.");
    result = ImportBytes(source, General());
    Require(
        result.status == Status::Accepted && result.model && !result.model->Wireframes()[0].Info().curves[0].sourceForwardInNative,
        "reversed source trim sense retained"
    );
}
void CurveFamilies(const std::string& source, const std::filesystem::path& directory)
{
    const std::array definitions{ std::string("#950=CIRCLE('',#904,2.);\n"),
                                  std::string("#950=ELLIPSE('',#904,3.,2.);\n"),
                                  std::string(
                                      "#950=B_SPLINE_CURVE_WITH_KNOTS('',2,(#914,#951,#952),.UNSPECIFIED.,.F.,.F.,(3,3),(0.,1.),.UNSPECIFIED.);\n"
                                      "#951=CARTESIAN_POINT('',(1.,1.,0.));\n#952=CARTESIAN_POINT('',(2.,0.,0.));\n"
                                  ),
                                  std::string(
                                      "#950=(BOUNDED_CURVE() B_SPLINE_CURVE(2,(#914,#951,#952),.UNSPECIFIED.,.F.,.F.) "
                                      "B_SPLINE_CURVE_WITH_KNOTS((3,3),(0.,1.),.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() "
                                      "RATIONAL_B_SPLINE_CURVE((1.,2.,1.)) REPRESENTATION_ITEM(''));\n"
                                      "#951=CARTESIAN_POINT('',(1.,1.,0.));\n#952=CARTESIAN_POINT('',(2.,0.,0.));\n"
                                  ) };
    for (std::size_t i = 0; i < definitions.size(); ++i)
    {
        auto bytes = source;
        Replace(bytes, "(#910,#904)", "(#910,#904,#960)");
        Add(bytes, definitions[i] + "#960=GEOMETRIC_CURVE_SET('',(#950));\n");
        auto result = ImportBytes(bytes, General());
        Require(result.status == Status::Accepted && result.model, "admitted wireframe curve family");
        auto view = result.model->Wireframes().at(1);
        const auto domain = view.Info().curves.at(0).parameterDomain;
        if (i < 2)
        {
            const double radiusX = i ? 3 : 2;
            Near(view.EvaluateCurve(0, domain[0]), { radiusX, 0, 0 });
            Near(view.EvaluateCurve(0, (domain[0] + domain[1]) / 2), { -radiusX, 0, 0 });
            Near(view.Closest({ radiusX + 1, 0, 0 }).distanceMm, 1);
        }
        else
        {
            Near(view.EvaluateCurve(0, domain[0]), { 0, 0, 0 });
            Near(view.EvaluateCurve(0, domain[1]), { 2, 0, 0 });
            Near(view.EvaluateCurve(0, (domain[0] + domain[1]) / 2), { 1, i == 2 ? .5 : 2. / 3., 0 });
        }
        result.model->WriteNative(directory / ("curve-family-" + std::to_string(i) + ".smb"));
    }
    auto interpreted = source;
    Replace(interpreted, "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1}", "AUTOMOTIVE_DESIGN {1 0 10303 214 3 1 1 1}");
    auto options = General();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    options.schemaInterpretation = step_semantics::SchemaInterpretation::UnregisteredAp214As2010;
    auto result = ImportBytes(interpreted, options);
    Require(result.status == Status::AcceptedUnderInterpretation && result.model, "linked interpretation remains distinct");
    const auto view = result.model->Wireframes().at(0);
    result = {};
    Require(view.AppliedSourceInterpretation() == options.schemaInterpretation, "wireframe retains unverified source assumption");
}
void Execution(const std::string& source, const std::filesystem::path& bodyFixture)
{
    const auto body = ImportFile(bodyFixture, General());
    const auto complete = ImportBytes(source, General());
    Require(body.model && complete.model && complete.report.nativeCalls > body.report.nativeCalls, "extra linked construction boundaries");
    for (auto limit = body.report.nativeCalls; limit < complete.report.nativeCalls; ++limit)
    {
        auto options = General();
        options.nativeLimits.nativeCalls = limit;
        const auto failed = ImportBytes(source, options);
        Require(
            failed.status == Status::ResourceLimit && !failed.model && failed.report.candidateDiscarded,
            "every linked native boundary discards the whole candidate"
        );
        options = General();
        options.execution.control = std::make_shared<ImportControl>();
        options.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeNativeCall && p.nativeCalls >= limit)
            {
                options.execution.control->RequestCancel();
            }
        };
        const auto cancelled = ImportBytes(source, options);
        Require(
            cancelled.status == Status::Cancelled && !cancelled.model && cancelled.report.candidateDiscarded,
            "linked curve owners are released on cancellation"
        );
    }
    for (const auto phase : { Phase::NativeReady, Phase::BeforeReport, Phase::ReportReady, Phase::BeforeCommit })
    {
        auto options = General();
        options.execution.control = std::make_shared<ImportControl>();
        options.execution.progress = [&](Progress p)
        {
            if (p.phase == phase)
            {
                options.execution.control->RequestCancel();
            }
        };
        const auto result = ImportBytes(source, options);
        Require(result.status == Status::Cancelled && !result.model && result.report.candidateDiscarded, "linked final publication is atomic");
    }
    auto options = General();
    options.nativeLimits.reportBytes = 64;
    const auto limited = ImportBytes(source, options);
    Require(limited.status == Status::ResourceLimit && !limited.model, "required linked report cannot be omitted");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "linked fixture, body fixture and local output directory required");
        const auto source = Read(argv[1]);
        const std::filesystem::path directory(argv[3]);
        std::filesystem::create_directories(directory);
        NativePartTolerance(directory / "native-point-tolerances.smb");
        Basic(source, directory / "complete-part.smb");
        IdentityAndSense(source, directory);
        CurveFamilies(source, directory);
        Execution(source, argv[2]);
        std::cout << "linked wireframe ownership/query/archive/publication controls passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
