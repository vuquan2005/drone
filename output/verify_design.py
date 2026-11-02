"""Independent acceptance checks against KiCad's exported netlist and geometry.

Run after kicad-cli netlist/ERC export: python verify_design.py
Uses only the Python standard library; does not alter the schematic or ERC rules.
"""
from pathlib import Path
import collections
import csv
import hashlib
import json
import math
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent
VALID = ROOT / 'validation'

def parse(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text)
    def read(i):
        tok = tokens[i]
        if tok == '(':
            out, i = [], i + 1
            while tokens[i] != ')':
                v, i = read(i)
                out.append(v)
            return out, i + 1
        return (json.loads(tok) if tok.startswith('"') else tok), i + 1
    return read(0)[0]

def kids(node, key):
    return [v for v in node if isinstance(v, list) and v and v[0] == key]

def one(node, key):
    return kids(node, key)[0]

sch = parse((ROOT / 'Drone_Direct_Wire.kicad_sch').read_text(encoding='utf-8'))
embedded = {s[1]: s for s in kids(one(sch, 'lib_symbols'), 'symbol')}
manifest = json.loads((VALID / 'manifest.json').read_text())
for file, digest in manifest['source_sha256'].items():
    assert hashlib.sha256((ROOT / file).read_bytes()).hexdigest() == digest, file
    for sym in kids(parse((ROOT / file).read_text(encoding='utf-8-sig')), 'symbol'):
        libid = Path(file).stem + ':' + sym[1]
        if libid in embedded:
            original = list(sym)
            original[1] = libid
            assert original == embedded[libid], f'Embedded symbol differs: {libid}'

all_instances = kids(sch, 'symbol')
instances = []
flags = {}
for inst in all_instances:
    props = {v[1]: v[2] for v in kids(inst, 'property')}
    if props['Reference'].startswith('#'):
        flags[props['Reference']] = inst
        assert one(inst, 'lib_id')[1] == 'ERC_Only:PWR_FLAG'
        assert one(inst, 'in_bom')[1] == 'no' and one(inst, 'on_board')[1] == 'no'
        assert props.get('Footprint', '') == ''
    else:
        instances.append(inst)
assert set(flags) == {'#FLG01', '#FLG02'}
flag_points = {r: tuple(map(float, one(inst, 'at')[1:3])) for r, inst in flags.items()}
byref, allpins, pinnames, bodyboxes = {}, {}, {}, {}
for inst in instances:
    props = {v[1]: v[2] for v in kids(inst, 'property')}
    ref, libid = props['Reference'], one(inst, 'lib_id')[1]
    assert ref not in byref
    byref[ref] = inst
    assert props.get('Footprint', '') == ''
    cx, cy, angle = map(float, one(inst, 'at')[1:])
    rad = math.radians(angle)
    def world(x, y):
        return (round(cx + x * math.cos(rad) - y * math.sin(rad), 4),
                round(cy - x * math.sin(rad) - y * math.cos(rad), 4))
    sym = embedded[libid]
    for unit in kids(sym, 'symbol'):
        for pin in kids(unit, 'pin'):
            number, name = one(pin, 'number')[1], one(pin, 'name')[1]
            x, y = map(float, one(pin, 'at')[1:3])
            allpins[f'{ref}.{number}'] = world(x, y)
            pinnames[f'{ref}.{number}'] = name
        for box in kids(unit, 'rectangle'):
            a, b = [world(*map(float, one(box, k)[1:])) for k in ['start', 'end']]
            bodyboxes[ref] = (min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1]))

expected_components = {
    'A1': 'DevKit_STM32:STM32F103VET6_DevKit',
    'U1': 'BMI270_DevKit:BMI270_DevKit_V1_3',
    'U2': 'SB_Nano_2G4RX_Physical:SB_Nano_2G4RX_Physical',
    **{f'U{i}': 'ESC_30A:ESC_30A_BLDC' for i in range(3, 7)},
    **{f'M{i}': 'A2212_Motor:A2212_10T_1400KV' for i in range(1, 5)},
    'BT1': 'OVONIC_LiPo:OVONIC_LiPo_3S_5200mAh_90C',
    'U7': 'LM2596_Module_Logical:LM2596_Module_Logical',
}
assert {r: one(v, 'lib_id')[1] for r, v in byref.items()} == expected_components
assert not any(kids(sch, k) for k in ['label', 'global_label', 'hierarchical_label', 'bus', 'sheet', 'image'])
assert not list(ROOT.glob('*.kicad_pcb'))

# This specification is deliberately independent of the builder manifest.
spec = {
    'SPI1_MISO': {'U1.3', 'A1.LO14'},
    'SPI1_MOSI': {'U1.4', 'A1.LI14'},
    'SPI1_SCLK': {'U1.5', 'A1.LI13'},
    'SPI1_CS': {'U1.6', 'A1.LO13'},
    'UART_TX': {'A1.RO16', 'U2.1'},
    'UART_RX': {'A1.RI15', 'U2.2'},
    '3V3': {'A1.LI02', 'U1.1'},
    '5V': {'U7.3', 'A1.LO03', 'U2.3'},
    'VBAT': {'BT1.1', 'U7.1', 'U3.1', 'U4.1', 'U5.1', 'U6.1'},
    'GND': {'BT1.2', 'U7.2', 'U7.4', 'A1.LO01', 'U1.2', 'U2.4',
            'U3.2', 'U4.2', 'U5.2', 'U6.2', 'U3.8', 'U4.8', 'U5.8', 'U6.8'},
}
for i, header in enumerate(['RO05', 'RI05', 'RO04', 'RI04'], 1):
    spec[f'PWM_ESC{i}'] = {f'A1.{header}', f'U{i + 2}.6'}
    for ep, mp in [(3, 1), (4, 2), (5, 3)]:
        spec[f'ESC{i}_PHASE_{mp}'] = {f'U{i + 2}.{ep}', f'M{i}.{mp}'}
for pin, name in {
    'LO14': 'A6', 'LI14': 'A7', 'LI13': 'A5', 'LO13': 'A4',
    'RO16': 'A9', 'RI15': 'A10', 'RO05': 'B6', 'RI05': 'B7',
    'RO04': 'B8', 'RI04': 'B9', 'LO03': '5V', 'LI02': '3V3', 'LO01': 'GND',
}.items():
    assert pinnames[f'A1.{pin}'] == name
assert {pinnames[f'U7.{i}'] for i in range(1, 5)} == {'IN+', 'IN-', 'OUT+', 'OUT-'}
assert [pinnames[f'U7.{i}'] for i in range(1, 5)] == ['IN+', 'IN-', 'OUT+', 'OUT-']

xml = ET.parse(VALID / 'Drone_Direct_Wire.net.xml').getroot()
assert {c.attrib['ref'] for c in xml.findall('./components/comp')} == set(expected_components)
netgroups = [frozenset(n.attrib['ref'] + '.' + n.attrib['pin'] for n in net.findall('node'))
             for net in xml.findall('./nets/net')]
connected = [n for n in netgroups if len(n) > 1]
assert len(connected) == len(spec) == 26
assert set(connected) == {frozenset(n) for n in spec.values()}, 'KiCad netlist does not match exact wiring specification'
used = set().union(*spec.values())
unused = set(allpins) - used
singletons = set().union(*(n for n in netgroups if len(n) == 1))
assert singletons == unused

wires = []
for wire in kids(sch, 'wire'):
    pts = [tuple(map(float, p[1:])) for p in kids(one(wire, 'pts'), 'xy')]
    assert len(pts) == 2
    a, b = pts
    assert a != b and (a[0] == b[0] or a[1] == b[1]), (a, b)
    wires.append((a, b))

def contains(pt, a, b):
    return ((a[0] == b[0] == pt[0] and min(a[1], b[1]) <= pt[1] <= max(a[1], b[1])) or
            (a[1] == b[1] == pt[1] and min(a[0], b[0]) <= pt[0] <= max(a[0], b[0])))

nc = [tuple(map(float, one(n, 'at')[1:])) for n in kids(sch, 'no_connect')]
assert len(nc) == len(set(nc)) == len(unused) == 93
assert set(nc) == {allpins[p] for p in unused}
degree = collections.Counter(pt for pair in wires for pt in pair)
junctions = {tuple(map(float, one(n, 'at')[1:])) for n in kids(sch, 'junction')}
assert junctions == {p for p, d in degree.items() if d >= 3}
assert not {pt for pt, d in degree.items() if d == 1 and pt not in allpins.values() and pt not in flag_points.values()}

# Reconstruct connectivity using only actual wire geometry and pin coordinates.
points = set(degree) | junctions | set(allpins.values()) | set(flag_points.values())
parent = {pt: pt for pt in points}
def find(p):
    while parent[p] != p:
        parent[p] = parent[parent[p]]
        p = parent[p]
    return p
def join(a, b):
    parent[find(a)] = find(b)
for a, b in wires:
    for pt in points:
        if contains(pt, a, b):
            join(a, pt)
geom = collections.defaultdict(set)
for pin, pt in allpins.items():
    geom[find(pt)].add(pin)
assert {frozenset(v) for v in geom.values() if len(v) > 1} == {frozenset(n) for n in spec.values()}
assert geom[find(flag_points['#FLG01'])] == spec['VBAT']
assert geom[find(flag_points['#FLG02'])] == spec['3V3']

# Before/after KiCad exports must contain exactly the same physical pin groups.
before_xml = ET.parse(VALID / 'before_PWR_FLAG.net.xml').getroot()
before_groups = {frozenset(n.attrib['ref'] + '.' + n.attrib['pin'] for n in net.findall('node'))
                 for net in before_xml.findall('./nets/net')}
assert before_groups == set(netgroups)
assert all(c.find('footprint') is None or not c.findtext('footprint') for c in xml.findall('./components/comp'))
power_audit = []
for net, flag, source, sinks in [
    ('VBAT', '#FLG01', 'BT1.1', ['U7.1', 'U3.1', 'U4.1', 'U5.1', 'U6.1']),
    ('5V', None, 'U7.3', ['A1.LO03', 'U2.3']),
    ('3V3', '#FLG02', 'A1.LI02', ['U1.1']),
]:
    actual = next(n for n in xml.findall('./nets/net')
                  if frozenset(p.attrib['ref'] + '.' + p.attrib['pin'] for p in n.findall('node')) == spec[net])
    source_node = next(n for n in actual.findall('node') if n.attrib['ref'] + '.' + n.attrib['pin'] == source)
    assert source_node.attrib['pintype'] == ('power_out' if net == '5V' else 'passive')
    power_audit.append({'functional_net': net, 'kicad_net_name': actual.attrib['name'],
                        'source_pin': source, 'source_type_unchanged': source_node.attrib['pintype'],
                        'destinations': sinks, 'erc_flag': flag,
                        'physical_pin_group': sorted(spec[net]),
                        'verified_by': 'KiCad netlist and independent wire geometry; not hardware measurement'})
(VALID / 'power_sources.json').write_text(json.dumps(power_audit, indent=2) + '\n', encoding='utf-8')

for ref, box in bodyboxes.items():
    x1, y1, x2, y2 = box
    assert 10 < x1 < x2 < 584 and 10 < y1 < y2 < 410, ref
    for a, b in wires:
        crosses = ((a[0] == b[0] and x1 < a[0] < x2 and max(min(a[1], b[1]), y1) < min(max(a[1], b[1]), y2)) or
                   (a[1] == b[1] and y1 < a[1] < y2 and max(min(a[0], b[0]), x1) < min(max(a[0], b[0]), x2)))
        assert not crosses, ('Wire enters symbol body', ref, a, b)
    for other, box2 in bodyboxes.items():
        if ref < other:
            a, b, c, d = box2
            assert max(x1, a) >= min(x2, c) or max(y1, b) >= min(y2, d), (ref, other)

center = tuple(map(float, one(byref['A1'], 'at')[1:3]))
for esc, motor, axis, sign in [('U3', 'M1', 1, -1), ('U4', 'M2', 0, 1), ('U5', 'M3', 1, 1), ('U6', 'M4', 0, -1)]:
    e, m = [tuple(map(float, one(byref[r], 'at')[1:3])) for r in [esc, motor]]
    assert 0 < sign * (e[axis] - center[axis]) < sign * (m[axis] - center[axis])
    assert e[1 - axis] == m[1 - axis] == center[1 - axis]
assert bodyboxes['U1'][2] < bodyboxes['A1'][0]
bmi_to_stm_pin_gap = round(allpins['A1.LO14'][0] - allpins['U1.3'][0], 4)
assert 0 < bmi_to_stm_pin_gap <= 30

# Generate a source/destination table from the audited specification.
rows = []
def csvrow(src, dst, func):
    sr, sp = src.split('.')
    dr, dp = dst.split('.')
    rows.append([sr, sp, pinnames[src], dr, dp, pinnames[dst], func])
for net in ['VBAT', 'GND', '5V', '3V3']:
    src = {'VBAT': 'BT1.1', 'GND': 'BT1.2', '5V': 'U7.3', '3V3': 'A1.LI02'}[net]
    for dst in sorted(spec[net] - {src}):
        csvrow(src, dst, net)
for src, dst, func in [
    ('U1.3', 'A1.LO14', 'SPI1_MISO'), ('A1.LI14', 'U1.4', 'SPI1_MOSI'),
    ('A1.LI13', 'U1.5', 'SPI1_SCK'), ('A1.LO13', 'U1.6', 'IMU_CS active low'),
    ('A1.RO16', 'U2.1', 'USART1_TX -> ELRS_RX'), ('U2.2', 'A1.RI15', 'ELRS_TX -> USART1_RX'),
]:
    csvrow(src, dst, func)
for i, header in enumerate(['RO05', 'RI05', 'RO04', 'RI04'], 1):
    csvrow(f'A1.{header}', f'U{i+2}.6', f'TIM4_CH{i} / PWM_M{i}')
    for ep, mp, phase in [(3, 1, 'U'), (4, 2, 'V'), (5, 3, 'W')]:
        csvrow(f'U{i+2}.{ep}', f'M{i}.{mp}', f'ESC{i} -> M{i} phase {phase}')
assert len(rows) == 43
with (ROOT / 'Bang_noi_day.csv').open('w', encoding='utf-8-sig', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Thiet_bi_nguon', 'Ma_chan_nguon', 'Ten_chan_nguon', 'Thiet_bi_dich', 'Ma_chan_dich', 'Ten_chan_dich', 'Chuc_nang'])
    w.writerows(rows)

bom = [
    ('A1', 1, 'STM32F103VET6_DevKit', 'DevKit_STM32', 'Original module symbol'),
    ('U1', 1, 'BMI270_DevKit_V1_3', 'BMI270_DevKit', 'Verify 3.3V supply and SDX/SCX auxiliary interface'),
    ('U2', 1, 'SB_Nano_2G4RX_Physical / ELRS 2.4 GHz', 'SB_Nano_2G4RX_Physical', '5V supply; UART logic level must match STM32'),
    ('U3 U4 U5 U6', 4, 'ESC 30A BLDC', 'ESC_30A', 'All BEC pins NC; verify actual supply/PWM requirements'),
    ('M1 M2 M3 M4', 4, 'A2212/10T 1400KV', 'A2212_Motor', 'No rotation or motor mixer assigned'),
    ('BT1', 1, 'OVONIC LiPo 3S 11.1V 5200mAh 90C', 'OVONIC_LiPo', '12.6V fully charged'),
    ('U7', 1, 'LM2596 complete DC-DC module / set 5.0V', 'LM2596_Module_Logical', 'LOGICAL PIN IDs; physical pin numbering and footprint UNVERIFIED'),
]
with (ROOT / 'BOM.csv').open('w', encoding='utf-8-sig', newline='') as f:
    w = csv.writer(f)
    w.writerow(['Reference', 'Quantity', 'Description', 'Symbol_library', 'Notes'])
    w.writerows(bom)

with (VALID / 'KiCad_BOM.csv').open(encoding='utf-8-sig', newline='') as f:
    kicad_bom = list(csv.DictReader(f))
assert len(kicad_bom) == 13
assert {r['Reference'] for r in kicad_bom} == set(expected_components)
assert all(r['Footprint'] == '' and r['Quantity'] == '1' for r in kicad_bom)

erc = json.loads((VALID / 'Drone_Direct_Wire.erc.json').read_text())
violations = [v for s in erc['sheets'] for v in s['violations']]
assert not violations, violations

report = {
    'status': 'PASS',
    'components': len(instances), 'connected_nets': len(spec),
    'wire_segments': len(wires), 'connected_pins': len(used), 'no_connects': len(nc),
    'wiring_csv_rows': len(rows), 'bom_quantity': sum(row[1] for row in bom),
    'source_libraries_unchanged': True, 'embedded_symbols_identical_to_libraries': True,
    'exact_required_pin_mapping': True, 'independent_wire_geometry_matches_kicad_netlist': True,
    'dangling_wire_endpoints': 0, 'wire_through_rectangle_bodies': 0,
    'overlapping_symbol_bodies': 0, 'labels': 0, 'erc_power_flags': 2,
    'erc_flags_in_bom_or_on_board': False, 'physical_nets_unchanged_from_before_flags': True,
    'kicad_exported_bom_verified_no_flags': True,
    'all_four_bec_pins_no_connect': True, 'bmi_to_stm_pin_gap_mm': bmi_to_stm_pin_gap,
    'erc_errors': 0, 'erc_warnings': 0, 'erc_rules_modified_or_exclusions_added': False,
    'erc_explanation': [
        '#FLG01 is wired only to the verified BT1+ / U7.IN+ / ESC BAT+ net; BT1+ remains passive.',
        '#FLG02 is wired only to the verified A1.LI02 / U1.VCC net; A1.LI02 remains passive.',
        'U7.OUT+ already drives the 5V net to A1.LO03 and U2.3; no redundant flag on 5V.',
        'Two ERC-only declarations, excluded from BOM and board. All 13 physical modules and all physical pin groups unchanged.',
    ],
}
(VALID / 'verification.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, indent=2))
