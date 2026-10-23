// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "StepAssemblyInputs.h"
#include "StepImport.h"

namespace step_import
{
namespace detail
{
struct AssemblySnapshotData;
struct AssemblyAccess;
} // namespace detail
struct AssemblyLimits
{
    std::size_t occurrences = 0, instances = 0, depth = 0; // Explicit request budgets; roots count as instances.
};
struct AssemblyInstanceInfo
{
    std::size_t definitionIndex, rootIndex, depth;
    std::optional<std::size_t> parentIndex;
    std::optional<std::string> sourceUsageId;
    std::array<Point, 3> rotationToRoot;
    Point translationToRootMm;
};
class AssemblySnapshot
{
public:

    std::size_t DefinitionCount() const;
    std::string SourceDefinitionId(std::size_t definition) const;
    std::string DefinitionRole(std::size_t definition) const;
    std::size_t BodyCount(std::size_t definition) const;
    // Owning read-only definition geometry in local mm, never transformed in place.
    std::shared_ptr<const ModelSnapshot> Body(std::size_t definition, std::size_t body) const;
    std::size_t InstanceCount() const;
    AssemblyInstanceInfo Instance(std::size_t index) const;
    std::shared_ptr<const step_semantics::AssemblyDocument> Source() const;

private:

    friend struct detail::AssemblyAccess;
    explicit AssemblySnapshot(std::shared_ptr<const detail::AssemblySnapshotData> data);
    std::shared_ptr<const detail::AssemblySnapshotData> m_data;
};
struct AssemblyOutcome
{
    Status status = Status::SourceFailure;
    std::shared_ptr<const AssemblySnapshot> assembly;
    Report report;
};
// Explicit general development path. All declared definitions/roots participate;
// no repair/selection isolation. Source closure, native checks and mandatory
// report must all succeed before the same cancellation/commit gate as ImportBytes.
AssemblyOutcome ImportAssemblyBytes(std::string bytes, const Options& options, AssemblyLimits limits);
AssemblyOutcome ImportAssemblyFile(const std::filesystem::path& path, const Options& options, AssemblyLimits limits);
} // namespace step_import
