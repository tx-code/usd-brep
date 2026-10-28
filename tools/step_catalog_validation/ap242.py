# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
"""Compare AP242 catalog transport with independently extracted fixed-schema facts.

No STEP parsing, source-rule or native-import qualification is performed here.
The fixed reference was extracted from MBx-IF mim_lf.exp v1.101, not this catalog.
"""
from copy import deepcopy
import argparse
import json
from pathlib import Path
import subprocess

REFERENCE = Path(__file__).with_name('contracts') / 'ap242e2-source-reference-v1.json'
SCHEMA_SHA = 'ff51beb1be6e78faebd52338da342fd1059da2840beaa7cfd08b4df8e7c5a410'


def require(condition, message):
    if not condition:
        raise ValueError(message)


def compare(document, reference):
    require(reference['format'] == 'ap242e2-catalog-source-reference.v1'
            and reference['source']['sha256'] == SCHEMA_SHA, 'Invalid fixed source reference')
    require(document['format'] == 'step-catalog-snapshot.v3' and len(document['profiles']) == 1,
            'Expected one independent AP242 catalog')
    actual = document['profiles'][0]
    require(actual['kind'] == 'Ap242e2Source' and actual['schema_sha256'] == SCHEMA_SHA,
            'Catalog identity is not the fixed AP242 source')
    expected = {row['name']: row for row in reference['declarations']}
    declarations = {row['name']: row for row in actual['declarations']}
    require(len(expected) == 166 and sum(len(row['fields']) for row in expected.values()) == 217,
            'Incomplete independent declaration reference')
    require(len(actual['declarations']) == len(declarations) and declarations.keys() == expected.keys(),
            'Missing or extra AP242 declarations')
    require(actual['registered_types'] == list(declarations)
            and [row['id'] for row in actual['declarations']] == list(range(len(declarations))),
            'Catalog identity order is incomplete')
    types = reference['types']

    def codec(name, seen=()):
        require(name not in seen, 'Cyclic source alias')
        if name in expected:
            return 'Reference'
        special = {'MEASURE_VALUE': 'Measure', 'SIZE_SELECT': 'SizeSelect',
                   'MARKER_SELECT': 'MarkerSelect', 'TRIMMING_SELECT': 'TrimmingSelect'}
        if name in special:
            return special[name]
        primitive = {'STRING': 'String', 'REAL': 'Real', 'INTEGER': 'Integer',
                     'NUMBER': 'Number', 'BOOLEAN': 'Boolean', 'LOGICAL': 'Logical'}
        if name in primitive:
            return primitive[name]
        entry = types[name]
        if entry['kind'] == 'ENUMERATION OF':
            return 'Enumeration'
        if entry['kind'] == 'SELECT':
            return 'Reference'
        return codec(entry['base'], (*seen, name))

    def field(observed, source):
        for key in ('name', 'type', 'optional'):
            require(observed[key] == source[key], f"Field {source['name']}: {key}")
        dimensions = source['dimensions']
        if dimensions:
            outer = dimensions[0]
            unique = 'Set' if outer['aggregate'] == 'SET' else 'List' if outer['unique'] else 'None'
            require(observed['kind'] == 'Aggregate' and observed['minimum'] == outer['minimum']
                    and observed['maximum'] == outer['maximum'] and observed['unique'] == unique,
                    f"Field {source['name']}: outer aggregate")
            require(observed['element_kind'] == codec(source['type']), f"Field {source['name']}: element codec")
            require(observed['nested_aggregates'] == dimensions[1:], f"Field {source['name']}: nested aggregates")
        else:
            require(observed['kind'] == codec(source['type']) and not observed['nested_aggregates'],
                    f"Field {source['name']}: scalar codec")

    for name, source in expected.items():
        observed = declarations[name]
        require(observed['parents'] == source['parents'], f'{name}: source parents')
        require(len(observed['fields']) == len(source['fields']), f'{name}: source local field count')
        for af, ef in zip(observed['fields'], source['fields']):
            field(af, ef)

    # Explicit redeclarations must retain the declaring slot. Check the type,
    # codec and inherited aggregate bounds independently of the compiler's
    # inclusion proof; a copied field at a new parameter would fail here.
    explicit = {(e['name'], f['declared_in'], f['field']): f
                for e in expected.values() for f in e['explicit_redeclarations']}
    observed = {(f['record_type'], f['declared_in'], f['field']): f for f in actual['explicit_redeclarations']}
    require(len(observed) == len(actual['explicit_redeclarations']) and observed.keys() == explicit.keys(),
            'Explicit redeclaration inventory')
    for key, source in explicit.items():
        entry = observed[key]
        expected_kind = 'Aggregate' if source['dimensions'] else codec(source['type'])
        require(entry['type'] == source['type'] and entry['kind'] == expected_kind,
                'Explicit redeclaration type/codec')
        owner, origin, slot = key
        require(entry['record_type_id'] == declarations[owner]['id']
                and entry['declaring_type_id'] == declarations[origin]['id']
                and declarations[origin]['fields'][entry['local_field_index']]['name'] == slot,
                'Explicit redeclaration lost original slot identity')
    derived = {(e['name'], f['declared_in'], f['field']) for e in expected.values() for f in e['derived_overrides']}
    require(len(actual['derived_slots']) == len(derived)
            and {(f['record_type'], f['declared_in'], f['field']) for f in actual['derived_slots']} == derived,
            'DERIVE inventory differs; explicit CBU and derived SI must stay distinct')

    def ancestors(name):
        return {name} | {a for p in expected[name]['parents'] for a in ancestors(p)}

    def alternatives(name, seen=()):
        require(name not in seen, 'Cyclic source SELECT')
        entry = types.get(name)
        if entry and entry['kind'] == 'SELECT':
            return {leaf for branch in entry['branches'] for leaf in alternatives(branch, (*seen, name))}
        return {name}

    used_types = {field['type'] for row in expected.values() for field in row['fields']}
    used_types.update(field['type'] for row in expected.values() for field in row['explicit_redeclarations'])
    selects = {name for name in used_types if name in types and types[name]['kind'] == 'SELECT'
               and codec(name) in ('Reference', 'MarkerSelect', 'TrimmingSelect')}
    enumerations = {name for name in used_types if name in types and types[name]['kind'] == 'ENUMERATION OF'}
    if 'MARKER_SELECT' in used_types:
        # The mixed marker codec also consumes its primitive enum arm.
        enumerations.add('MARKER_TYPE')
    require(len(actual['reference_selects']) == len(selects)
            and {entry['name'] for entry in actual['reference_selects']} == selects,
            'Reference SELECT inventory is incomplete')
    require(len(actual['enumerations']) == len(enumerations - {'SI_PREFIX'})
            and {entry['name'] for entry in actual['enumerations']} == enumerations - {'SI_PREFIX'},
            'Enumeration inventory is incomplete')
    require({entry['name'] for entry in actual['si_prefixes'] if entry['name']}
            == set(types['SI_PREFIX']['branches']), 'SI prefix name inventory')
    require(len(actual['measure_branches']) == len(set(actual['measure_branches']))
            and set(actual['measure_branches']) <= alternatives('MEASURE_VALUE'), 'Invalid admitted measure branch')
    for select in actual['reference_selects']:
        allowed = alternatives(select['name'])
        require(bool(select['branches']), 'Empty admitted SELECT')
        for branch in select['branches']:
            require(branch in expected and bool(ancestors(branch) & allowed),
                    f"SELECT {select['name']}: invalid admitted source branch")
    for enumeration in actual['enumerations']:
        source = types[enumeration['name']]
        require(source['kind'] == 'ENUMERATION OF' and enumeration['branches'] == source['branches'],
                f"Enumeration {enumeration['name']}: source alternatives")
    return actual


def controls(document, reference):
    # Every negative starts from the same complete passing native export.
    # The mutations target the source differences that an AP214 alias misses.
    for case in range(10):
        changed = deepcopy(document)
        profile = changed['profiles'][0]
        declarations = {row['name']: row for row in profile['declarations']}
        if case == 0:
            declarations['STYLED_ITEM']['fields'][0]['minimum'] = 1
        elif case == 1:
            declarations['CURVE_STYLE']['fields'][1]['optional'] = False
        elif case == 2:
            profile['derived_slots'].append({'record_type': 'CONVERSION_BASED_UNIT',
                                             'declared_in': 'NAMED_UNIT', 'field': 'dimensions'})
        elif case == 3:
            profile['explicit_redeclarations'].pop()
        elif case == 4:
            declarations['PRODUCT_DEFINITION_RELATIONSHIP']['fields'][3]['type'] = 'PRODUCT_DEFINITION'
        elif case == 5:
            declarations['B_SPLINE_SURFACE']['fields'][2]['nested_aggregates'][0]['minimum'] = 1
        elif case == 6:
            profile['reference_selects'][-1]['branches'] = ['COLOUR']
        elif case == 7:
            profile['reference_selects'].pop()
        elif case == 8:
            profile['enumerations'].pop()
        else:
            profile['measure_branches'].append('UNREGISTERED_MEASURE')
        try:
            compare(changed, reference)
        except ValueError:
            continue
        raise ValueError(f'Independent comparison admitted mutation {case}')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = subprocess.run([str(args.snapshot.resolve()), '--ap242-catalog'], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        document = json.loads(result.stdout)
        reference = json.loads(REFERENCE.read_text(encoding='utf-8'))
        compare(document, reference)
        controls(document, reference)
        print('AP242 fixed-source catalog: 166 entities / 217 local fields; 10 negative comparisons passed')
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f'AP242 catalog validation: {error}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
