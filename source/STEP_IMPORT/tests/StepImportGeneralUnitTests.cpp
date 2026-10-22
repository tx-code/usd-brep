// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"
#include "StepImportPlan.h"
#include "StepImportUnits.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void Near(double actual, double expected, double tolerance, const char* message)
{
    Require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void ConversionUnitContract(const std::filesystem::path& source)
{
    std::ifstream input(source, std::ios::binary);
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const std::string original = "#10=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.));";
    const auto at = text.find(original);
    Require(at != std::string::npos, "public fixture length declaration");
    text.replace(at, original.size(), "#10=(CONVERSION_BASED_UNIT('arbitrary',#1000) LENGTH_UNIT() NAMED_UNIT(#1001));");
    const auto end = text.find("ENDSEC;\nEND-ISO");
    Require(end != std::string::npos, "public fixture DATA end");
    text.insert(
        end,
        "#1000=MEASURE_WITH_UNIT(LENGTH_MEASURE(0.0254),#1002);\n"
        "#1001=DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.);\n"
        "#1002=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT($,.METRE.));\n"
    );
    auto options = step_import::Options::PlanarFixture();
    options.generalBrep = true;
    options.quality = step_import::Quality::GeneralDevelopment();
    auto outcome = step_import::ImportBytes(text, options);
    Require(outcome.status == step_import::Status::Accepted && outcome.model, "checked conversion source builds natively");
    auto model = outcome.model;
    outcome = {};
    const auto units = model->LengthUnits();
    Require(
        units.lengthToMm == step_import::Fraction(127, 5) && units.generalDefinitions.size() == 1,
        "owning model retains exact conversion after report disposal"
    );
    auto copy = units;
    copy.generalDefinitions.clear();
    Require(model->LengthUnits().generalDefinitions.size() == 1, "length normalization is an independent returned value");
    const auto sourceOwner = model->Source();
    const auto& definition = units.generalDefinitions.front();
    Require(
        sourceOwner->Source().Records()[definition.factor].id == "1000" &&
            sourceOwner->Field(definition.factor, "unit_component").ReferenceTarget() == definition.baseUnit,
        "normalization retains actual typed factor/base identities"
    );
    Near(*model->MaterialVolumeMm3(), 6000 * 25.4 * 25.4 * 25.4, 1e-4, "independent converted box volume");
    Require(
        model->ClassifyMaterial({ 7 * 25.4, 7 * 25.4, 20 * 25.4 }) == step_import::MaterialPointClass::Inside,
        "material query uses canonical millimetres"
    );
    bool wrongQuantity = false;
    try
    {
        step_import::detail::ResolveLengthScale(
            *sourceOwner,
            sourceOwner->UnitContext().planeAngleUnit,
            []
            {
            }
        );
    }
    catch (const step_import::detail::Failure& failure)
    {
        wrongQuantity = failure.status == step_import::Status::Unsupported && failure.key == "GeneralConversionQuantityUnsupported";
    }
    Require(wrongQuantity, "angle units cannot substitute for a nominal length root");
    auto wideText = text;
    const std::string factor = "LENGTH_MEASURE(0.0254)";
    const auto factorAt = wideText.find(factor);
    Require(factorAt != std::string::npos, "public conversion factor");
    wideText.replace(factorAt, factor.size(), "LENGTH_MEASURE(0.001234567890123456789)");
    auto wideOutcome = step_import::ImportBytes(wideText, options);
    Require(wideOutcome.status == step_import::Status::Accepted && wideOutcome.model, "wide factor builds after exact SI cancellation");
    auto wideModel = wideOutcome.model;
    wideOutcome = {};
    const auto wideUnits = wideModel->LengthUnits();
    Require(
        wideUnits.lengthToMm == step_import::Fraction(1234567890123456789LL, 1000000000000000000LL) &&
            wideUnits.generalDefinitions.front().factorValue.Numerator() == "1234567890123456789" &&
            wideUnits.generalDefinitions.front().factorValue.Denominator() == "1000000000000000000000" &&
            wideUnits.generalDefinitions.front().scaleMm.Denominator() == "1000000000000000000" && wideModel->Source()->Source().Bytes() == wideText,
        "owning exact unit evidence survives report disposal without narrowing source factors"
    );
    const double scale = 1.234567890123456789;
    Near(*wideModel->MaterialVolumeMm3(), 6000 * scale * scale * scale, 1e-6, "independent wide-factor box volume");
    auto lateCancel = options;
    lateCancel.execution.control = std::make_shared<step_import::ImportControl>();
    lateCancel.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::NativeReady)
        {
            lateCancel.execution.control->RequestCancel();
        }
    };
    const auto wideStopped = step_import::ImportBytes(wideText, lateCancel);
    Require(wideStopped.status == step_import::Status::Cancelled && !wideStopped.model, "wide arithmetic never bypasses final cancellation");
    auto cancel = options;
    cancel.execution.control = std::make_shared<step_import::ImportControl>();
    std::size_t boundaries = 0;
    cancel.execution.progress = [&](step_import::Progress progress)
    {
        if (progress.phase == step_import::Phase::SemanticsChecked && ++boundaries == 3)
        {
            cancel.execution.control->RequestCancel();
        }
    };
    const auto stopped = step_import::ImportBytes(text, cancel);
    Require(
        stopped.status == step_import::Status::Cancelled && !stopped.model && stopped.report.nativeCalls == 0,
        "conversion preflight remains cancellable before native allocation"
    );
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "public box fixture required");
        const auto planar = step_import::ImportFile(argv[1], step_import::Options::PlanarFixture());
        Require(
            planar.status == step_import::Status::Accepted && planar.model && planar.model->LengthUnits().lengthToMm == step_import::Fraction(1),
            "proved planar snapshots also own length normalization"
        );
        ConversionUnitContract(argv[1]);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
