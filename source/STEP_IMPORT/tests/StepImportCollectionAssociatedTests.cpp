// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrep.h>
#include <SmContext.h>
#include <SmNativeArchive.h>
#include <fstream>
#include <iostream>
namespace
{
using namespace step_import;
using step_import_test::Require;
Options Settings()
{
    auto o = Options::PlanarFixture();
    o.generalBrep = true;
    o.quality = Quality::GeneralDevelopment();
    return o;
}
void Valid(SmBrep& b)
{
    SmAssertArray p, d;
    Require(b.ValidatePointers(&p) == SM_SUCCESS && !p.GetSize(), "associated archive pointers");
    Require(b.AssertValid(&d, SM_LEVEL_2, SM_WALK) && !d.GetSize(), "associated archive LEVEL2");
}
void Archive(const std::filesystem::path& path, std::size_t bodies, std::size_t curves, std::size_t surfaces, std::size_t points)
{
    SmContext context;
    SmTArray<SmCurve*> c;
    SmTArray<SmSurface*> s;
    SmTArray<long> t;
    SmTArray<SmBrep*> b;
    SmObjsDelete<SmCurve*> dc(&c);
    SmObjsDelete<SmSurface*> ds(&s);
    SmObjsDelete<SmBrep*> db(&b);
    Require(SmNativeArchive::ReadPartFromFile(context, path.c_str(), c, s, t, b, SM_BINARY) == SM_SUCCESS, "whole associated archive read");
    Require(
        c.GetSize() == curves && s.GetSize() == surfaces && b.GetSize() == bodies + points && !t.GetSize(),
        "whole part inventory includes every body, independent curve, frame/plane and point adapter"
    );
    for (auto* body : b)
    {
        Valid(*body);
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public associated collection fixture and output required");
        std::ifstream in(argv[1], std::ios::binary);
        std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
        Require(!text.empty(), "public fixture opens");
        std::filesystem::create_directories(argv[2]);
        const auto output = std::filesystem::path(argv[2]);
        auto result = ImportMaterialCollectionBytes(text, Settings());
        if (!result.collection)
        {
            for (const auto& d : result.report.diagnostics)
            {
                std::cerr << d.source.key << '\n';
            }
        }
        Require(result.status == Status::Accepted && result.collection, "whole associated collection accepted");
        auto collection = result.collection;
        Require(collection->BodyCount() == 2, "two independent material bodies");
        auto wires = collection->Wireframes();
        auto support = collection->SupportingGeometry();
        Require(wires.size() == 1 && support.size() == 1, "associated roles owned once by the part");
        const auto w = wires[0].Info();
        const auto s = support[0].Info();
        Require(
            w.curves.size() == 2 && w.points.size() == 1 && s.curves.size() == 2 && s.points.size() == 1 && s.frames.size() == 2 &&
                s.planes.size() == 1,
            "source role inventory complete"
        );
        Require(
            w.curves[0].sourceId == s.curves[0].sourceId && w.curves[0].nativeIndex == s.curves[0].nativeIndex &&
                w.points[0].nativeIndex == s.points[0].nativeIndex,
            "one source definition shared by roles within the part"
        );
        Require(
            wires[0].EvaluateCurve(0, (w.curves[0].parameterDomain[0] + w.curves[0].parameterDomain[1]) / 2) == Point{ 2.5, 0, 0 },
            "part-level curve queries retain source parameters"
        );
        for (std::size_t i = 0; i < collection->BodyCount(); ++i)
        {
            const auto body = collection->Body(i);
            Require(
                body->Wireframes().empty() && body->SupportingGeometry().empty(),
                "body snapshots cannot duplicate or impersonate part-level associated geometry"
            );
            auto copy = body->CopyBrepForModeling();
            Require(copy.status == Status::Accepted && copy.model, "explicit body-only modeling copy");
            copy.model->Edit(Valid);
        }
        const auto calls = result.report.nativeCalls;
        const auto bytes = result.report.json.size();
        Require(
            !result.report.wireframes.empty() && !result.report.supportingGeometry.empty(),
            "required collection report retains associated ownership evidence"
        );
        collection->WriteNative(output / "part.smb");
        Archive(output / "part.smb", 2, 2, 3, 1);
        auto settings = Settings();
        settings.nativeLimits.nativeCalls = calls;
        Require(ImportMaterialCollectionBytes(text, settings).collection != nullptr, "exact native budget includes associated construction");
        --settings.nativeLimits.nativeCalls;
        const auto shortCalls = ImportMaterialCollectionBytes(text, settings);
        Require(shortCalls.status == Status::ResourceLimit && !shortCalls.collection, "native N-1 publishes neither bodies nor associated owner");
        settings = Settings();
        settings.nativeLimits.reportBytes = bytes;
        Require(ImportMaterialCollectionBytes(text, settings).collection != nullptr, "exact complete report budget");
        --settings.nativeLimits.reportBytes;
        const auto shortReport = ImportMaterialCollectionBytes(text, settings);
        Require(shortReport.status == Status::ResourceLimit && !shortReport.collection, "report N-1 cannot publish body-only result");
        for (auto phase : { Phase::BeforeNativeCall, Phase::NativeReady, Phase::BeforeCommit })
        {
            settings = Settings();
            settings.execution.control = std::make_shared<ImportControl>();
            settings.execution.progress = [&](Progress p)
            {
                if (p.phase == phase)
                {
                    settings.execution.control->RequestCancel();
                }
            };
            const auto stopped = ImportMaterialCollectionBytes(text, settings);
            Require(stopped.status == Status::Cancelled && !stopped.collection, "cancelled complete collection releases all candidates");
        }
        result = {};
        auto badSecond = text;
        const auto wrongVertex = badSecond.find("#10100=CARTESIAN_POINT('',(102.0,-3.,5.))");
        Require(wrongVertex != std::string::npos, "public second-body defect token");
        badSecond
            .replace(wrongVertex, std::string_view("#10100=CARTESIAN_POINT('',(102.0,-3.,5.))").size(), "#10100=CARTESIAN_POINT('',(103.0,-3.,5.))");
        const auto incomplete = ImportMaterialCollectionBytes(badSecond, Settings());
        Require(
            !incomplete.collection && incomplete.status != Status::Accepted,
            "failed second body cannot release the first body or the already built part-associated candidate"
        );
        collection.reset();
        Require(
            wires[0].EvaluateCurve(0, (w.curves[0].parameterDomain[0] + w.curves[0].parameterDomain[1]) / 2) == Point{ 2.5, 0, 0 } &&
                support[0].Info().frames.size() == 2,
            "associated views independently own geometry after collection/report release"
        );
        auto documentText = text;
        documentText.insert(
            documentText.rfind("ENDSEC;"),
            "#20800=PRODUCT('second','independent definition',$,(#3));\n#20801=PRODUCT_DEFINITION_FORMATION('B',$,#20800);\n#20802=PRODUCT_DEFINITION('D2',$,#20801,#6);\n#20803=PRODUCT_DEFINITION_SHAPE('',$,#20802);\n#20804=SHAPE_DEFINITION_REPRESENTATION(#20803,#20820);\n#20820=ADVANCED_BREP_SHAPE_REPRESENTATION('child',(#21),#12);\n"
        );
        auto document = ImportDocumentBytes(documentText, Settings());
        if (!document.document)
        {
            for (const auto& d : document.report.diagnostics)
            {
                std::cerr << d.source.key << '\n';
            }
        }
        Require(
            document.status == Status::Accepted && document.document && document.document->PartCount() == 2 && document.document->BodyCount() == 3,
            "complete document retains associated first part and independent second definition"
        );
        const auto first = std::get<std::shared_ptr<const MaterialCollectionSnapshot>>(document.document->Part(0));
        Require(first->SupportingGeometry().size() == 1 && first->Wireframes().size() == 1, "document part owns its associated source roles");
        Require(document.document->Body(2)->SupportingGeometry().empty(), "foreign part does not inherit another part's associated geometry");
        document.document->WriteNative(output / "document.smb");
        Archive(output / "document.smb", 3, 2, 3, 1);
        Require(first->Source()->Source().Bytes() == documentText, "original source bytes survive grouped import");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
