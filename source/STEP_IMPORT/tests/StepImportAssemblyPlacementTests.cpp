// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportAssemblyInstances.h"
#include "StepImportAssemblyPlacement.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace step_import;
using namespace step_import::detail;
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Replace(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    Require(at != std::string::npos, "synthetic mutation target");
    text.replace(at, from.size(), to);
    return text;
}
std::shared_ptr<const step_semantics::AssemblyInputs> Inputs(const std::string& text)
{
    auto source = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    auto result = step_semantics::InspectAssemblyInputs(source, {}, step_semantics::SourcePolicy::Strict);
    Require(
        result.checks.status == step_semantics::Status::Deferred && result.inputs && !result.checks.snapshot && !result.checks.document,
        "complete context observations without native/source qualification"
    );
    return result.inputs;
}
Point Apply(const AssemblyOccurrencePlacement& transform, Point point)
{
    Point result = transform.translationMm;
    for (std::size_t i = 0; i < 3; ++i)
    {
        for (std::size_t j = 0; j < 3; ++j)
        {
            result[i] += transform.rotation[i][j] * point[j];
        }
    }
    return result;
}
void Near(Point actual, Point expected)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        Require(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) < 1e-10, "independent placement coordinate oracle");
    }
}
AssemblyPlacementCandidate Evaluate(const std::string& text)
{
    auto result = EvaluateAssemblyPlacements(
        Inputs(text),
        2,
        []() -> Result<void>
        {
            return {};
        }
    );
    Require(bool(result), "assembly placement evaluation");
    return std::move(*result);
}
void BaseOracle(const AssemblyPlacementCandidate& candidate)
{
    Require(candidate.occurrences.size() == 2 && candidate.contexts.size() == 2, "two instances, two owned unit contexts");
    // Independent A(x,y,z)=(100-y,200+x,300+z), B(x,y,z)=(x-40,y,z).
    // Check all eight source box corners, not just the frame origin.
    for (double x : { 2., 12. })
    {
        for (double y : { -3., 17. })
        {
            for (double z : { 5., 35. })
            {
                Near(Apply(candidate.occurrences[0], { x, y, z }), { 100 - y, 200 + x, 300 + z });
                Near(Apply(candidate.occurrences[1], { x, y, z }), { x - 40, y, z });
            }
        }
    }
}
void ScalarDiagnosticContract(const std::string& text)
{
    const auto checkpoint = []() -> Result<void>
    {
        return {};
    };
    for (const auto& item : std::array<std::pair<const char*, const char*>, 3>{ { { "(1.E309,0.,0.)", "GeneralNumericRangeUnestablished" },
                                                                                  { "(1.E-400,0.,0.)", "GeneralNumericRangeUnestablished" },
                                                                                  { "(0.,0.)", "GeneralRequires3DGeometry" } } })
    {
        const auto source = Replace(text, "#930=CARTESIAN_POINT('',(0.,0.,0.))", std::string("#930=CARTESIAN_POINT('',") + item.first + ")");
        const auto inputs = Inputs(source);
        const auto result = EvaluateAssemblyPlacements(inputs, 2, checkpoint);
        Require(
            !result && result.error().status == Status::Indeterminate && result.error().key == item.second &&
                result.error().message == "Assembly numeric placement prerequisite is unestablished" &&
                result.error().record == inputs->Bindings().occurrences[0].childItem && result.error().stage == "AssemblyPlacement",
            "assembly scalar conversion retains its legacy message, owning placement and stage"
        );
    }
    const auto scaled = Replace(
        Replace(text, "#930=CARTESIAN_POINT('',(0.,0.,0.))", "#930=CARTESIAN_POINT('',(1.E308,0.,0.))"),
        "SI_UNIT(.MILLI.,.METRE.)",
        "SI_UNIT($,.METRE.)"
    );
    const auto inputs = Inputs(scaled);
    const auto result = EvaluateAssemblyPlacements(inputs, 2, checkpoint);
    Require(
        !result && result.error().status == Status::Indeterminate && result.error().key == "GeneralLengthRangeUnestablished" &&
            result.error().message == "Assembly numeric placement prerequisite is unestablished" &&
            result.error().record == inputs->Bindings().occurrences[0].childItem && result.error().stage == "AssemblyPlacement",
        "assembly coordinate scaling refuses overflow without changing the old diagnostic adapter"
    );
    const auto direction = EvaluateAssemblyPlacements(
        Inputs(Replace(text, "#936=DIRECTION('',(0.,0.,1.))", "#936=DIRECTION('',(0.,1.7E308,1.7E308))")),
        2,
        checkpoint
    );
    Require(
        !direction && direction.error().status == Status::Indeterminate && direction.error().key == "GeneralDegenerateFrame" &&
            direction.error().message == "Native derived evidence is unestablished" && direction.error().stage == "AssemblyPlacement",
        "assembly frame normalization keeps its distinct pre-existing returned-error message"
    );
}

void InstanceExpansion(const std::string& source)
{
    const auto checkpoint = []() -> Result<void>
    {
        return {};
    };
    auto inputs = Inputs(source);
    const auto roots = inputs->Bindings().structure->Roots();
    auto flat = ExpandAssemblyInstances(inputs, roots, 2, 3, 1, checkpoint);
    Require(flat && flat->instances.size() == 3, "root and both repeated instance paths retained");
    Require(
        !flat->instances[0].parentIndex && flat->instances[1].parentIndex == 0 && flat->instances[2].parentIndex == 0,
        "explicit root and occurrence parent identities"
    );
    const auto point = [](const AssemblyInstance& instance)
    {
        AssemblyOccurrencePlacement transform{};
        transform.rotation = instance.rotationToRoot;
        transform.translationMm = instance.translationToRootMm;
        return Apply(transform, { 2, -3, 5 });
    };
    Near(point(flat->instances[1]), { 103, 202, 305 });
    Near(point(flat->instances[2]), { -38, -3, 5 });
    auto count = ExpandAssemblyInstances(inputs, roots, 2, 2, 1, checkpoint);
    Require(!count && count.error().key == "AssemblyInstanceLimit", "expanded count includes root and all repeated instances");
    auto depth = ExpandAssemblyInstances(inputs, roots, 2, 3, 0, checkpoint);
    Require(!depth && depth.error().key == "AssemblyInstanceDepthLimit", "depth failure discards expansion");
    const std::array duplicate{ roots[0], roots[0] };
    Require(!ExpandAssemblyInstances(inputs, duplicate, 2, 6, 2, checkpoint), "duplicate root selection refused");
    const std::array nonroot{ inputs->Bindings().occurrences[0].childDefinition };
    Require(!ExpandAssemblyInstances(inputs, nonroot, 2, 6, 2, checkpoint), "nonroot cannot silently change assembly scope");
    auto nested = Replace(source, "'parent',(#933,#935),#921", "'parent',(#933,#935,#931),#921");
    nested = Replace(
        nested,
        "ENDSEC;\nEND-ISO",
        "#950=PRODUCT('grand','synthetic',$,(#3));\n"
        "#951=PRODUCT_DEFINITION_FORMATION('A',$,#950);\n"
        "#952=PRODUCT_DEFINITION('grand',$,#951,#6);\n"
        "#953=PRODUCT_DEFINITION_SHAPE('',$,#952);\n"
        "#954=SHAPE_DEFINITION_REPRESENTATION(#953,#960);\n"
        "#960=SHAPE_REPRESENTATION('grand',(#964,#966),#961);\n"
        "#961=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11)) REPRESENTATION_CONTEXT('grand','3D'));\n"
        "#962=CARTESIAN_POINT('',(10.,20.,30.));\n#963=DIRECTION('',(-1.,0.,0.));\n"
        "#964=AXIS2_PLACEMENT_3D('',#962,#936,#963);\n"
        "#965=CARTESIAN_POINT('',(-10.,0.,0.));\n#966=AXIS2_PLACEMENT_3D('',#965,#936,#937);\n"
        "#970=NEXT_ASSEMBLY_USAGE_OCCURRENCE('grand-a','same',$,#952,#902,$);\n"
        "#971=PRODUCT_DEFINITION_SHAPE('',$,#970);\n#972=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#974,#971);\n"
        "#974=(REPRESENTATION_RELATIONSHIP('',$,#920,#960) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#975) SHAPE_REPRESENTATION_RELATIONSHIP());\n"
        "#975=ITEM_DEFINED_TRANSFORMATION('',$,#931,#964);\n"
        "#976=NEXT_ASSEMBLY_USAGE_OCCURRENCE('grand-b','same',$,#952,#902,$);\n"
        "#977=PRODUCT_DEFINITION_SHAPE('',$,#976);\n#978=CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#984,#977);\n"
        "#984=(REPRESENTATION_RELATIONSHIP('',$,#920,#960) REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#985) SHAPE_REPRESENTATION_RELATIONSHIP());\n"
        "#985=ITEM_DEFINED_TRANSFORMATION('',$,#931,#966);\nENDSEC;\nEND-ISO"
    );
    auto tree = Inputs(nested);
    auto expanded = ExpandAssemblyInstances(tree, tree->Bindings().structure->Roots(), 4, 7, 2, checkpoint);
    Require(expanded && expanded->instances.size() == 7, "three definitions expand to seven path-specific nodes");
    auto lateCount = ExpandAssemblyInstances(tree, tree->Bindings().structure->Roots(), 4, 6, 2, checkpoint);
    Require(!lateCount && lateCount.error().key == "AssemblyInstanceLimit", "last repeated path exceeding budget cannot be omitted");
    auto lateDepth = ExpandAssemblyInstances(tree, tree->Bindings().structure->Roots(), 4, 7, 1, checkpoint);
    Require(!lateDepth && lateDepth.error().key == "AssemblyInstanceDepthLimit", "nested depth is bounded independently of node count");
    Near(point(expanded->instances[3]), { -93, -182, 335 });
    Near(point(expanded->instances[4]), { 48, 23, 35 });
    Near(point(expanded->instances[5]), { 93, 202, 305 });
    Near(point(expanded->instances[6]), { -48, -3, 5 });
    Require(
        expanded->instances[3].usage == expanded->instances[5].usage && expanded->instances[3].parentIndex != expanded->instances[5].parentIndex,
        "same usage under repeated parent has distinct path identity"
    );
    std::size_t calls = 0;
    auto measured = ExpandAssemblyInstances(
        tree,
        tree->Bindings().structure->Roots(),
        4,
        7,
        2,
        [&]() -> Result<void>
        {
            ++calls;
            return {};
        }
    );
    Require(bool(measured), "measure all expansion checkpoints");
    for (std::size_t stop = 1; stop <= calls; ++stop)
    {
        std::size_t n = 0;
        auto cancelled = ExpandAssemblyInstances(
            tree,
            tree->Bindings().structure->Roots(),
            4,
            7,
            2,
            [&]() -> Result<void>
            {
                if (++n == stop)
                {
                    return tl::unexpected(Failure{ Status::Cancelled, "InstanceCancel", "cancelled", {} });
                }
                return {};
            }
        );
        Require(!cancelled && cancelled.error().key == "InstanceCancel", "cancellation never returns partial instance tree");
    }
    tree.reset();
    Require(expanded->placements.source->SourceFields().Source().Bytes() == nested, "expanded candidate owns original input");
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "pass repeated box fixture");
        std::ifstream stream(argv[1], std::ios::binary);
        Require(bool(stream), "fixture exists");
        const std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
        ScalarDiagnosticContract(text);
        InstanceExpansion(text);
        auto candidate = Evaluate(text);
        BaseOracle(candidate);
        auto reversed = Replace(
            Replace(text, "REPRESENTATION_RELATIONSHIP('',$,#20,#920)", "REPRESENTATION_RELATIONSHIP('',$,#920,#20)"),
            "ITEM_DEFINED_TRANSFORMATION('',$,#931,#933)",
            "ITEM_DEFINED_TRANSFORMATION('',$,#933,#931)"
        );
        BaseOracle(Evaluate(reversed));
        auto rescaled = Replace(
            Replace(text, "#936=DIRECTION('',(0.,0.,1.))", "#936=DIRECTION('',(0.,0.,2.))"),
            "#938=DIRECTION('',(0.,1.,0.))",
            "#938=DIRECTION('',(0.,5.,0.))"
        );
        BaseOracle(Evaluate(rescaled));
        auto mixed = Replace(
            Replace(
                text,
                "#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#10,#11))",
                "#921=(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNIT_ASSIGNED_CONTEXT((#939,#11))"
            ),
            "ENDSEC;\nEND-ISO",
            "#939=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));\nENDSEC;\nEND-ISO"
        );
        auto equivalent = Replace(
            Replace(mixed, "#932=CARTESIAN_POINT('',(100.,200.,300.))", "#932=CARTESIAN_POINT('',(0.1,0.2,0.3))"),
            "#934=CARTESIAN_POINT('',(-40.,0.,0.))",
            "#934=CARTESIAN_POINT('',(-0.04,0.,0.))"
        );
        BaseOracle(Evaluate(equivalent));
        auto sharedItem = Replace(
            Replace(
                Replace(mixed, "#930=CARTESIAN_POINT('',(0.,0.,0.))", "#930=CARTESIAN_POINT('',(1.,2.,3.))"),
                "'parent',(#933,#935),#921",
                "'parent',(#933,#935,#931),#921"
            ),
            "#925=ITEM_DEFINED_TRANSFORMATION('',$,#931,#935)",
            "#925=ITEM_DEFINED_TRANSFORMATION('',$,#931,#931)"
        );
        auto sharedFrame = Evaluate(sharedItem);
        Near(Apply(sharedFrame.occurrences[1], { 2, -3, 5 }), { 1001, 1995, 3002 });
        auto converted = Replace(
            mixed,
            "#939=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));",
            "#939=(CONVERSION_BASED_UNIT('synthetic inch',#940) LENGTH_UNIT() NAMED_UNIT(*));\n"
            "#940=MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#10);"
        );
        auto inches = Evaluate(converted);
        Near(Apply(inches.occurrences[0], { 2, -3, 5 }), { 2543, 5082, 7625 });
        Near(Apply(inches.occurrences[1], { 2, -3, 5 }), { -1014, -3, 5 });
        bool exact = false;
        for (const auto& context : inches.contexts)
        {
            exact |= context.length.scaleMm == Fraction(127, 5) && context.length.definitions.size() == 1;
        }
        Require(exact, "exact source conversion ratio and chain retained");
        auto shifted = Evaluate(Replace(text, "#930=CARTESIAN_POINT('',(0.,0.,0.))", "#930=CARTESIAN_POINT('',(1.,2.,3.))"));
        Near(Apply(shifted.occurrences[0], { 2, -3, 5 }), { 105, 201, 302 });
        Near(Apply(shifted.occurrences[1], { 2, -3, 5 }), { -39, -5, 2 });
        auto defaults = Evaluate(Replace(text, "#933=AXIS2_PLACEMENT_3D('',#932,#936,#938)", "#933=AXIS2_PLACEMENT_3D('',#932,$,$)"));
        Near(Apply(defaults.occurrences[0], { 2, -3, 5 }), { 102, 197, 305 });
        auto parallelDefault = Evaluate(Replace(text, "#933=AXIS2_PLACEMENT_3D('',#932,#936,#938)", "#933=AXIS2_PLACEMENT_3D('',#932,#937,$)"));
        Near(Apply(parallelDefault.occurrences[0], { 2, -3, 5 }), { 105, 202, 297 });
        auto inputs = Inputs(text);
        std::size_t checkpoints = 0;
        auto completed = EvaluateAssemblyPlacements(
            inputs,
            2,
            [&]() -> Result<void>
            {
                ++checkpoints;
                return {};
            }
        );
        Require(bool(completed) && checkpoints > 8, "unit arithmetic/frame/commit checkpoints run");
        for (std::size_t stop = 1; stop <= checkpoints; ++stop)
        {
            std::size_t visits = 0;
            auto cancelled = EvaluateAssemblyPlacements(
                inputs,
                2,
                [&]() -> Result<void>
                {
                    if (++visits == stop)
                    {
                        return tl::unexpected(Failure{ Status::Cancelled, "SyntheticAssemblyCancel", "cancelled", {}, "AssemblyPlacement" });
                    }
                    return {};
                }
            );
            Require(
                !cancelled && cancelled.error().status == Status::Cancelled && cancelled.error().key == "SyntheticAssemblyCancel",
                "every cancellation boundary withholds all candidate placements"
            );
        }
        auto limited = EvaluateAssemblyPlacements(
            inputs,
            1,
            []() -> Result<void>
            {
                return {};
            }
        );
        Require(!limited && limited.error().status == Status::ResourceLimit, "occurrence limit never yields partial instance set");
        auto unrelated = ResolveLengthScale(
            *inputs,
            inputs->Bindings().occurrences[0].usage,
            []() -> Result<void>
            {
                return {};
            }
        );
        Require(!unrelated && unrelated.error().key == "AssemblyUnitContextUnestablished", "unowned context cannot borrow another scale");
        auto negativeScale = EvaluateAssemblyPlacements(
            Inputs(Replace(converted, "LENGTH_MEASURE(25.4)", "LENGTH_MEASURE(-25.4)")),
            2,
            []() -> Result<void>
            {
                return {};
            }
        );
        Require(
            !negativeScale && negativeScale.error().key == "GeneralUnitScaleNotPositive",
            "negative scale is refused without taking absolute value"
        );
        auto pointItem = Replace(
            Replace(text, "'parent',(#933,#935),#921", "'parent',(#933,#935,#932),#921"),
            "#915=ITEM_DEFINED_TRANSFORMATION('',$,#931,#933)",
            "#915=ITEM_DEFINED_TRANSFORMATION('',$,#931,#932)"
        );
        auto unsupported = EvaluateAssemblyPlacements(
            Inputs(pointItem),
            2,
            []() -> Result<void>
            {
                return {};
            }
        );
        Require(!unsupported && unsupported.error().key == "AssemblyFrameTypeUnsupported", "a bound generic item is not a guessed coordinate frame");
        auto huge = Replace(
            Replace(text, "#930=CARTESIAN_POINT('',(0.,0.,0.))", "#930=CARTESIAN_POINT('',(1.E308,1.E308,1.E308))"),
            "#932=CARTESIAN_POINT('',(100.,200.,300.))",
            "#932=CARTESIAN_POINT('',(1.E308,1.E308,1.E308))"
        );
        auto overflow = EvaluateAssemblyPlacements(
            Inputs(huge),
            2,
            []() -> Result<void>
            {
                return {};
            }
        );
        Require(!overflow && overflow.error().status == Status::Indeterminate, "nonfinite composed placement is not published or clamped");
        inputs.reset();
        Require(completed->source->SourceFields().Source().Bytes() == text, "candidate owns immutable source context receipt");
        std::cout << "Assembly placement numeric contracts passed; no native assembly qualification\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
