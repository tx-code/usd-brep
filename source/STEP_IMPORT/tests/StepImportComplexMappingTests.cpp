// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportDefinitionChecks.h"

#include <SmAssertArray.h>
#include <SmBrepData.h>
#include <SmNativeArchive.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
using namespace step_import;
using step_import_test::Require;
namespace
{
Options Config(bool interpret = true)
{
    auto options = Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = Quality::GeneralDevelopment();
    options.sourcePolicy = step_semantics::SourcePolicy::PreserveNonGeometric;
    if (interpret)
    {
        options.complexMappingInterpretation = step_semantics::ComplexMappingInterpretation::RegisteredFacetNames;
    }
    return options;
}
std::string Reorder(std::string source, std::string_view facet)
{
    const auto raw = step_source::InspectBytes(source, step_source::Limits::PlanarFixtures());
    Require(bool(raw.source), "public source parses before fixture mutation");
    for (const auto& record : raw.source->Records())
    {
        if (!record.complex || std::none_of(
                                   record.components.begin(),
                                   record.components.end(),
                                   [&](const auto& c)
                                   {
                                       return c.name == facet;
                                   }
                               ))
        {
            continue;
        }
        std::string replacement = "#" + record.id + "=(";
        for (auto i = record.components.rbegin(); i != record.components.rend(); ++i)
        {
            replacement += raw.source->Raw(i->span);
            replacement += " ";
        }
        replacement += ");";
        source.replace(record.span.begin, record.span.end - record.span.begin, replacement);
        return source;
    }
    throw std::runtime_error("public complex fixture facet missing");
}
std::string RationalSurface(std::string source)
{
    const auto raw = step_source::InspectBytes(source, step_source::Limits::PlanarFixtures());
    const auto typed = step_semantics::Check(raw, {}, step_semantics::SourcePolicy::Strict, step_semantics::GeometryScope::GeneralBrep);
    Require(bool(typed.snapshot), "public canonical field bindings");
    for (std::size_t r = 0; r < raw.source->Records().size(); ++r)
    {
        const auto& record = raw.source->Records()[r];
        if (record.complex || record.components.front().name != "B_SPLINE_SURFACE_WITH_KNOTS")
        {
            continue;
        }
        auto names = typed.snapshot->TypesOf(r);
        names.push_back("RATIONAL_B_SPLINE_SURFACE");
        std::sort(names.begin(), names.end());
        std::map<std::string, std::string> slots;
        std::string weights = "(";
        for (std::size_t p = 0; p < record.components.front().parameters.size(); ++p)
        {
            const auto attribute = typed.snapshot->Attribute(r, 0, p);
            auto& fields = slots[std::string(attribute.declaredIn)];
            if (!fields.empty())
            {
                fields += ',';
            }
            fields += raw.source->Raw(attribute.value->span);
            if (attribute.name == "control_points_list")
            {
                for (const auto& row : attribute.value->children)
                {
                    if (weights.size() > 1)
                    {
                        weights += ',';
                    }
                    weights += '(';
                    for (std::size_t c = 0; c < row.children.size(); ++c)
                    {
                        weights += c ? ",1.0" : "1.0";
                    }
                    weights += ')';
                }
            }
        }
        weights += ')';
        slots["RATIONAL_B_SPLINE_SURFACE"] = weights;
        std::string replacement = "#" + record.id + "=(";
        for (const auto& name : names)
        {
            replacement += name + "(" + slots[name] + ") ";
        }
        replacement += ");";
        source.replace(record.span.begin, record.span.end - record.span.begin, replacement);
        return source;
    }
    throw std::runtime_error("public polynomial roof missing");
}
void Replace(std::string& source, const std::string& old, const std::string& value)
{
    const auto at = source.find(old);
    Require(at != std::string::npos, "public mutation token present");
    source.replace(at, old.size(), value);
}
void Same(const ModelSnapshot& a, const ModelSnapshot& b)
{
    auto first = a.CopyBrepForModeling(), second = b.CopyBrepForModeling();
    Require(first.status == Status::Accepted && second.status == Status::Accepted, "independent native copies");
    first.model->Edit(
        [&](SmBrep& x)
        {
            second.model->Edit(
                [&](SmBrep& y)
                {
                    step_import_test::CompareNativeDefinitions(x, y);
                }
            );
        }
    );
}
void Archive(const ModelSnapshot& model, const std::filesystem::path& output)
{
    model.WriteNative(output);
    SmContext context;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    SmTArray<SmBrep*> bodies;
    SmObjsDelete<SmCurve*> dc(&curves);
    SmObjsDelete<SmSurface*> ds(&surfaces);
    SmObjsDelete<SmBrep*> db(&bodies);
    Require(
        SmNativeArchive::ReadPartFromFile(context, output.c_str(), curves, surfaces, trees, bodies, SM_BINARY) == SM_SUCCESS && bodies.GetSize() == 1,
        "interpreted source native archive reload"
    );
    SmAssertArray pointers, deep;
    Require(
        bodies[0]->ValidatePointers(&pointers) == SM_SUCCESS && pointers.GetSize() == 0 && bodies[0]->AssertValid(&deep, SM_LEVEL_2, SM_WALK) &&
            deep.GetSize() == 0,
        "archive remains fully valid"
    );
    auto copy = model.CopyBrepForModeling();
    copy.model->Edit(
        [&](SmBrep& original)
        {
            step_import_test::CompareNativeDefinitions(original, *bodies[0]);
        }
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "public fixture and native output directory required");
        std::ifstream input(argv[1], std::ios::binary);
        const std::string canonical{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
        const auto truth = ImportBytes(canonical, Config(false));
        Require(truth.status == Status::Accepted && truth.model, "canonical public control");
        std::filesystem::create_directories(argv[2]);
        const auto rational = RationalSurface(canonical);
        const auto rationalTruth = ImportBytes(rational, Config(false));
        Require(rationalTruth.status == Status::Accepted && rationalTruth.model, "registered rational roof control with unit weights");
        const auto units = Reorder(canonical, "LENGTH_UNIT"), surface = Reorder(rational, "B_SPLINE_SURFACE_WITH_KNOTS");
        for (const auto& [source, control] : { std::pair{ units, truth.model }, std::pair{ surface, rationalTruth.model } })
        {
            const auto strict = ImportBytes(source, Config(false));
            Require(strict.status == Status::Rejected && !strict.model && strict.report.nativeCalls == 0, "strict order rule unchanged");
            auto interpreted = ImportBytes(source, Config());
            Require(
                interpreted.status == Status::AcceptedUnderInterpretation && interpreted.model,
                "complete registered name binding qualifies separately"
            );
            auto semanticReport = *interpreted.report.generalSource;
            semanticReport.complexMappingDiagnostics.clear();
            const auto raw = step_source::InspectBytes(source, step_source::Limits::PlanarFixtures());
            std::string json;
            Require(
                step_semantics::InspectJson(raw, semanticReport, json) &&
                    json.find("\"key\":\"ComplexConstituentOrderInvalid\"") != std::string::npos,
                "reserializing a qualified source uses its owning facts after mutable report diagnostics are cleared"
            );
            auto model = interpreted.model;
            const auto bytes = interpreted.report.json.size();
            interpreted = {};
            const auto semantic = model->Source();
            Require(
                semantic->Source().Bytes() == source && semantic->ComplexMappingAssumptions().size() == 1 &&
                    semantic->ComplexMappingDiagnostics().at(0).source.key == "ComplexConstituentOrderInvalid",
                "owning original bytes and strict fact"
            );
            const auto& mapping = semantic->ComplexMappingAssumptions().at(0);
            const auto& record = semantic->Source().Records().at(mapping.record);
            std::vector<std::string> names;
            for (const auto index : mapping.canonicalComponentOrder)
            {
                names.push_back(record.components.at(index).name);
            }
            Require(
                std::is_sorted(names.begin(), names.end()) && names.size() == record.components.size() &&
                    record.components.front().name != names.front(),
                "canonical lookup permutation does not reorder source"
            );
            Require(
                model->ClassifyMaterial({ 7, 7, 20 }) == MaterialPointClass::Inside &&
                    model->ClassifyMaterial({ 100, 100, 100 }) == MaterialPointClass::Outside,
                "interpreted model native exact queries"
            );
            step_import_test::Near(*model->MaterialVolumeMm3(), 6000 + 4000. / 9, .01, "independent Bernstein integral truth");
            Same(*control, *model);
            Archive(*model, std::filesystem::path(argv[2]) / (source == units ? "units.smb" : "surface.smb"));
            auto cap = Config();
            cap.nativeLimits.reportBytes = bytes - 1;
            const auto failedReport = ImportBytes(source, cap);
            Require(failedReport.status == Status::ResourceLimit && !failedReport.model, "mandatory assumption evidence cannot be truncated");
        }
        const auto noop = ImportBytes(canonical, Config());
        Require(noop.status == Status::Accepted && noop.model->Source()->ComplexMappingAssumptions().empty(), "unused request creates no assumption");
        for (int defect = 0; defect < 4; ++defect)
        {
            auto source = units;
            if (defect == 0)
            {
                Replace(source, "NAMED_UNIT(*)", "NAMED_UNIT(*) NAMED_UNIT(*)");
            }
            if (defect == 1)
            {
                Replace(source, "NAMED_UNIT(*)", "");
            }
            if (defect == 2)
            {
                Replace(source, "SI_UNIT(.MILLI.,.METRE.)", "SI_UNIT(.MILLI.,.METRE.,42)");
            }
            if (defect == 3)
            {
                Replace(source, "NAMED_UNIT(*)", "NAMED_UNIT($)");
            }
            const auto result = ImportBytes(source, Config());
            Require(
                !result.model && result.report.nativeCalls == 0 && !result.report.generalSource->snapshot,
                "duplicate/missing facets, field counts and physical markers still reject"
            );
            if (defect > 0)
            {
                Require(
                    !result.report.generalSource->complexMappingDiagnostics.empty(),
                    "observed order error survives later source failure without a qualified receipt"
                );
            }
        }
        auto repairSource = units;
        Replace(repairSource, "#517=ADVANCED_FACE('',(#516),#514,.T.);", "#517=ADVANCED_FACE('',(#516),#514,.F.);");
        auto repair = Config();
        const auto repairRaw = step_source::InspectBytes(repairSource, step_source::Limits::PlanarFixtures());
        repair.faceOrientationRepair = FaceOrientationRepairRequest{ repairRaw.sha256, 1, 10000, 1000000, { { "7", "21", "517", false, true } } };
        const auto repaired = ImportBytes(repairSource, repair);
        Require(
            repaired.status == Status::AcceptedWithRepairs && repaired.model && repaired.model->Repairs()->sourceInterpretationApplied &&
                repaired.model->Source()->ComplexMappingAssumptions().size() == 1 && repaired.model->Source()->Source().Bytes() == repairSource,
            "repair retains the separate complex-facet source interpretation and original bytes"
        );
        auto capacity = Config();
        capacity.semanticLimits.preservedDiagnostics = 1;
        const auto two = ImportBytes(Reorder(Reorder(rational, "LENGTH_UNIT"), "B_SPLINE_SURFACE_WITH_KNOTS"), capacity);
        Require(
            two.status == Status::ResourceLimit && !two.model && two.report.nativeCalls == 0,
            "diagnostic budget is cumulative across interpreted records"
        );
        std::ifstream multiInput(std::filesystem::path(argv[1]).parent_path() / "independent_two_parts.step", std::ios::binary);
        const std::string multi{ std::istreambuf_iterator<char>(multiInput), std::istreambuf_iterator<char>() };
        const auto multiSource = Reorder(multi, "LENGTH_UNIT");
        const auto rawMulti = step_source::InspectBytes(multiSource, step_source::Limits::PlanarFixtures());
        auto graphOnly = step_semantics::Check(
            rawMulti,
            {},
            step_semantics::SourcePolicy::PreserveNonGeometric,
            step_semantics::GeometryScope::GeneralBrep,
            step_semantics::SchemaInterpretation::DeclaredOnly,
            step_semantics::UnitInterpretation::DeclaredOnly,
            step_semantics::ComplexMappingInterpretation::RegisteredFacetNames
        );
        auto graph = graphOnly.productStructure;
        graphOnly = {};
        Require(
            graph && graph->Source().Bytes() == multiSource &&
                graph->Basis().complexMappingInterpretation == step_semantics::ComplexMappingInterpretation::RegisteredFacetNames &&
                graph->Basis().complexMappingDiagnostics.at(0).source.key == "ComplexConstituentOrderInvalid",
            "declared graph retains its own type-binding interpretation after outcome disposal, without a qualified part"
        );
        auto cancelled = Config();
        cancelled.execution.control = std::make_shared<ImportControl>();
        cancelled.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::SemanticsChecked)
            {
                cancelled.execution.control->RequestCancel();
            }
        };
        const auto stop = ImportBytes(units, cancelled);
        Require(stop.status == Status::Cancelled && !stop.model && stop.report.nativeCalls == 0, "source assumption cannot bypass execution decision");
        std::cout << "registered complex facet binding contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
