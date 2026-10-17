// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#include "ProbeCommon.h"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <Bnd_Box.hxx>
#include <DESTEP_Parameters.hxx>
#include <GProp_GProps.hxx>
#include <Interface_Check.hxx>
#include <Interface_CheckIterator.hxx>
#include <NCollection_IndexedMap.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <StepData_StepModel.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <XSControl_TransferReader.hxx>
#include <XSControl_WorkSession.hxx>

namespace
{
int Count(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> items;
    TopExp::MapShapes(shape, type, items);
    return items.Extent();
}
std::pair<int, int> Checks(Interface_CheckIterator checks)
{
    int failures = 0, warnings = 0;
    for (checks.Start(); checks.More(); checks.Next())
    {
        failures += checks.Value()->NbFails();
        warnings += checks.Value()->NbWarnings();
    }
    return { failures, warnings };
}
bench::Trial Run(const bench::Args& args)
{
    if (args.mode != "read" && args.mode != "transfer")
    {
        throw std::invalid_argument("OCCT mode must be read or transfer");
    }
    bench::Trial row;
    auto cleanup = bench::Clock::now();
    const auto begin = cleanup;
    {
        auto start = bench::Clock::now();
        STEPControl_Reader reader;
        DESTEP_Parameters parameters;
        parameters.InitFromStatic();
        row.timings["reader_initialization"] = bench::Ms(start);
        start = bench::Clock::now();
        const auto status = reader.ReadFile(bench::Utf8(args.input).c_str(), parameters);
        row.timings["read_file"] = bench::Ms(start);
        row.records = reader.StepModel().IsNull() ? 0 : static_cast<std::size_t>(reader.StepModel()->NbEntities());
        row.status = status == IFSelect_RetDone ? "ReadDone" : "ReadFailure";
        start = bench::Clock::now();
        const auto loadChecks = Checks(reader.WS()->ModelCheckList(false));
        row.timings["load_diagnostics"] = bench::Ms(start);
        if (status == IFSelect_RetDone && loadChecks.first)
        {
            row.status = "ReadWithFailures";
        }
        row.extra = ",\"read_return\":" + std::to_string(static_cast<int>(status));
        row.extra += ",\"read_load_failures\":" + std::to_string(loadChecks.first) + ",\"read_load_warnings\":" + std::to_string(loadChecks.second);
        row.extra +=
            ",\"settings\":{\"policy\":\"OCCT default FixShape processing; not strict-import equivalence\","
            "\"system_length_unit_mm\":1,\"precision_mode\":" +
            std::to_string(static_cast<int>(parameters.ReadPrecisionMode)) + ",\"precision_value\":" + bench::N(parameters.ReadPrecisionVal) +
            ",\"maximum_precision_mode\":" + std::to_string(static_cast<int>(parameters.ReadMaxPrecisionMode)) +
            ",\"maximum_precision_value\":" + bench::N(parameters.ReadMaxPrecisionVal) +
            ",\"surface_curve_mode\":" + std::to_string(static_cast<int>(parameters.ReadSurfaceCurveMode)) +
            ",\"same_parameter\":" + (parameters.ReadSameParamMode ? "true" : "false") +
            ",\"assembly_level\":" + std::to_string(static_cast<int>(parameters.ReadAssemblyLevel)) +
            ",\"tessellated_mode\":" + std::to_string(static_cast<int>(parameters.ReadTessellated)) + "}";
        if (status == IFSelect_RetDone && args.mode == "transfer")
        {
            reader.SetSystemLengthUnit(1.0);
            start = bench::Clock::now();
            const auto roots = reader.NbRootsForTransfer();
            const auto transferred = reader.TransferRoots();
            const auto shape = reader.OneShape();
            row.timings["transfer_roots"] = bench::Ms(start);
            row.hasModel = !shape.IsNull();
            row.status = row.hasModel ? (transferred == roots && roots > 0 ? "Transferred" : "PartialTransfer") : "NoShape";
            row.extra += ",\"candidate_roots\":" + std::to_string(roots) + ",\"transferred_roots\":" + std::to_string(transferred) +
                         ",\"result_shapes\":" + std::to_string(reader.NbShapes());
            // Freeze read/translation time before optional geometric validation and measurements.
            row.operationMs = row.timings.at("reader_initialization") + row.timings.at("read_file") + row.timings.at("transfer_roots");
            start = bench::Clock::now();
            const auto transferChecks = Checks(reader.WS()->TransferReader()->LastCheckList());
            row.timings["transfer_diagnostics"] = bench::Ms(start);
            row.extra += ",\"transfer_failures\":" + std::to_string(transferChecks.first) +
                         ",\"transfer_warnings\":" + std::to_string(transferChecks.second);
            if (row.status == "Transferred" && (loadChecks.first || transferChecks.first))
            {
                row.status = "TransferredWithFailures";
            }
            if (row.hasModel)
            {
                start = bench::Clock::now();
                const bool valid = BRepCheck_Analyzer(shape, true, false, false).IsValid();
                if (!valid && row.status == "Transferred")
                {
                    row.status = "TransferredBRepInvalid";
                }
                row.timings["brep_check"] = bench::Ms(start);
                start = bench::Clock::now();
                const auto solids = Count(shape, TopAbs_SOLID);
                GProp_GProps area, volume;
                BRepGProp::SurfaceProperties(shape, area);
                if (solids)
                {
                    BRepGProp::VolumeProperties(shape, volume);
                }
                Bnd_Box box;
                BRepBndLib::AddOptimal(shape, box, false, false);
                std::string bounds = "null";
                if (!box.IsVoid() && !box.IsOpen())
                {
                    double xmin, ymin, zmin, xmax, ymax, zmax;
                    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
                    bounds = "[[" + bench::N(xmin) + "," + bench::N(ymin) + "," + bench::N(zmin) + "],[" + bench::N(xmax) + "," + bench::N(ymax) +
                             "," + bench::N(zmax) + "]]";
                }
                std::ostringstream model;
                model << "{\"faces\":" << Count(shape, TopAbs_FACE) << ",\"edges\":" << Count(shape, TopAbs_EDGE)
                      << ",\"vertices\":" << Count(shape, TopAbs_VERTEX) << ",\"solids\":" << solids << ",\"area_mm2\":" << bench::N(area.Mass())
                      << ",\"volume_mm3\":" << (solids ? bench::N(volume.Mass()) : "null") << ",\"brep_check_valid\":" << (valid ? "true" : "false")
                      << ",\"bounds_mm\":" << bounds << '}';
                row.model = model.str();
                row.timings["measurements"] = bench::Ms(start);
            }
        }
        else
        {
            row.operationMs = row.timings.at("reader_initialization") + row.timings.at("read_file");
        }
        row.timings["wrapper_until_cleanup"] = bench::Ms(begin);
        row.aliveMemory = bench::Memory();
        cleanup = bench::Clock::now();
    }
    row.cleanupMs = bench::Ms(cleanup);
    return row;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try
    {
        return bench::Main(
            bench::Tokens(argc, argv),
            "occt",
            "{\"version\":" + bench::Q(OCC_VERSION_STRING_EXT) + ",\"TKernel\":" + bench::Q(bench::Loaded(L"TKernel.dll")) +
                ",\"TKDESTEP\":" + bench::Q(bench::Loaded(L"TKDESTEP.dll")) + "}",
            Run
        );
    }
    catch (const Standard_Failure& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
