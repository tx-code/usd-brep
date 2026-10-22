# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Administrative header text is retained only under a separate explicit policy."""
import unittest
from tools.step_inspect_validation.tests import test_opaque_header
from tools.step_inspect_validation.tests.test_inspect import document

FLAG = '--preserve-opaque-file-name-text'
HEADER = b"FILE_NAME('part-\x80','time-\x81',('known','author-\x82'),('org-\x83'),'pre-\x84','sys-\x85','auth-\x86');"


def header_text_document():
    data = document("#1=A('standard');\n")
    start = data.index(b'FILE_NAME(')
    end = data.index(b';', start)+1
    return data[:start]+HEADER+data[end:]


class FileNameTextTests(unittest.TestCase):
    invoke = test_opaque_header.OpaqueHeaderTests.invoke

    def test_all_fields_keep_original_bytes_and_precise_diagnostic_paths(self):
        source = header_text_document()
        run, report = self.invoke(source, FLAG)
        self.assertEqual(0, run.returncode, run.stderr or report)
        self.assertEqual('PreserveFileNameText', report['header_name_policy'])
        self.assertEqual(source, report['input']['raw'].encode('latin1'))
        self.assertEqual({(1,), (2,), (3, 2), (4, 1), (5,), (6,), (7,)},
                         {tuple(d['parameter_path']) for d in report['diagnostics']})
        for d in report['diagnostics']:
            self.assertEqual('FILE_NAME', d['component'])
            value = report['header'][1]['parameters'][d['parameter_path'][0]-1]
            if len(d['parameter_path']) == 2:
                value = value['children'][d['parameter_path'][1]-1]
            self.assertEqual('OpaqueString', value['kind'])
            self.assertIsNone(value['text'])
            self.assertNotIn('DATA', value['notice'])
            self.assertEqual(source[value['span'][0]:value['span'][1]], value['raw'].encode('latin1'))

    def test_defaults_old_option_and_conflicts_remain_strict(self):
        source = header_text_document()
        for flags in [(), ('--preserve-opaque-header-name',), (FLAG, '--preserve-opaque-header-name'),
                      ('--preserve-opaque-header-name', FLAG)]:
            run, report = self.invoke(source, *flags)
            self.assertNotEqual(0, run.returncode)
            if len(flags) == 2:
                self.assertEqual(64, run.returncode)
                self.assertIsNone(report)
            if report:
                self.assertIsNone(report['records'])

    def test_schema_description_data_comments_and_controls_are_not_exempt(self):
        original = document("#1=A('standard');\n")
        for source in [original.replace(b"'TEST'", b"'\x80'"),
                       original.replace(b"'independent test'", b"'\x80'"),
                       original.replace(b"'2;1'", b"'\x80'"),
                       original.replace(b"'standard'", b"'\x80'"),
                       original.replace(b'HEADER;', b'HEADER;/*\x80*/'),
                       header_text_document().replace(b'author-\x82', b'author-\x00')]:
            run, report = self.invoke(source, FLAG)
            self.assertNotEqual(0, run.returncode)
            self.assertIsNone(report['records'])

    def test_shape_and_resource_limits_still_apply(self):
        for source, flags in [(header_text_document().replace(b"('org-\x83')", b"(('org-\x83'))"), ()),
                              (header_text_document().replace(b"'sys-\x85'", b'42'), ()),
                              (header_text_document().replace(b'author-\x82', b'author-\x82'+b'x'*64),
                               ('--max-string-bytes', '32'))]:
            run, report = self.invoke(source, FLAG, *flags)
            self.assertNotEqual(0, run.returncode)
            if report:
                self.assertIsNone(report['records'])
                if flags:
                    self.assertEqual('StringByteLimit', report['diagnostics'][0]['key'])
