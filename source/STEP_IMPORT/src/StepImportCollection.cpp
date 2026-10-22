// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "StepImportExecution.h"
#include "StepImportNativeData.h"

#include <SmBrepData.h>
#include <algorithm>
#include <stdexcept>

namespace step_import::detail
{
std::vector<std::shared_ptr<ModelData>> BuildMaterialCollection(
    std::shared_ptr<const step_semantics::Snapshot> source,
    const Options& options,
    Report& report,
    Execution& execution
)
{
    const auto& part = source->PartDefinition();
    report.selectedBodyCount = part.shapes.size();
    if (part.shapes.empty() || std::any_of(
                                   part.shapes.begin(),
                                   part.shapes.end(),
                                   [](const auto& shape)
                                   {
                                       return shape.role != "MaterialSolid";
                                   }
                               ))
    {
        throw Failure{ Status::Unsupported,
                       "MaterialCollectionRequiresSolids",
                       "Every collection member must retain a source material-solid role",
                       {},
                       "GeneralBrep" };
    }
    // Associated geometry belongs to the part, not an arbitrary first body.
    // Until a collection-level owner/export is qualified, refuse the complete
    // mixed request before constructing bodies rather than duplicating/dropping it.
    for (const auto& role : part.roles)
    {
        if (role.name == "wireframe_representation" || role.name == "construction_representation")
        {
            throw Failure{ Status::Unsupported,
                           "MaterialCollectionAssociatedGeometryUnimplemented",
                           "Collection-level associated geometry ownership is not implemented",
                           role.recordIndex,
                           "GeneralBrep" };
        }
    }
    std::vector<std::shared_ptr<ModelData>> candidates;
    candidates.reserve(part.shapes.size());
    report.bodies.reserve(part.shapes.size());
    try
    {
        for (const auto& binding : part.shapes)
        {
            auto checks = std::make_shared<Report>();
            checks->generalBrep = true;
            checks->encoding = report.encoding;
            checks->sourceEvidenceScope = report.sourceEvidenceScope;
            checks->source = report.source;
            checks->sourceSha256 = report.sourceSha256;
            checks->sourceSchema = report.sourceSchema;
            checks->numericalIntegration = report.numericalIntegration;
            checks->periodicNormalizationRequested = report.periodicNormalizationRequested;
            checks->supportDomainRestrictionRequested = report.supportDomainRestrictionRequested;
            report.bodies.push_back({ binding.shape, binding.shell, false, checks });
            // One immutable source and one Execution ledger for all bodies.
            // A child report never resets the native-call/cancellation budget.
            candidates.push_back(BuildGeneralBody(source, binding, options, *checks, execution));
            report.bodies.back().checksPassed = true;
        }
        if (report.numericalIntegration)
        {
            report.numericalIntegration->completed = std::all_of(
                report.bodies.begin(),
                report.bodies.end(),
                [](const auto& body)
                {
                    return body.checks->numericalIntegration && body.checks->numericalIntegration->completed;
                }
            );
            if (report.numericalIntegration->completed)
            {
                report.numericalIntegration->appliedRelativeAccuracy = report.bodies.front().checks->numericalIntegration->appliedRelativeAccuracy;
            }
        }
        return candidates;
    }
    catch (...)
    {
        // Vector owners release every earlier native body before the failure
        // report is produced. Observations survive; candidate handles do not.
        report.candidateDiscarded = report.nativeCalls > 0 || !candidates.empty();
        for (auto& body : report.bodies)
        {
            if (body.checks && body.checks->nativeCalls)
            {
                body.checks->candidateDiscarded = true;
            }
        }
        throw;
    }
}
std::shared_ptr<const MaterialCollectionSnapshot> Access::Collection(
    std::shared_ptr<const step_semantics::Snapshot> source,
    std::vector<std::shared_ptr<ModelData>> bodies
)
{
    if (!source || bodies.empty() || bodies.size() != source->PartDefinition().shapes.size() ||
        std::any_of(
            bodies.begin(),
            bodies.end(),
            [&](const auto& body)
            {
                return !body || !body->brep || body->source != source;
            }
        ))
    {
        throw std::logic_error("IncompleteMaterialCollection");
    }
    auto data = std::make_shared<CollectionData>();
    data->source = std::move(source);
    data->bodies = std::move(bodies);
    return std::shared_ptr<const MaterialCollectionSnapshot>(new MaterialCollectionSnapshot(std::move(data)));
}
} // namespace step_import::detail

namespace step_import
{
MaterialCollectionSnapshot::MaterialCollectionSnapshot(std::shared_ptr<detail::CollectionData> data) : m_data(std::move(data))
{
}
std::size_t MaterialCollectionSnapshot::BodyCount() const
{
    return m_data->bodies.size();
}
std::shared_ptr<const ModelSnapshot> MaterialCollectionSnapshot::Body(std::size_t index) const
{
    return detail::Access::Snapshot(m_data->bodies.at(index));
}
std::string MaterialCollectionSnapshot::SourceShapeId(std::size_t index) const
{
    return m_data->source->Source().Records().at(m_data->source->PartDefinition().shapes.at(index).shape).id;
}
std::shared_ptr<const step_semantics::Snapshot> MaterialCollectionSnapshot::Source() const
{
    return m_data->source;
}
void MaterialCollectionSnapshot::WriteNative(const std::filesystem::path& path) const
{
    std::lock_guard lock(detail::NativeMutex());
    // Native transfer convention: ordered BRep array, empty CSG tree. Do not
    // infer union, deduplicate coincident bodies or transfer import qualification.
    // As on ModelSnapshot::WriteNative, serialization works on private copies
    // because the kernel writer canonicalizes UV storage in place.
    auto context = std::make_unique<SmContext>();
    std::vector<std::unique_ptr<SmBrep>> copies;
    SmTArray<SmBrep*> breps;
    SmTArray<SmCurve*> curves;
    SmTArray<SmSurface*> surfaces;
    SmTArray<long> trees;
    copies.reserve(m_data->bodies.size());
    for (const auto& body : m_data->bodies)
    {
        std::lock_guard bodyLock(body->mutex);
        copies.emplace_back(new (*context) SmBrep(*body->brep, nullptr, nullptr, TRUE, FALSE));
        if (!copies.back() || copies.back()->GetContext() != context.get())
        {
            throw std::runtime_error("CollectionArchiveContextMismatch");
        }
        breps.Add(copies.back().get());
    }
    if (SmBrepData::WritePartToFile(path.c_str(), curves, surfaces, trees, breps, SM_BINARY, TRUE, FALSE) != SM_SUCCESS)
    {
        throw std::runtime_error("CollectionArchiveWriteFailed");
    }
}
} // namespace step_import
