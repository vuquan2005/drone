"""Build the module-only quadrotor schematic from unmodified local libraries.

No external Python packages required. Run with Python 3.11 or newer.
"""
from pathlib import Path
import collections
import csv
import hashlib
import json
import math
import re
import uuid

REPO_ROOT = Path(__file__).resolve().parents[1]
ROOT = REPO_ROOT / 'hardware' / 'kicad'
SYMBOLS = ROOT / 'symbols'
SYMBOLS.mkdir(parents=True, exist_ok=True)
ROOT.mkdir(exist_ok=True)
OUT = ROOT / "Drone_Direct_Wire.kicad_sch"
VALID = REPO_ROOT / "output" / "validation"
VALID.mkdir(parents=True, exist_ok=True)

# Project-local libraries and their registration table are maintained in KiCad.
# Read them as inputs; rebuilding the schematic must not overwrite libraries.
for name in (
    'A2212_Motor', 'BMI270_DevKit', 'DevKit_STM32', 'ERC_Only',
    'ESC_30A', 'LM2596_Module_Logical', 'OVONIC_LiPo',
    'SB_Nano_2G4RX_Physical',
):
    if not (SYMBOLS / (name + '.kicad_sym')).is_file():
        raise FileNotFoundError('Missing project symbol library: ' + name)
if not (ROOT / 'sym-lib-table').is_file():
    raise FileNotFoundError('Missing project sym-lib-table')

(ROOT / 'Drone_Direct_Wire.kicad_pro').write_text(json.dumps({'meta': {'filename': 'Drone_Direct_Wire.kicad_pro', 'version': 1}}, indent=2) + '\n', encoding='utf-8')

class Atom(str):
    pass

def parse(text):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text)
    def read(i):
        tok = tokens[i]
        if tok == '(':
            result = []
            i += 1
            while tokens[i] != ')':
                value, i = read(i)
                result.append(value)
            return result, i + 1
        if tok.startswith('"'):
            return json.loads(tok), i + 1
        return Atom(tok), i + 1
    return read(0)[0]

def kids(node, key):
    return [v for v in node if isinstance(v, list) and v and v[0] == key]

def one(node, key):
    return kids(node, key)[0]

def dumps(node):
    if isinstance(node, list):
        return '(' + ' '.join(map(dumps, node)) + ')'
    if isinstance(node, Atom):
        return str(node)
    return json.dumps(node, ensure_ascii=False)

def q(s):
    return json.dumps(str(s), ensure_ascii=False)

def uid(key):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, 'quadrotor-module-wiring/' + key))

def fmt(n):
    return f'{n:.4f}'.rstrip('0').rstrip('.') or '0'

libraries = {}
input_hashes = {}
for file in sorted(SYMBOLS.glob('*.kicad_sym')):
    input_hashes[file.name] = hashlib.sha256(file.read_bytes()).hexdigest()
    lib = parse(file.read_text(encoding='utf-8-sig'))
    for sym in kids(lib, 'symbol'):
        libraries[file.stem + ':' + sym[1]] = sym

placements = [
    ('A1', 'DevKit_STM32:STM32F103VET6_DevKit', 304.8, 203.2, 0),
    ('U1', 'BMI270_DevKit:BMI270_DevKit_V1_3', 210.82, 218.44, 0),
    ('U2', 'SB_Nano_2G4RX_Physical:SB_Nano_2G4RX_Physical', 400.05, 264.16, 270),
    ('U3', 'ESC_30A:ESC_30A_BLDC', 304.8, 91.44, 90),
    ('U4', 'ESC_30A:ESC_30A_BLDC', 457.2, 203.2, 0),
    ('U5', 'ESC_30A:ESC_30A_BLDC', 304.8, 325.12, 270),
    ('U6', 'ESC_30A:ESC_30A_BLDC', 114.3, 203.2, 180),
    ('M1', 'A2212_Motor:A2212_10T_1400KV', 304.8, 40.64, 90),
    ('M2', 'A2212_Motor:A2212_10T_1400KV', 520.7, 203.2, 0),
    ('M3', 'A2212_Motor:A2212_10T_1400KV', 304.8, 375.92, 270),
    ('M4', 'A2212_Motor:A2212_10T_1400KV', 50.8, 203.2, 180),
    ('BT1', 'OVONIC_LiPo:OVONIC_LiPo_3S_5200mAh_90C', 403.86, 63.5, 0),
    ('U7', 'LM2596_Module_Logical:LM2596_Module_Logical', 457.2, 93.98, 0),
]
pins = {}
names = {}
parts = {}
for ref, libid, cx, cy, angle in placements:
    sym = libraries[libid]
    parts[ref] = (libid, cx, cy, angle)
    for unit in kids(sym, 'symbol'):
        for pin in kids(unit, 'pin'):
            x, y, _ = map(float, one(pin, 'at')[1:])
            r = math.radians(angle)
            pt = (round(cx + x * math.cos(r) - y * math.sin(r), 4),
                  round(cy - x * math.sin(r) - y * math.cos(r), 4))
            number, name = one(pin, 'number')[1], one(pin, 'name')[1]
            pins[ref, number] = pt
            names[ref, name] = number

def p(ref, number):
    return pins[ref, str(number)]

def gpio(name):
    return p('A1', names['A1', name])

routes = []
expected = collections.defaultdict(set)

def route(net, *points):
    for a, b in zip(points, points[1:]):
        if a != b:
            assert a[0] == b[0] or a[1] == b[1], (net, a, b)
            routes.append((net, a, b))

def signal(net, ref1, pin1, ref2, pin2, middle=()):
    route(net, p(ref1, pin1), *middle, p(ref2, pin2))
    expected[net].update(((ref1, str(pin1)), (ref2, str(pin2))))

# Four short SPI traces next to A4/A5/A6/A7. Crossings have no junction.
for net, upin, gname, x in [
    ('SPI1_MISO', 3, 'A6', 234.95),
    ('SPI1_MOSI', 4, 'A7', 232.41),
    ('SPI1_SCLK', 5, 'A5', 237.49),
    ('SPI1_CS', 6, 'A4', 240.03),
]:
    a, b = p('U1', upin), gpio(gname)
    signal(net, 'U1', upin, 'A1', names['A1', gname], [(x, a[1]), (x, b[1])])

# UART is crossed by function: MCU TX A9 -> receiver RX, receiver TX -> A10.
for net, upin, gname, x in [('UART_TX', 1, 'A9', 373.38), ('UART_RX', 2, 'A10', 375.92)]:
    a, b = gpio(gname), p('U2', upin)
    signal(net, 'A1', names['A1', gname], 'U2', upin, [(x, a[1]), (x, b[1])])

# TIM4 channels 1..4: the numbers identify schematic arms only.
a, b = gpio('B6'), p('U3', 6)
signal('PWM_ESC1', 'A1', names['A1', 'B6'], 'U3', 6,
       [(383.54, a[1]), (383.54, 137.16), (b[0], 137.16)])
a, b = gpio('B7'), p('U4', 6)
signal('PWM_ESC2', 'A1', names['A1', 'B7'], 'U4', 6,
       [(391.16, a[1]), (391.16, b[1])])
a, b = gpio('B8'), p('U5', 6)
signal('PWM_ESC3', 'A1', names['A1', 'B8'], 'U5', 6,
       [(419.1, a[1]), (419.1, 284.48), (b[0], 284.48)])
a, b = gpio('B9'), p('U6', 6)
signal('PWM_ESC4', 'A1', names['A1', 'B9'], 'U6', 6,
       [(386.08, a[1]), (386.08, 134.62), (157.48, 134.62), (157.48, b[1])])

# Dedicated three-phase wiring on each radial arm.
for i in range(1, 5):
    esc, motor = 'U' + str(i + 2), 'M' + str(i)
    for ep, mp in [(3, 1), (4, 2), (5, 3)]:
        a, b = p(esc, ep), p(motor, mp)
        if i in (1, 3):
            y = 60.96 if i == 1 else 355.6
            middle = [(a[0], y), (b[0], y)]
        else:
            x = 492.76 if i == 2 else 80.01
            middle = [(x, a[1]), (x, b[1])]
        signal(f'ESC{i}_PHASE_{mp}', esc, ep, motor, mp, middle)

# Battery rails are real continuous wires. No labels or power symbols.
route('VBAT', p('BT1', 1), (396.24, 53.34), (396.24, 121.92), (431.8, 121.92),
      (142.24, 121.92), (142.24, 294.64), (317.5, 294.64), p('U5', 1))
route('VBAT', p('U7', 1), (431.8, 88.9), (431.8, 121.92))
route('VBAT', p('U3', 1), (292.1, 121.92))
route('VBAT', p('U4', 1), (431.8, 190.5), (431.8, 121.92))
route('VBAT', p('U6', 1), (142.24, 215.9))
expected['VBAT'].update([('BT1', '1'), ('U7', '1')] + [(f'U{i}', '1') for i in range(3, 7)])

route('GND', p('BT1', 2), (426.72, 76.2), (426.72, 127),
      (147.32, 127), (147.32, 289.56), (373.38, 289.56))
route('GND', p('U7', 2), (426.72, 99.06))
route('GND', p('U7', 4), (487.68, 99.06), (487.68, 127), (426.72, 127))
expected['GND'].update([('U7', '2'), ('U7', '4')])
for ref, n, mid in [
    ('U3', 2, [(297.18, 127)]), ('U3', 8, [(317.5, 127)]),
    ('U4', 2, [(426.72, 195.58), (426.72, 127)]),
    ('U4', 8, [(421.64, 215.9), (421.64, 127)]),
    ('U5', 2, [(312.42, 289.56)]), ('U5', 8, [(292.1, 289.56)]),
    ('U6', 2, [(147.32, 210.82)]),
    ('U6', 8, [(152.4, 190.5), (152.4, 127)]),
    ('A1', 'LO01', [(233.68, 157.48), (233.68, 127)]),
    ('U1', 2, [(236.22, 213.36), (236.22, 157.48), (233.68, 157.48)]),
    ('U2', 4, [(373.38, 271.78), (373.38, 289.56)]),
]:
    route('GND', p(ref, n), *mid)
    expected['GND'].add((ref, str(n)))
expected['GND'].add(('BT1', '2'))

signal('3V3', 'A1', 'LI02', 'U1', 1,
       [(231.14, 165.1), (231.14, 208.28)])
route('5V', p('U7', 3), (492.76, 88.9), (492.76, 132.08),
      (238.76, 132.08), (238.76, 167.64), p('A1', 'LO03'))
route('5V', (408.94, 132.08), (408.94, 238.76), (378.46, 238.76),
      (378.46, 266.7), p('U2', 3))
expected['5V'].update([('U7', '3'), ('A1', 'LO03'), ('U2', '3')])

# Only the two source nets whose actual drivers are passive in the original
# module symbols. These declarations do not replace any direct wire.
erc_flags = [
    ('#FLG01', 'VBAT', (388.62, 104.14), (396.24, 104.14)),
    ('#FLG02', '3V3', (223.52, 182.88), (231.14, 182.88)),
]
for ref, net, point, tap in erc_flags:
    route(net, point, tap)

def on_segment(pt, a, b):
    return ((a[0] == b[0] == pt[0] and min(a[1], b[1]) <= pt[1] <= max(a[1], b[1])) or
            (a[1] == b[1] == pt[1] and min(a[0], b[0]) <= pt[0] <= max(a[0], b[0])))

# A wire endpoint meeting a different net would silently short it in KiCad.
for i, (net, a, b) in enumerate(routes):
    for other, c, d in routes[i + 1:]:
        if net != other:
            for pt, s1, s2 in [(a, c, d), (b, c, d), (c, a, b), (d, a, b)]:
                assert not on_segment(pt, s1, s2), ('endpoint short', net, other, pt)
    for key, pt in pins.items():
        if on_segment(pt, a, b):
            assert key in expected[net], ('wire touches unintended pin', net, key, pt)

# Split only same-net branches and make explicit three-way junctions.
segments = set()
for net, a, b in routes:
    cuts = {a, b}
    for other, c, d in routes:
        if other == net:
            cuts.update(pt for pt in (c, d) if on_segment(pt, a, b))
    ordered = sorted(cuts)
    for c, d in zip(ordered, ordered[1:]):
        if c != d:
            segments.add((net, c, d))
degree = collections.Counter((net, pt) for net, a, b in segments for pt in (a, b))

root_id = uid('root')
chunks = [f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {root_id})',
          '(paper "A2")',
          '(title_block (title "QUADROTOR - DIRECT MODULE WIRING") (date "2026-11-02") (rev "2.1") (comment 1 "Direct wires retained; two PWR_FLAG declarations for ERC only"))',
          '(lib_symbols']
for libid in sorted({item[1] for item in placements} | {'ERC_Only:PWR_FLAG'}):
    sym = list(libraries[libid])
    sym[1] = libid
    chunks.append(dumps(sym))
chunks.append(')')

for ref, libid, cx, cy, angle in placements:
    sym = libraries[libid]
    value = next(v[2] for v in kids(sym, 'property') if v[1] == 'Value')
    # Readable horizontal instance properties, keeping library graphics intact.
    if ref == 'A1':
        tx, ty = cx, 167.64
    elif ref == 'U1':
        tx, ty = cx, 198.12
    elif ref == 'U2':
        tx, ty = cx, 244.0
    elif ref == 'U7':
        tx, ty = cx, cy - 20.32
    elif ref in ('U3', 'U5'):
        tx, ty = cx + 48, cy - 3.81
    elif ref in ('U4', 'U6'):
        tx, ty = cx, cy - 25.4
    elif ref in ('M1', 'M3'):
        tx, ty = cx + 35.56, cy - 2.54
    elif ref in ('M2', 'M4'):
        tx, ty = cx, cy - 16.51
    else:
        tx, ty = cx + 6.35, 38.1
    chunks.append(f'(symbol (lib_id {q(libid)}) (at {fmt(cx)} {fmt(cy)} {angle}) (unit 1) (in_bom yes) (on_board no) (dnp no) (uuid {uid(ref)})')
    for name, text, y in [('Reference', ref, ty), ('Value', value, ty + 2.54)]:
        text_angle = 90 if angle in (90, 270) else 0
        chunks.append(f'(property {q(name)} {q(text)} (at {fmt(tx)} {fmt(y)} {text_angle}) (effects (font (size 1.27 1.27))))')
    chunks.append(f'(property "Footprint" "" (at {cx} {cy} 0) (effects (font (size 1.27 1.27)) hide))')
    for key in pins:
        if key[0] == ref:
            chunks.append(f'(pin {q(key[1])} (uuid {uid(ref + ":" + key[1])}))')
    chunks.append(f'(instances (project "Drone_Direct_Wire" (path "/{root_id}" (reference {q(ref)}) (unit 1)))) )')

for ref, net, point, tap in erc_flags:
    x, y = point
    chunks.append(f'(symbol (lib_id "ERC_Only:PWR_FLAG") (at {x} {y} 0) (unit 1) (in_bom no) (on_board no) (dnp no) (uuid {uid(ref)})')
    chunks.append(f'(property "Reference" {q(ref)} (at {x} {fmt(y - 1.905)} 0) (effects (font (size 1.27 1.27)) hide))')
    chunks.append(f'(property "Value" "PWR_FLAG" (at {x} {fmt(y - 3.81)} 0) (effects (font (size 1.27 1.27))))')
    chunks.append(f'(property "Footprint" "" (at {x} {y} 0) (effects (font (size 1.27 1.27)) hide))')
    chunks.append(f'(pin "1" (uuid {uid(ref + ":1")}))')
    chunks.append(f'(instances (project "Drone_Direct_Wire" (path "/{root_id}" (reference {q(ref)}) (unit 1)))) )')

for i, (net, a, b) in enumerate(sorted(segments)):
    chunks.append(f'(wire (pts (xy {fmt(a[0])} {fmt(a[1])}) (xy {fmt(b[0])} {fmt(b[1])})) (stroke (width 0) (type default)) (uuid {uid("wire" + str(i))}))')
for (net, pt), deg in sorted(degree.items()):
    if deg >= 3:
        chunks.append(f'(junction (at {fmt(pt[0])} {fmt(pt[1])}) (diameter 0) (color 0 0 0 0) (uuid {uid("junction" + str(pt))}))')

used = set().union(*expected.values())
for key, pt in pins.items():
    if key not in used:
        chunks.append(f'(no_connect (at {fmt(pt[0])} {fmt(pt[1])}) (uuid {uid("nc" + str(key))}))')

notes = [
    ('FRONT / ESC1 - M1', 304.8, 20.32, 2),
    ('ESC2 - M2', 508, 154.94, 1.5),
    ('ESC3 - M3', 304.8, 398.78, 1.5),
    ('ESC4 - M4', 66.04, 154.94, 1.5),
    ('ELRS 2.4 GHz / UART1', 400.05, 296.18, 1.27),
    ('SPI1 / BMI270 - FIRMWARE CONFIGURATION', 30.48, 30.48, 1.5),
    ('Master, full duplex, 8-bit, MSB first; Mode 0: CPOL=0, CPHA=0.', 30.48, 38.1, 1.27),
    ('Software NSS; SPI1 clock 72 MHz / 32 = SCK 2.25 MHz.', 30.48, 45.72, 1.27),
    ('PA4: GPIO output push-pull, initial High; CS active Low.', 30.48, 53.34, 1.27),
    ('Select SPI and initialize BMI270 per datasheet / Bosch SensorAPI.', 30.48, 60.96, 1.27),
    ('Configuration notes only: firmware has NOT been created or flashed.', 30.48, 68.58, 1.27),
    ('U1 INT1 / INT2 / SDX / SCX: NC; PA0 unused (poll SPI).', 30.48, 83.82, 1.27),
    ('VERIFY U1.8 SDX / U1.9 SCX are auxiliary ASDx / ASCx on V1.3;', 30.48, 91.44, 1.27),
    ('do not confuse these with Bosch chip primary SDx / SCx.', 30.48, 99.06, 1.27),
    ('CONTROL / UNUSED PINS', 30.48, 315.0, 1.5),
    ('USART1: CRSF, 420000 baud, 8N1, non-inverted, full duplex;', 30.48, 322.58, 1.27),
    ('no hardware flow control. PA9 TX -> RX; PA10 RX <- TX.', 30.48, 330.2, 1.27),
    ('TIM4 default, no remap: B6 / B7 / B8 / B9 = ESC1 / 2 / 3 / 4.', 30.48, 337.82, 1.27),
    ('PWM frequency, pulse range and calibration: match actual ESC.', 30.48, 345.44, 1.27),
    ('400 Hz and DShot support are NOT assumed.', 30.48, 353.06, 1.27),
    ('All four ESC pin 7 BEC/VCC: NC. No BEC or USB logic supply.', 30.48, 360.68, 1.27),
    ('PA13/PA14 reserved for SWD; set existing BOOT0 jumper = 0.', 30.48, 368.3, 1.27),
    ('Motor numbers do not define rotation or mixer.', 30.48, 375.92, 1.27),
    ('Crossings connect only where a junction dot is shown.', 30.48, 383.54, 1.27),
    ('POWER / HARDWARE VERIFICATION', 386.08, 307.34, 1.5),
    ('Adjust AND measure LM2596 output = 5.0 V before connecting loads.', 386.08, 314.96, 1.27),
    ('Verify DevKit accepts power at 5V and LM2596 has enough current.', 386.08, 322.58, 1.27),
    ('Verify BMI270 V1.3 module supports a 3.3 V supply.', 386.08, 330.2, 1.27),
    ('LiPo 3S: nominal 11.1 V; fully charged 12.6 V.', 386.08, 337.82, 1.27),
    ('Never connect LiPo to STM32 5V, 3V3 or backup VBAT.', 386.08, 345.44, 1.27),
    ('Never connect LM2596 5 V output to 3V3.', 386.08, 353.06, 1.27),
    ('USB is NOT the supply; verify DevKit backfeed protection', 386.08, 360.68, 1.27),
    ('before simultaneous USB connection.', 386.08, 368.3, 1.27),
    ('U7: logical module symbol; physical pin numbers / footprint UNVERIFIED.', 370.84, 30.48, 1.27),
    ('Two PWR_FLAGs: ERC declarations only; no hardware, BOM or PCB.', 30.48, 391.16, 1.27),
]
for i, (text, x, y, size) in enumerate(notes):
    justify = '' if i < 5 else '(justify left)'
    chunks.append(f'(text {q(text)} (at {x} {y} 0) (effects (font (size {size} {size})) {justify}) (uuid {uid("text" + str(i))}))')
chunks.append(')')
OUT.write_text('\n'.join(chunks) + '\n', encoding='utf-8')

# Artifact-level evidence for the independent exported-netlist audit.
manifest = {
    'source_sha256': input_hashes,
    'components': {r: lib for r, lib, *_ in placements},
    'expected_nets': {n: sorted(f'{r}.{p}' for r, p in v) for n, v in expected.items()},
    'wire_segments': len(segments), 'no_connects': len(pins) - len(used),
    'labels': 0, 'power_symbols': 2, 'footprints': 0,
    'erc_flags': {ref: {'net': net, 'pin': list(pt), 'wire_tap': list(tap), 'in_bom': False, 'on_board': False} for ref, net, pt, tap in erc_flags},
    'power_assumption': 'LM2596 OUT+ powers A1 and U2 at 5V; A1 3V3 powers U1. All ESC BEC pins unused. USB is not the power source.',
}
(VALID / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(f'Created {OUT}: {len(placements)} modules, {len(segments)} wire segments, {len(expected)} nets')

if __name__ == '__main__':
    pass
