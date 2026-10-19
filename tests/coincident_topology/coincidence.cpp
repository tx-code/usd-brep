// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include <SmApiGeneral.h>
#include <SmApiPrimitives.h>
#include <SmAssertArray.h>
#include <SmBrep.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace
{
void Require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
std::unique_ptr<SmBrep> Box(double x = 0)
{
    SmBrep* raw = nullptr;
    const auto status = SmApiCreateBox(SmVector3d(x, 0, 0), 10, 20, 30, raw);
    std::unique_ptr<SmBrep> result(raw);
    Require(status == SM_SUCCESS && bool(result), "analytic box construction");
    return result;
}
std::unique_ptr<SmBrep> Pair(double x)
{
    auto first = Box();
    auto second = Box(x);
    // MergeBrep copies both valid pointer graphs without intersection/sewing.
    // At x=0 the distinct topology intentionally shares exact geometry. This
    // creates a safe coincidence control without breaking an edge or vertex.
    Require(first->MergeBrep(*second) == SM_SUCCESS, "copying the second box topology");
    return first;
}
void Query(SmBrep& body, bool expected, bool all)
{
    for (const auto level : { SM_LEVEL_0, SM_LEVEL_2 })
    {
        SmTArray<SmTopology*> first, second;
        SmAssertArray reports;
        const bool found = body.ContainsCoincidentTopology(.001, &first, &second, &reports, level, nullptr, all) != FALSE;
        Require(found == expected, "coincidence predicate disagrees with analytic truth");
        Require(first.GetSize() == second.GetSize(), "coincidence output pairing");
        Require((first.GetSize() != 0) == expected, "coincidence output agrees with predicate");
        Require(!expected || all || first.GetSize() == 1, "first-pair query stops at its first witness");
        Require((reports.GetSize() != 0) == (expected && level == SM_LEVEL_2), "report level does not change geometric truth");
    }
}
void Root(SmBrep& body, bool expected, SmAssertTestLevel level, SmTArray<ULONG>* requests = nullptr)
{
    SmAssertArray reports;
    const bool valid = body.AssertValid(&reports, level, SM_NO_WALK, requests) != FALSE;
    Require(valid == expected, "root validation coincidence result");
    Require((reports.GetSize() == 0) == expected, "root validation diagnostic presence");
    if (!expected)
    {
        bool rootTest = false;
        for (ULONG i = 0; i < reports.GetSize(); ++i)
        {
            rootTest |= reports[i]->GetListIndex() == 0 && reports[i]->GetTestIndex() == 15;
        }
        Require(rootTest, "root test 15 reports the coincidence");
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        SmApiCreateContext();
        Require(argc == 2, "one contract case required");
        const std::string_view mode(argv[1]);
        if (mode == "clean")
        {
            auto body = Box();
            Query(*body, false, false);
            Query(*body, false, true);
            auto separated = Pair(100);
            Query(*separated, false, true);
        }
        else if (mode == "first" || mode == "all")
        {
            auto body = Pair(0);
            Query(*body, true, mode == "all");
        }
        else if (mode == "touching" || mode == "outside")
        {
            const bool touching = mode == "touching";
            auto body = Pair(touching ? 10.0005 : 10.002);
            Query(*body, touching, true);
        }
        else if (mode == "optional")
        {
            auto body = Box();
            SmTArray<SmTopology*> first, second;
            first.Add(nullptr);
            second.Add(nullptr);
            Require(!body->ContainsCoincidentTopology(.001, &first, nullptr), "only the first output requested");
            Require(!body->ContainsCoincidentTopology(.001, nullptr, &second), "only the second output requested");
            Require(!body->ContainsCoincidentTopology(.001), "no output arrays requested");
            Require(first.GetSize() == 0 && second.GetSize() == 0, "optional output arrays remain empty");
            auto overlap = Pair(0);
            Require(overlap->ContainsCoincidentTopology(.001, &first, nullptr), "first-only coincidence witness");
            Require(overlap->ContainsCoincidentTopology(.001, nullptr, &second), "second-only coincidence witness");
            Require(first.GetSize() == 1 && second.GetSize() == 1, "optional witness outputs are independently populated");
        }
        else if (mode == "root-clean")
        {
            auto body = Box();
            Root(*body, true, SM_LEVEL_2);
            SmAssertArray reports;
            Require(body->AssertValid(&reports, SM_LEVEL_2, SM_WALK) && reports.GetSize() == 0, "full analytic box validation");
        }
        else if (mode == "root-overlap" || mode == "levels")
        {
            auto body = Pair(0);
            if (mode == "levels")
            {
                Root(*body, true, SM_LEVEL_0);
            }
            Root(*body, false, SM_LEVEL_2);
        }
        else if (mode == "requests")
        {
            auto body = Pair(0);
            SmTArray<ULONG> requests;
            requests.Add(13); // Context check: unrelated to coincidence test 15.
            Root(*body, true, SM_LEVEL_GIVEN, &requests);
            requests.ReSet();
            requests.Add(15);
            Root(*body, false, SM_LEVEL_GIVEN, &requests);
        }
        else
        {
            throw std::runtime_error("unknown contract case");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
