// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "StepImportResult.h"
#include "StepImportSurfaceCurveInterpretation.h"

#include <map>
#include <string_view>
#include <tuple>

namespace step_import::detail
{
// Options errors use the established invalid_argument caller contract. The
// request is permitted with terminal-domain interpretation, but not repairs,
// selection/isolation or explicit topology/support-domain normalization.
void ValidateSurfaceCurve3dInterpretationOptions(const Options& options);
bool IsSurfaceCurve3dAuxiliaryConflict(const Failure& failure);
class SurfaceCurve3dInterpretationSession
{
public:

    // Deep-copy request and budgets before any progress callback can mutate
    // caller-owned data. No file reads, native geometry or publication here.
    explicit SurfaceCurve3dInterpretationSession(const Options& options);
    Result<void> BindSource(const step_source::SourceSnapshot& source);
    Result<std::optional<std::size_t>> FindUse(std::string_view definition, std::string_view shape, std::string_view edge) const;
    const SurfaceCurve3dInterpretationUse& Plan(std::size_t index) const;
    Result<std::size_t> RecordAction(SurfaceCurve3dInterpretationAction action);
    Result<void> RecordConflict(std::size_t action, SurfaceCurve3dInterpretationConflict conflict);
    Result<void> MarkNativeChecked(std::size_t action);
    Result<void> Complete();
    std::shared_ptr<const SurfaceCurve3dInterpretationReceipt> Evidence() const;

private:

    std::shared_ptr<SurfaceCurve3dInterpretationReceipt> m_receipt;
    std::map<std::tuple<std::string, std::string, std::string>, std::size_t> m_uses;
    std::vector<bool> m_recorded;
    bool m_bound = false;
};
} // namespace step_import::detail
