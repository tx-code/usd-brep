// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepSemantics.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
using namespace step_semantics;
void Need(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
std::string Read(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    Need(bool(in), "public fixture required");
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}
void Replace(std::string& text, const std::string& old, const std::string& replacement)
{
    const auto at = text.find(old);
    Need(at != std::string::npos, "public mutation token");
    text.replace(at, old.size(), replacement);
}
void BreakFace(
    std::string& text,
    const std::string& face = "10507",
    const std::string& bound = "10506",
    const std::string& loop = "10505",
    const std::string& extra = "990000"
)
{
    Replace(text, "#" + face + "=ADVANCED_FACE('',(#" + bound + ")", "#" + face + "=ADVANCED_FACE('',(#" + bound + ",#" + extra + ")");
    text.insert(text.rfind("ENDSEC;"), "#" + extra + "=FACE_OUTER_BOUND('',#" + loop + ",.T.);\n");
}
Outcome Selected(const std::string& text, const std::string& id = "7", std::size_t visits = 100000, std::size_t failures = 4)
{
    const auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
    Need(bool(raw.source), "public input captures");
    return CheckSelectedIndependentProducts(raw, Limits{}, SourcePolicy::Strict, { raw.sha256, { id }, failures, visits });
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Need(argc == 3, "distinct and shared public fixtures required");
        const auto original = Read(argv[1]);
        auto text = original;
        BreakFace(text);
        auto raw = step_source::InspectBytes(text, step_source::Limits::PlanarFixtures());
        Need(raw.source->Sha256() == raw.sha256, "immutable owner capture hash");
        const auto ordinary = CheckIndependentProducts(raw, Limits{}, SourcePolicy::Strict);
        Need(ordinary.status == Status::Rejected && !ordinary.document, "ordinary whole-source rejection remains");
        auto accepted = Selected(text);
        Need(
            accepted.status == Status::CheckedSelectedDefinitions && accepted.document && accepted.document->PartCount() == 1,
            "explicit selected source qualifies only the unaffected definition"
        );
        auto document = accepted.document;
        auto part = document->Part(0);
        accepted = {};
        raw = {};
        const auto& receipt = *part->SourceSelection();
        Need(
            part->Source().Bytes() == text && receipt.localityEstablished && receipt.failures.size() == 1 &&
                receipt.selectedDefinitionIds == std::vector<std::string>{ "7" } &&
                receipt.excludedDefinitionIds == std::vector<std::string>{ "902" } && receipt.failures[0].diagnostic.source.recordId == "10507" &&
                receipt.request.sourceSha256 == part->Source().Sha256(),
            "owning selected receipt preserves original failure, complete inventory and source"
        );
        const auto denied = Selected(text, "902");
        Need(denied.status == Status::Rejected && !denied.document, "selected invalid definition remains rejected");
        text = original;
        BreakFace(text, "507", "506", "505");
        Need(Selected(text, "902").status == Status::CheckedSelectedDefinitions, "excluded first failure cannot poison a later valid definition");
        auto shared = Read(argv[2]);
        BreakFace(shared, "507", "506", "505");
        for (const auto id : { "7", "902" })
        {
            const auto failed = Selected(shared, id);
            Need(failed.status == Status::Rejected && !failed.document, "shared invalid face blocks either selected use");
        }
        text = original;
        BreakFace(text);
        const auto capped = Selected(text, "7", 1);
        Need(
            capped.status == Status::ResourceLimit && !capped.document && capped.sourceInfluenceVisits == 1,
            "influence budget cannot publish partial qualification"
        );
        BreakFace(text, "10517", "10516", "10515", "990001");
        Need(Selected(text, "7", 100000, 1).status == Status::ResourceLimit, "source failure cap is cumulative");
        raw = step_source::InspectBytes(original, step_source::Limits::PlanarFixtures());
        auto forged = raw;
        forged.sha256 = std::string(64, '0');
        const auto mismatch = CheckSelectedIndependentProducts(forged, Limits{}, SourcePolicy::Strict, { forged.sha256, { "7" }, 4, 100000 });
        Need(
            mismatch.status == Status::Unsupported && !mismatch.document && raw.source->Sha256() == raw.sha256,
            "mutable outcome identity cannot rebind immutable source"
        );
        const auto missing = Selected(original, "999999");
        Need(missing.status == Status::Unsupported && !missing.document, "every selected identity must exist");
        const auto noError = Selected(original);
        Need(
            noError.document && noError.document->SourceSelection()->failures.empty(),
            "explicit selection without errors invents no isolated failure"
        );
        std::cout << "source selection isolation contracts passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
