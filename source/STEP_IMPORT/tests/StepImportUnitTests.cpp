// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("Pass frozen case directory");
        }
        const auto scaled = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P06/input.step", step_import::Options::PlanarFixture());
        if (scaled.status != step_import::Status::Accepted || !scaled.model)
        {
            throw std::runtime_error("P06 SI centimetres must deliver the ten-times physical surface");
        }
        const auto info = scaled.model->Info();
        if (std::abs(info.areaMm2 - 9600) > 1e-6 || std::abs(info.boundaryMm - 400) > 1e-7 ||
            info.boundsMm != step_import::Bounds{ step_import::Point{ -34, -20, 50 }, step_import::Point{ 102, 124, 50 } })
        {
            throw std::runtime_error("P06 canonical geometry follows the declared unit, not the unchanged labels");
        }
        const auto same = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P07/input.step", step_import::Options::PlanarFixture());
        if (!same.model || std::abs(same.model->Info().areaMm2 - 96) > 1e-6)
        {
            throw std::runtime_error("P07 coordinated centimetre restatement retains the same world surface");
        }
        const auto inch = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P11/input.step", step_import::Options::PlanarFixture());
        if (!inch.model || std::abs(inch.model->Info().areaMm2 - 14193.52) > 1e-6 || !inch.model->MaterialVolumeMm3() ||
            std::abs(*inch.model->MaterialVolumeMm3() - 98322.384) > 1e-5)
        {
            throw std::runtime_error("P11 explicit 127/5 conversion must deliver the independent U1 solid");
        }
        if (!scaled.report.units || scaled.report.units->lengthToMm != step_import::Fraction(10) || !inch.report.units ||
            inch.report.units->lengthToMm != step_import::Fraction(127, 5) || inch.report.units->definitions.size() != 1)
        {
            throw std::runtime_error("Owning unit report retains exact source definitions");
        }
        for (const auto* outcome : { &scaled, &same, &inch })
        {
            for (const auto& tolerance : outcome->report.tolerances)
            {
                if (tolerance.valueMm != 1e-5)
                {
                    throw std::runtime_error("Working tolerances remain canonical mm after unit normalization");
                }
            }
            for (const auto& edge : outcome->report.edges)
            {
                if (edge.sourceStartCoefficient != step_import::Fraction() || edge.sourceEndCoefficient != step_import::Fraction(1))
                {
                    throw std::runtime_error("Source dimensionless t is not scaled with length");
                }
            }
        }
        const auto p = same.model->Face().EvaluateSurface(12, 8);
        if (std::abs(p[0] - 3.8) > 1e-7 || std::abs(p[1] - 12.4) > 1e-7 || std::abs(p[2] - 5) > 1e-7 ||
            inch.model->ClassifyMaterial({ 12.7, 25.4, 38.1 }) != step_import::MaterialPointClass::Inside)
        {
            throw std::runtime_error("Canonical surface UV and solid queries preserve physical meaning");
        }
        const auto bytes = inch.model->Source()->Source().Bytes();
        for (std::size_t limit = 0; limit < inch.report.nativeCalls; ++limit)
        {
            auto options = step_import::Options::PlanarFixture();
            options.nativeLimits.nativeCalls = limit;
            const auto failed = step_import::ImportBytes(bytes, options);
            if (failed.status != step_import::Status::ResourceLimit || failed.model)
            {
                throw std::runtime_error("Converted geometry call boundaries never publish partial models");
            }
        }
        std::cout << "SI unit restatement preserves canonical geometry\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
