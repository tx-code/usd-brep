// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImport.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <thread>

namespace
{
void Require(bool value, const char* message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
void CancellationBoundaries(const std::string& bytes)
{
    using namespace step_import;
    std::vector<Progress> trace;
    auto observed = Options::PlanarFixture();
    observed.execution.progress = [&](Progress progress)
    {
        trace.push_back(progress);
    };
    const auto complete = ImportBytes(bytes, observed);
    Require(complete.model && complete.report.nativeCalls == 55 && trace.size() == 63, "All F1 safe boundaries are observed");
    for (const auto phase : { Phase::BeforeCapture,
                              Phase::Captured,
                              Phase::SemanticsChecked,
                              Phase::PlanarChecked,
                              Phase::NativeReady,
                              Phase::BeforeReport,
                              Phase::ReportReady,
                              Phase::BeforeCommit })
    {
        Require(
            std::any_of(
                trace.begin(),
                trace.end(),
                [&](Progress p)
                {
                    return p.phase == phase;
                }
            ),
            "Every source/publication phase is present"
        );
    }
    for (std::size_t stop = 0; stop < trace.size(); ++stop)
    {
        auto options = Options::PlanarFixture();
        options.execution.control = std::make_shared<ImportControl>();
        std::size_t index = 0;
        options.execution.progress = [&](Progress)
        {
            if (index++ == stop)
            {
                options.execution.control->RequestCancel();
            }
        };
        const auto result = ImportBytes(bytes, options);
        Require(result.status == Status::Cancelled && !result.model && !result.report.json.empty(), "Every cancellation prevents publication");
        Require(
            result.report.nativeCalls == trace[stop].nativeCalls && result.report.lastBoundary == trace[stop].phase,
            "Cancellation occurs at the requested boundary"
        );
        Require(
            result.report.diagnostics.back().source.key == "ImportCancelled" && result.report.diagnostics.back().source.stage == "Execution",
            "Cancellation is not a STEP defect"
        );
        if (stop > 0)
        {
            Require(result.report.captured && *result.report.captured == bytes, "Completed capture survives cancellation");
        }
        Require(options.execution.control->RequestCancel() == CancelResult::AlreadyCompleted, "Cancellation cleanup completed synchronously");
    }
    auto options = Options::PlanarFixture();
    options.execution.control = std::make_shared<ImportControl>();
    const auto result = ImportBytes(bytes, options);
    Require(result.model && options.execution.control->RequestCancel() == CancelResult::TooLate, "Late cancellation cannot destroy an accepted model");
    bool reused = false;
    try
    {
        (void)ImportBytes(bytes, options);
    }
    catch (const std::invalid_argument&)
    {
        reused = true;
    }
    Require(reused && result.model->Info().faces == 1, "A one-shot request cannot be silently reused");
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        auto race = Options::PlanarFixture();
        race.execution.control = std::make_shared<ImportControl>();
        std::binary_semaphore ready(0);
        std::optional<CancelResult> decision;
        race.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeCommit)
            {
                ready.release();
            }
        };
        std::jthread canceller(
            [&](std::stop_token stop)
            {
                while (!ready.try_acquire_for(std::chrono::milliseconds(5)))
                {
                    if (stop.stop_requested())
                    {
                        return;
                    }
                }
                decision = race.execution.control->RequestCancel();
            }
        );
        const auto raced = ImportBytes(bytes, race);
        Require(raced.status == Status::Accepted || raced.status == Status::Cancelled, "Race reaches a valid terminal outcome");
        canceller.join();
        Require(
            (raced.status == Status::Accepted && raced.model && decision == CancelResult::TooLate) ||
                (raced.status == Status::Cancelled && !raced.model && decision == CancelResult::Requested),
            "Atomic success/cancel arbitration has one winner"
        );
    }
}
void LimitsAndFaults(const std::string& bytes)
{
    using namespace step_import;
    for (const auto [limit, accepted] : { std::pair<std::size_t, bool>{ 2508, true }, { 2507, false } })
    {
        auto options = Options::PlanarFixture();
        options.sourceLimits.inputBytes = limit;
        const auto result = ImportBytes(bytes, options);
        Require(
            bool(result.model) == accepted && result.status == (accepted ? Status::Accepted : Status::ResourceLimit),
            "Frozen 2508-byte input N/N-1 bound"
        );
    }
    for (const auto [limit, accepted] : { std::pair<std::size_t, bool>{ 51, true }, { 50, false } })
    {
        auto options = Options::PlanarFixture();
        options.sourceLimits.records = limit;
        const auto result = ImportBytes(bytes, options);
        Require(
            bool(result.model) == accepted && result.status == (accepted ? Status::Accepted : Status::ResourceLimit),
            "Frozen 51-record input N/N-1 bound"
        );
    }
    for (const auto field : { &step_source::Limits::references,
                              &step_source::Limits::values,
                              &step_source::Limits::nesting,
                              &step_source::Limits::stringBytes,
                              &step_source::Limits::recordBytes,
                              &step_source::Limits::idDigits })
    {
        auto options = Options::PlanarFixture();
        options.sourceLimits.*field = 1;
        const auto result = ImportBytes(bytes, options);
        Require(result.status == Status::ResourceLimit && !result.model && result.report.nativeCalls == 0, "Declared source quotas block native work");
    }
    auto measured = Options::PlanarFixture();
    measured.execution.measureProcessMemory = true;
    const auto baseline = ImportBytes(bytes, measured);
    Require(
        baseline.model && baseline.report.maximumObservedPrivateBytes && *baseline.report.maximumObservedPrivateBytes > 0 &&
            baseline.report.processPeakCommitBytes,
        "Real process counters measured, not placeholders"
    );
    auto quota = Options::PlanarFixture();
    quota.nativeLimits.processPrivateBytes = 0;
    const auto memory = ImportBytes(bytes, quota);
    Require(
        memory.status == Status::ResourceLimit && !memory.model && memory.report.nativeCalls == 0 && memory.report.maximumObservedPrivateBytes &&
            memory.report.diagnostics.back().source.key == "ProcessMemoryLimit",
        "Cooperative process memory quota is enforced"
    );
    auto hard = Options::PlanarFixture();
    hard.nativeLimits.hardMemoryBytes = 1;
    const auto unsupported = ImportBytes(bytes, hard);
    Require(
        unsupported.status == Status::Unsupported && !unsupported.model &&
            unsupported.report.diagnostics.back().source.key == "UnsupportedHardMemoryContract",
        "Hard memory contracts are never silently relaxed"
    );
    auto deadline = Options::PlanarFixture();
    deadline.execution.deadline = std::chrono::steady_clock::now();
    const auto expired = ImportBytes(bytes, deadline);
    Require(
        expired.status == Status::ResourceLimit && !expired.model && expired.report.diagnostics.back().source.key == "DeadlineExceeded",
        "Deadline is distinct from user cancellation"
    );
    for (const bool allocation : { false, true })
    {
        auto fault = Options::PlanarFixture();
        fault.execution.control = std::make_shared<ImportControl>();
        fault.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::BeforeNativeCall && p.nativeCalls == 3)
            {
                if (allocation)
                {
                    throw std::bad_alloc();
                }
                fault.execution.control->RequestCancel();
                throw std::runtime_error("observer fault");
            }
        };
        bool unexpectedAllocation = false;
        try
        {
            const auto result = ImportBytes(bytes, fault);
            Require(
                !allocation && result.status == Status::BuildFailure && !result.model && result.report.candidateDiscarded &&
                    result.report.diagnostics.back().classification == "ExecutionFailure",
                "Actual fault is not hidden by a pending cancellation"
            );
        }
        catch (const std::bad_alloc&)
        {
            unexpectedAllocation = true;
        }
        Require(unexpectedAllocation == allocation, "Unexpected bad_alloc is not a configured ResourceLimit");
        Require(fault.execution.control->RequestCancel() == CancelResult::AlreadyCompleted, "Fault closes request lifetime");
    }
}
void CapturedSource(const std::string& bytes)
{
    using namespace step_import;
    const auto path = std::filesystem::temp_directory_path() /
                      ("step-capture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    struct RemoveFile
    {
        std::filesystem::path path;
        ~RemoveFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } cleanup{ path };
    for (const bool cancel : { false, true })
    {
        {
            std::ofstream stream(path, std::ios::binary);
            stream << bytes;
        }
        auto options = Options::PlanarFixture();
        options.execution.control = std::make_shared<ImportControl>();
        options.execution.progress = [&](Progress p)
        {
            if (p.phase == Phase::Captured)
            {
                {
                    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
                    stream << "changed file";
                }
                if (cancel)
                {
                    options.execution.control->RequestCancel();
                }
            }
        };
        const auto result = ImportFile(path, options);
        Require(
            result.status == (cancel ? Status::Cancelled : Status::Accepted) && bool(result.model) == !cancel,
            "Captured source is independent of later file writes"
        );
        Require(
            result.report.captured && *result.report.captured == bytes && result.report.source->Bytes() == bytes,
            "Original captured bytes remain available"
        );
    }
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "Pass frozen case directory");
        auto options = step_import::Options::PlanarFixture();
        options.execution.control = std::make_shared<step_import::ImportControl>();
        Require(options.execution.control->RequestCancel() == step_import::CancelResult::Requested, "Pre-cancel request accepted");
        const auto cancelled = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P01/input.step", options);
        Require(
            cancelled.status == step_import::Status::Cancelled && !cancelled.model && cancelled.report.nativeCalls == 0,
            "Pre-cancelled synchronous import does not publish a model"
        );
        Require(
            !cancelled.report.captured && !cancelled.report.source && !cancelled.report.json.empty(),
            "Capture was not entered; cancellation report completed"
        );
        Require(options.execution.control->RequestCancel() == step_import::CancelResult::AlreadyCompleted, "Cancellation is finalized after cleanup");
        const auto prior = step_import::ImportFile(std::filesystem::path(argv[1]) / "S203-P01/input.step", step_import::Options::PlanarFixture());
        Require(bool(prior.model), "Prior published model available");
        auto mutableOptions = step_import::Options::PlanarFixture();
        mutableOptions.execution.progress = [&](step_import::Progress progress)
        {
            if (progress.phase == step_import::Phase::Captured)
            {
                mutableOptions.nativeLimits.nativeCalls = 0;
            }
        };
        const auto frozen = step_import::ImportBytes(prior.report.source->Bytes(), mutableOptions);
        Require(frozen.status == step_import::Status::Accepted && frozen.model, "In-flight options are frozen before observer callbacks");
        const auto bytes = prior.report.source->Bytes();
        const auto before = prior.model->Closest({ 4.2, 9.6, 8 });
        CancellationBoundaries(bytes);
        LimitsAndFaults(bytes);
        CapturedSource(bytes);
        Require(
            prior.report.source->Bytes() == bytes && std::abs(prior.model->Closest({ 4.2, 9.6, 8 }).distanceMm - before.distanceMm) < 1e-7,
            "All later failures preserve the old published result and source"
        );
        std::cout << "Lifecycle contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
