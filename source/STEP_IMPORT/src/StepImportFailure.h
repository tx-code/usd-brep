// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepImport.h"

namespace step_import::detail
{
// Target failure facts are shared by result propagation and the remaining
// legacy adapters. They do not depend on a native construction plan.
enum class NativeFailureKind
{
    Unclassified,
    FaceLoopOrientation,
    // An operation refusal formerly left the strict attempt immediately via
    // exception, before its failure evidence was finalized. Keep that private
    // observation boundary when the refusal is returned as a value instead.
    ConstructionOperation
};
struct Failure
{
    Status status;
    std::string key, message;
    std::optional<std::size_t> record;
    std::string stage = "D5";
    NativeFailureKind nativeKind = NativeFailureKind::Unclassified;
};
void AddFailureDiagnostic(Report& report, const Failure& failure);
} // namespace step_import::detail
