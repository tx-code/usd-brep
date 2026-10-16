// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepPlanar.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace
{
using namespace step_planar;
template <class T>
concept WritableFractionMembers = requires(T& value) {
    value.numerator = std::int64_t{};
    value.denominator = std::int64_t{};
};
static_assert(!WritableFractionMembers<Rational>, "A caller cannot bypass the rational constructor invariants");
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    Require(static_cast<bool>(stream), "Missing frozen fixture");
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}
std::string Replace(std::string text, const std::string& before, const std::string& after)
{
    const auto at = text.find(before);
    Require(at != std::string::npos && text.find(before, at + before.size()) == std::string::npos, "Unique mutation preimage required");
    text.replace(at, before.size(), after);
    return text;
}
Outcome Inspect(const std::string& text, Quality quality = Quality::PlanarFixture(), Limits limits = {})
{
    const auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    const auto semantic = step_semantics::Check(raw, {});
    return Check(semantic, std::move(quality), limits);
}
const RuleEvidence& Rule(const Outcome& result, const std::string& name)
{
    for (const auto& rule : result.evidence.rules)
    {
        if (rule.name == name)
        {
            return rule;
        }
    }
    throw std::runtime_error("Required evidence absent");
}
} // namespace
int main(int argc, char** argv)
{
    std::size_t groups = 0;
    try
    {
        using namespace step_planar;
        Require(argc == 2, "Pass frozen corpus case directory");
        const std::filesystem::path corpus(argv[1]);
        const auto p01 = Read(corpus / "S203-P01/input.step");
        Require(Rational::Decimal("-12.50E-1") == Rational(-5, 4), "Exact decimal reduction");
        Require(Rational(1, 3) + Rational(1, 6) == Rational(1, 2) && Rational(2, 3) / Rational(-4, 5) == Rational(-5, 6), "Rational operations");
        const auto reduced = Rational(2, 4);
        Require(reduced.Numerator() == 1 && reduced.Denominator() == 2 && reduced == Rational(1, 2), "Canonical read-only fraction");
        Require(Rational(0, 12).Numerator() == 0 && Rational(0, 12).Denominator() == 1, "Canonical zero");
        const std::array<std::pair<std::int64_t, std::int64_t>, 3> invalidFractions = {
            { { 1, 0 }, { 1, -1 }, { std::numeric_limits<std::int64_t>::min(), 1 } }
        };
        for (const auto& [numerator, denominator] : invalidFractions)
        {
            bool rejected = false;
            try
            {
                (void)Rational(numerator, denominator);
            }
            catch (const std::overflow_error&)
            {
                rejected = true;
            }
            Require(rejected, "Invalid denominator or out-of-range numerator must not create a rational");
        }
        const auto max = std::numeric_limits<std::int64_t>::max();
        Require(Rational(max - 1, max) < Rational(max, max - 1) && Rational(-max, max - 1) < Rational(-max + 1, max), "Overflow-free comparison");
        bool overflow = false;
        try
        {
            (void)(Rational(max) + Rational(1));
        }
        catch (const std::overflow_error&)
        {
            overflow = true;
        }
        Require(overflow, "Overflow must not round or wrap");
        ++groups;

        Require(
            Not(Logical::Unknown) == Logical::Unknown && !QuerySelects(Logical::Unknown) && !QuerySelects(Logical::False) &&
                QuerySelects(Logical::True),
            "EXPRESS LOGICAL/QUERY semantics"
        );
        ++groups;

        auto result = Inspect(p01);
        Require(result.status == Status::Checked, "P01 source rules and region");
        Require(result.localRules == "Passed" && result.globalRules == "Passed" && result.geometry == "Passed", "Successful planar phases");
        auto evidence = result.evidence;
        result = {};
        Require(
            evidence.source->Source().Find("300") && evidence.faces[0].areaSquaredMm == Rational(9216),
            "Owning evidence survives input/outcome destruction"
        );
        static_assert(std::is_const_v<std::remove_reference_t<decltype(evidence.source->Source().Records()[0])>>);
        Require(evidence.faces[0].centroidMm == Vector{ Rational(17, 5), Rational(26, 5), 5 }, "Translated plane centroid");
        ++groups;

        result = Inspect(p01);
        std::map<std::string, int> classes;
        for (const auto& r : result.evidence.rules)
        {
            if (r.kind == "GLOBAL")
            {
                ++classes[r.applicability];
            }
        }
        Require(classes == std::map<std::string, int>{ { "Mandatory", 5 }, { "Conditional", 1 }, { "Guarded", 41 } }, "47 applicability classes");
        const auto& g36 = Rule(result, "restrict_representation_for_surface_condition");
        Require(
            g36.value == Logical::True && g36.facts.at("correlation_UNKNOWN_count") == "1" &&
                g36.populations.at("PROPERTY_DEFINITION_REPRESENTATION").size() == 1,
            "SDR/PDR UNKNOWN witness"
        );
        Require(
            Rule(result, "validate_dependently_instantiable_entity_data_types").populations.at("LOOP").size() == 1,
            "EL inherited LOOP population"
        );
        ++groups;

        for (const auto name : { "S203-N11", "S203-N12", "S203-N13", "S203-N19", "S203-N20", "S203-N21", "S203-N22", "S203-N23", "S203-N24" })
        {
            Require(Inspect(Read(corpus / name / "input.step")).status == Status::Rejected, "Required negative control rejected");
        }
        Require(Rule(Inspect(Read(corpus / "S203-N24/input.step")), "PATH.WR1").value == Logical::True, "Closure failure must not falsify PATH");
        ++groups;

        const auto boundary = Replace(p01, "#130=CARTESIAN_POINT('',(3.,-2.,5.));", "#130=CARTESIAN_POINT('',(3.,-2.,5.0000001));");
        Require(Inspect(boundary).status == Status::Checked, "Consistency threshold inclusive exact boundary");
        auto strict = Quality::PlanarFixture();
        strict.id = "explicit-stricter-probe";
        strict.consistencyMm = Rational(1, 20000000);
        Require(Inspect(boundary, strict).status == Status::Rejected, "Tighter explicit Q must reject same source");
        ++groups;

        const auto work = Inspect(p01).work;
        Require(Inspect(p01, Quality::PlanarFixture(), { work }).status == Status::Checked, "Exact work limit");
        Require(Inspect(p01, Quality::PlanarFixture(), { work - 1 }).status == Status::ResourceLimit, "N-1 work limit must block");
        ++groups;

        const auto localFailure = Inspect(Read(corpus / "S203-N11/input.step"));
        const auto globalFailure = Inspect(Read(corpus / "S203-N29/input.step"));
        const auto geometryFailure = Inspect(Read(corpus / "S203-N20/input.step"));
        const auto admissionFailure = Inspect(p01, Quality::PlanarFixture(), { 0 });
        Require(
            localFailure.localRules == "Failed" && localFailure.globalRules == "NotRun" && localFailure.geometry == "NotRun",
            "Local failure gate"
        );
        Require(
            globalFailure.localRules == "Passed" && globalFailure.globalRules == "Failed" && globalFailure.geometry == "NotRun",
            "Global failure gate"
        );
        Require(
            geometryFailure.localRules == "Passed" && geometryFailure.globalRules == "Passed" && geometryFailure.geometry == "Failed",
            "Geometry failure gate"
        );
        Require(
            admissionFailure.localRules == "NotRun" && admissionFailure.globalRules == "NotRun" && admissionFailure.geometry == "NotRun",
            "Admission stop precedes planar phases"
        );
        ++groups;

        bool invalid = false;
        try
        {
            auto q = Quality::PlanarFixture();
            q.id.clear();
            (void)Inspect(p01, q);
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Require(invalid, "Quality selection must be explicit");
        ++groups;

        const auto irrational = Replace(p01, "#132=DIRECTION('',(0.6,0.8,0.));", "#132=DIRECTION('',(1.,1.,0.));");
        result = Inspect(irrational);
        Require(
            result.status == Status::Checked && result.evidence.faces[0].xNormSquared == Rational(2) &&
                result.evidence.faces[0].areaSquaredMm == Rational(9216),
            "Irrational unit axis retained algebraically"
        );
        ++groups;

        const auto defaultAxis = Replace(p01, "#133=AXIS2_PLACEMENT_3D('',#130,#131,#132);", "#133=AXIS2_PLACEMENT_3D('',#130,$,#132);");
        Require(Inspect(defaultAxis).status == Status::UpstreamFailure, "Unused original axis must remain an explicit orphan, not disappear");
        auto unresolved = Replace(p01, "#133=AXIS2_PLACEMENT_3D('',#130,#131,#132);", "#133=AXIS2_PLACEMENT_3D('',#130,$,#132);");
        unresolved = Replace(unresolved, "#131=DIRECTION('',(0.,0.,1.));\n", "");
        unresolved = Replace(unresolved, "#132=DIRECTION('',(0.6,0.8,0.));", "#132=DIRECTION('',(0.,0.,1.));");
        result = Inspect(unresolved);
        Require(
            result.status == Status::Indeterminate && Rule(result, "AXIS2_PLACEMENT_3D.WR4").value == Logical::True,
            "Source optional WR4 true but derived axes indeterminate"
        );
        Require(
            result.localRules == "Passed" && result.globalRules == "Passed" && result.geometry == "Indeterminate" &&
                result.diagnostics[0].source.stage == "D4",
            "Derived geometry phase remains unestablished"
        );
        ++groups;

        auto huge = Replace(p01, "#200=DIRECTION('',(0.6,0.8,0.));", "#200=DIRECTION('',(1.E200,0.8,0.));");
        result = Inspect(huge);
        Require(
            result.status == Status::Indeterminate && !result.diagnostics[0].source.recordId.empty(),
            "Numeric capacity failure needs source provenance"
        );
        Require(
            result.localRules == "Indeterminate" && result.globalRules == "NotRun" && result.geometry == "NotRun" &&
                result.diagnostics[0].source.stage == "D3",
            "Local numeric phase remains unestablished"
        );
        ++groups;

        const auto p08 = Read(corpus / "S203-P08/input.step");
        const auto positive = Replace(p08, "LENGTH_MEASURE(0.0001)", "POSITIVE_LENGTH_MEASURE(0.0001)");
        result = Inspect(positive);
        Require(
            result.status == Status::Checked && Rule(result, "POSITIVE_LENGTH_MEASURE.WR1").value == Logical::True &&
                Rule(result, "NON_NEGATIVE_LENGTH_MEASURE.WR1").value == Logical::True,
            "Inherited named TYPE predicates"
        );
        ++groups;

        auto raw = step_source::InspectBytes(p01, step_source::Limits::PlanarFixtures());
        auto semantic = step_semantics::Check(raw, {});
        result = Check(semantic, Quality::PlanarFixture(), {});
        std::string json;
        Require(InspectJson(raw, semantic, result, json), "Bounded JSON report");
        raw.limits.reportBytes = json.size();
        Require(InspectJson(raw, semantic, result, json), "Budget number spelling changes report size");
        const auto bytes = json.size();
        raw.limits.reportBytes = bytes;
        Require(InspectJson(raw, semantic, result, json) && json.size() == bytes, "Exact report byte limit");
        raw.limits.reportBytes = bytes - 1;
        Require(!InspectJson(raw, semantic, result, json) && json.empty(), "No truncated success JSON at N-1");
        std::string upstream;
        raw.limits.reportBytes = bytes;
        Require(step_semantics::InspectJson(raw, semantic, upstream), "Nested report budget control");
        raw.limits.reportBytes = upstream.size() + 32;
        Require(!InspectJson(raw, semantic, result, json) && json.empty(), "Late outer report exhaustion must discard the upstream JSON too");
        raw.limits.reportBytes = 0;
        json = "stale prior report";
        Require(!InspectJson(raw, semantic, result, json) && json.empty(), "Zero report budget must clear prior output");
        ++groups;

        result = Inspect(Read(corpus / "S203-P11/input.step"));
        Require(
            result.status == Status::Deferred && result.evidence.rules.empty() && result.geometry == "NotRun",
            "Conversion still deferred without native build"
        );
        ++groups;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Contract failure after " << groups << " groups: " << e.what() << '\n';
        return 1;
    }
    std::cout << "Passed " << groups << " source planar contract groups\n";
    return 0;
}
