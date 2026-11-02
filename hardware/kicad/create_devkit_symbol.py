"""Generate a logical dev-kit symbol; pin IDs are signal names, NOT header positions."""
from pathlib import Path
import csv

ROOT = Path(__file__).resolve().parent
NAME = 'STM32F103VET6_DevKit_Logical'
parts = ['(kicad_symbol_lib (version 20241209) (generator "devkit_symbol")',
         f'(symbol "{NAME}" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)',
         '(property "Reference" "A" (at 0 26.67 0) (effects (font (size 1.27 1.27))))',
         f'(property "Value" "{NAME}" (at 0 24.13 0) (effects (font (size 1.27 1.27))))',
         '(property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
         '(property "Description" "Logical dev-kit draft. Signal IDs are NOT physical header pad numbers. Verify header breakout before PCB use." (at 0 0 0) (effects (font (size 1.27 1.27)) hide))']
rows = []
for unit, port in enumerate('ABCDE', 1):
    parts.append(f'(symbol "{NAME}_{unit}_0" (rectangle (start -12.7 22.86) (end 12.7 -22.86) (stroke (width 0.254) (type default)) (fill (type background))) (text "PORT {port}" (at 0 -21.59 0) (effects (font (size 1.524 1.524)))))')
    parts.append(f'(symbol "{NAME}_{unit}_1"')
    for i in range(16):
        signal = f'P{port}{i}'
        label = signal + ({'PA13':'/SWDIO','PA14':'/SWCLK','PB2':'/BOOT1','PC14':'/OSC32_IN','PC15':'/OSC32_OUT','PD0':'/OSC_IN','PD1':'/OSC_OUT'}.get(signal, ''))
        y = 19.05 - i * 2.54
        parts.append(f'(pin bidirectional line (at -17.78 {y:.2f} 0) (length 5.08) (name "{label}" (effects (font (size 1.27 1.27)))) (number "{signal}" (effects (font (size 1.016 1.016)))))')
        rows.append((unit, signal, label, 'UNVERIFIED', 'Logical signal ID; not a physical pad number'))
    parts.append(')')
parts.append(f'(symbol "{NAME}_6_0" (rectangle (start -12.7 12.7) (end 12.7 -12.7) (stroke (width 0.254) (type default)) (fill (type background))) (text "POWER / CTRL" (at 0 -10.16 0) (effects (font (size 1.27 1.27)))))')
parts.append(f'(symbol "{NAME}_6_1"')
for i, signal in enumerate(['3V3','5V','GND','VBAT','NRST','BOOT0']):
    kind = 'input' if signal in ('NRST','BOOT0') else 'passive'
    parts.append(f'(pin {kind} line (at -17.78 {8.89-i*2.54:.2f} 0) (length 5.08) (name "{signal}" (effects (font (size 1.27 1.27)))) (number "{signal}" (effects (font (size 1.016 1.016)))))')
    rows.append((6, signal, signal, 'UNVERIFIED', 'Power direction and header breakout require board schematic'))
parts.extend([')', ')'])
MODULE = 'STM32F103VET6_DevKit'
parts.extend([
    f'(symbol "{MODULE}" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)',
    '(property "Reference" "A" (at 0 71.12 0) (effects (font (size 1.27 1.27))))',
    f'(property "Value" "{MODULE}" (at 0 68.58 0) (effects (font (size 1.27 1.27))))',
    '(property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
    '(property "Description" "Logical module draft; GPIO breakout and header pad mapping UNVERIFIED. No footprint." (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
    f'(symbol "{MODULE}_0_1" (rectangle (start -22.86 66.04) (end 22.86 -66.04) (stroke (width 0.254) (type default)) (fill (type background)))',
    '(text "STM32F103" (at 0 5.08 0) (effects (font (size 1.524 1.524))))',
    '(text "VET6 DEV KIT" (at 0 0 0) (effects (font (size 1.27 1.27))))',
    '(text "LOGICAL DRAFT" (at 0 -5.08 0) (effects (font (size 1.016 1.016))))',
    ')',
    f'(symbol "{MODULE}_1_1"',
])
for side, groups in [('left', ['A','B','C']), ('right', ['D','E','CTRL'])]:
    y = 62.23
    for group in groups:
        selected = [r for r in rows if (r[1].startswith('P'+group) if group != 'CTRL' else r[0] == 6)]
        for _, signal, label, *_ in selected:
            x, angle = (-27.94, 0) if side == 'left' else (27.94, 180)
            kind = 'bidirectional' if signal.startswith('P') else ('input' if signal in ('NRST','BOOT0') else 'passive')
            parts.append(f'(pin {kind} line (at {x} {y:.2f} {angle}) (length 5.08) (name "{label}" (effects (font (size 1.016 1.016)))) (number "{signal}" (effects (font (size 0.762 0.762)))))')
            y -= 2.54
        y -= 2.54
parts.extend([')', ')', ')'])
(ROOT / 'DevKit_STM32.kicad_sym').write_text('\n'.join(parts)+'\n', encoding='utf-8')
with (ROOT / 'DevKit_STM32_pinmap.csv').open('w', newline='', encoding='utf-8') as f:
    writer = csv.writer(f)
    writer.writerow(['unit','logical_pin_id','pin_name','physical_header_mapping','note'])
    writer.writerows(rows)
print(f'Generated {len(rows)} unique logical pins in 6 units')


