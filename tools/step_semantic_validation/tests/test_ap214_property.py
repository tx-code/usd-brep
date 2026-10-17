# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Property attachment preserves source values and proves selected-part ownership."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.step_semantic_validation.tests.test_ap214 import AP203, AP214, CORPUS, ap214_source
from tools.step_semantic_validation.tests.test_ap214_entities import append_records
from tools.step_semantic_validation.tests.test_ap214_units import unit_source


def property_source():
    data = ap214_source((CORPUS / "S203-P01/input.step").read_bytes())
    return append_records(data, b"""#900=PROPERTY_DEFINITION('material property',$,#7);
#901=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);
#902=REPRESENTATION('material name',(#903),#904);
#903=DESCRIPTIVE_REPRESENTATION_ITEM('source alloy','unchanged alloy text');
#904=REPRESENTATION_CONTEXT('property context','source metadata');
""")


def quantity_property_source():
    return append_records(property_source(), b"""#910=PROPERTY_DEFINITION('source quantity','raw length',#8);
#911=PROPERTY_DEFINITION_REPRESENTATION(#910,#912);
#912=REPRESENTATION('source quantity',(#913),#914);
#913=MEASURE_REPRESENTATION_ITEM('source length',LENGTH_MEASURE(+0.025400E+3),#915);
#914=REPRESENTATION_CONTEXT('quantity context','original metadata');
#915=(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.CENTI.,.METRE.));
""")


class Ap214PropertyTests(unittest.TestCase):
    def invoke(self, data, *options):
        with tempfile.TemporaryDirectory(prefix="step-ap214-property-") as directory:
            path = Path(directory) / "source.step"
            path.write_bytes(data)
            run = subprocess.run([os.environ["STEP_SEMANTIC_BINARY"], "--fixture-limits", str(path),
                                  *map(str, options)], capture_output=True, timeout=20)
        return run, json.loads(run.stdout) if run.stdout else None

    def checked(self, data):
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        return result

    def test_descriptive_property_keeps_attachment_to_selected_part(self):
        data = property_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode, run.stderr or result.get("diagnostics"))
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertEqual([dict(link="901", property_definition="900", characterized_definition="7",
                               product_definition="7", representation="902", context="904")],
                         result["snapshot"]["part"]["property_attachments"])
        self.assertEqual("12", result["snapshot"]["part"]["roles"]["unit_context"])

    def test_quantity_keeps_its_explicit_unit_context_and_shape_role(self):
        result = self.checked(quantity_property_source())
        part = result["snapshot"]["part"]
        self.assertEqual(dict(link="911", property_definition="910", characterized_definition="8",
                              product_definition="7", representation="912", context="914"),
                         part["property_attachments"][1])
        self.assertEqual("OpenSurfaceModel", part["source_shape_role"])
        self.assertFalse(part["shape_geometry_verified"])
        self.assertEqual(("20", "21", "12"),
                         tuple(part["roles"][k] for k in ("representation", "shape", "unit_context")))
        units = result["snapshot"]["units"]
        self.assertEqual({"kind": "rational", "num": "1", "den": "1"}, units["length_scale_mm"])
        quantity_unit = next(u for u in units["si_definitions"] if u["source_id"] == "915")
        self.assertEqual(("CENTI", {"kind": "rational", "num": "10", "den": "1"}),
                         (quantity_unit["prefix"], quantity_unit["scale"]))
        self.assertIn("LENGTH_MEASURE(+0.025400E+3),#915", result["source"]["input"]["raw"])
        self.assertEqual(2, result["statistics"]["property_attachment_checks"])

    def test_repeated_links_names_and_shared_items_are_not_merged(self):
        data = append_records(property_source(), b"""#905=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);
#906=PROPERTY_DEFINITION('material property',$,#7);
#907=PROPERTY_DEFINITION_REPRESENTATION(#906,#902);
""")
        attachments = self.checked(data)["snapshot"]["part"]["property_attachments"]
        self.assertEqual([("901", "900"), ("905", "900"), ("907", "906")],
                         [(a["link"], a["property_definition"]) for a in attachments])
        self.assertEqual(["902"] * 3, [a["representation"] for a in attachments])

    def test_selected_shape_identity_and_inherited_shape_property_are_supported(self):
        result = self.checked(property_source().replace(b"'material property',$,#7", b"'material property',$,#8"))
        self.assertEqual("8", result["snapshot"]["part"]["property_attachments"][0]["characterized_definition"])
        data = property_source().replace(b"#900=PROPERTY_DEFINITION('material property',$,#7);\n", b"")
        data = data.replace(b"REPRESENTATION(#900,#902)", b"REPRESENTATION(#8,#902)")
        result = self.checked(data)
        attachment = result["snapshot"]["part"]["property_attachments"][0]
        fields = ("property_definition", "characterized_definition", "product_definition")
        self.assertEqual(("8", "7", "7"), tuple(attachment[k] for k in fields))

    def test_foreign_part_never_becomes_an_attachment_root(self):
        data = append_records(property_source(), b"#905=PRODUCT_DEFINITION('other',$,#5,#6);\n")
        data = data.replace(b"'material property',$,#7", b"'material property',$,#905")
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedSinglePartGraph", result["diagnostics"][0]["key"])
        self.assertIsNone(result["snapshot"])
        self.assertEqual(0, result["statistics"]["property_attachment_checks"])

    def test_geometry_or_styles_cannot_be_smuggled_into_property_items(self):
        for target in (b"#100", b"#133", b"#21"):
            with self.subTest(target=target):
                data = property_source().replace(b"'material name',(#903)", b"'material name',(" + target + b")")
                run, result = self.invoke(data)
                self.assertEqual(2, run.returncode)
                error = result["diagnostics"][0]
                self.assertEqual(("UnsupportedPropertyItem", "items", [2, 1]),
                                 (error["key"], error["field"], error["parameter_path"]))
                self.assertEqual(target, data[slice(*error["span"])])
                self.assertIsNone(result["snapshot"])
        data = property_source().replace(b"REPRESENTATION(#900,#902)", b"REPRESENTATION(#900,#20)")
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedPropertyRepresentation", result["diagnostics"][0]["key"])

    def test_detached_properties_and_representations_are_not_auto_owned(self):
        original = property_source()
        for data in (
            original.replace(b"#901=PROPERTY_DEFINITION_REPRESENTATION(#900,#902);\n", b""),
            append_records(original, b"#905=PROPERTY_DEFINITION('same part',$,#7);\n"),
            append_records(original, b"#905=REPRESENTATION('unlinked',(#903),#904);\n"),
        ):
            run, result = self.invoke(data)
            self.assertEqual(2, run.returncode)
            self.assertEqual("UnownedOrOrphanProfileContent", result["diagnostics"][0]["key"])
            self.assertIsNone(result["snapshot"])

    def test_transport_fields_and_derived_names_are_not_invented(self):
        for old, new, key in (
            (b"REPRESENTATION(#900,#902)", b"REPRESENTATION('invented',#900,#902)", "AttributeCountMismatch"),
            (b"'material property',$,#7", b"'material property',$,#7,'invented id'", "AttributeCountMismatch"),
            (b"'source alloy','unchanged alloy text'", b"'source alloy',$", "RequiredAttributeMissing"),
            (b"'material property',$,#7", b"'material property',$,#100", "ReferenceTypeMismatch"),
            (b"(#903),#904", b"(#903),#900", "ReferenceTypeMismatch"),
        ):
            with self.subTest(key=key):
                run, result = self.invoke(property_source().replace(old, new))
                self.assertEqual(1, run.returncode)
                self.assertEqual(key, result["diagnostics"][0]["key"])
                self.assertIsNone(result["snapshot"])
        self.checked(property_source().replace(b"'material property',$,#7", b"'material property','',#7"))
        run, result = self.invoke(property_source().replace(AP214, AP203))
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedEntityMapping", result["diagnostics"][0]["key"])

    def test_bad_quantity_stops_before_attachment_and_preserves_source_unit_conflict(self):
        data = append_records(unit_source(), b"""#950=PROPERTY_DEFINITION('density property',$,#7);
#951=PROPERTY_DEFINITION_REPRESENTATION(#950,#952);
#952=REPRESENTATION('density',(#953),#12);
#953=MEASURE_REPRESENTATION_ITEM('density',POSITIVE_RATIO_MEASURE(7850.),#903);
""")
        run, result = self.invoke(data)
        self.assertEqual(1, run.returncode)
        self.assertEqual("SourceRuleViolation:MEASURE_WITH_UNIT.WR1", result["diagnostics"][0]["key"])
        self.assertEqual("NotRun", result["checks"]["single_part_profile"])
        self.assertEqual(0, result["statistics"]["property_attachment_checks"])
        self.assertEqual(data.decode("ascii"), result["source"]["input"]["raw"])
        self.assertIsNone(result["snapshot"])

    def test_later_bad_link_and_resource_limits_publish_no_partial_attachments(self):
        data = append_records(property_source(), b"""#905=PROPERTY_DEFINITION_REPRESENTATION(#900,#906);
#906=REPRESENTATION('bad later',(#100),#904);
""")
        run, result = self.invoke(data)
        self.assertEqual(2, run.returncode)
        self.assertEqual("UnsupportedPropertyItem", result["diagnostics"][0]["key"])
        self.assertEqual(2, result["statistics"]["property_attachment_checks"])
        self.assertIsNone(result["snapshot"])
        data = quantity_property_source()
        run, result = self.invoke(data)
        self.assertEqual(0, run.returncode)
        visits = result["statistics"]["dependency_visits"]
        self.assertEqual(0, self.invoke(data, "--max-semantic-visits", visits)[0].returncode)
        stopped, limited = self.invoke(data, "--max-semantic-visits", visits - 1)
        self.assertEqual(3, stopped.returncode)
        self.assertIsNone(limited["snapshot"])
        size = len(run.stdout)
        for _ in range(4):
            run, _ = self.invoke(data, "--max-report-bytes", size)
            self.assertEqual(0, run.returncode)
            size = len(run.stdout)
        stopped, limited = self.invoke(data, "--max-report-bytes", size - 1)
        self.assertEqual(5, stopped.returncode)
        self.assertEqual(b"", stopped.stdout)
        self.assertIsNone(limited)


if __name__ == "__main__":
    unittest.main()
