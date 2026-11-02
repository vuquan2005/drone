"""Generate module headers from the user's spatial CSV, USB up."""
from pathlib import Path
import csv
import re
root = Path(__file__).resolve().parent
layout_file = root / 'stm32_header_layout.csv'
layout = list(csv.reader(layout_file.open(encoding='utf-8-sig')))
assert len(layout) == 18 and all(len(r) == 18 for r in layout)
# User confirmed C3 -> C9 at row 17, column 18.
assert layout[16][17] in ('C3','C9')
layout[16][17] = 'C9'
with layout_file.open('w',newline='',encoding='utf-8') as f:
    csv.writer(f).writerows(layout)
name = 'STM32F103VET6_DevKit_Headers'
p = [f'(symbol "{name}" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)',
     '(property "Reference" "A" (at 0 76.2 0) (effects (font (size 1.27 1.27))))',
     f'(property "Value" "{name}" (at 0 73.66 0) (effects (font (size 1.27 1.27))))',
     '(property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
     '(property "Description" "Module headers from user CSV, USB up. Custom position IDs, no footprint." (at 0 0 0) (effects (font (size 1.27 1.27)) hide))',
     f'(symbol "{name}_0_1" (rectangle (start -55.88 55.88) (end 55.88 -58.42) (stroke (width 0.254) (type default)) (fill (type background)))']
for label,y in [('USB UP',48.26),('STM32F103VET6 DEV KIT',10.16),('LEFT 2x18 / RIGHT 2x18',5.08),('BOTTOM 2x10 / SWD + BOOT',0),('O = OUTER / I = INNER',-5.08)]:
    p.append(f'(text "{label}" (at 0 {y} 0) (effects (font (size 1.27 1.27))))')
p.extend([')',f'(symbol "{name}_1_1"'])
mapping=[]
def pin(ident,raw,x,y,angle,header,row,column,cr,cc):
    raw=raw.strip().upper()
    signal={'VB':'VBAT'}.get(raw,raw)
    kind='input' if signal in ('BOOT0','BOOT1') else ('bidirectional' if re.fullmatch('[ABCDE][0-9]+',signal) or signal in ('SWCLK','SWDIO') else 'passive')
    p.append(f'(pin {kind} line (at {x:.2f} {y:.2f} {angle}) (length 5.08) (name "{signal}" (effects (font (size 1.016 1.016)))) (number "{ident}" (effects (font (size 0.762 0.762)))))')
    mapping.append((ident,header,row,column,signal,cr,cc))
for side,cols,x,angle in [('L',[('O',0),('I',1)],-60.96,0),('R',[('O',17),('I',16)],60.96,180)]:
    for r in range(18):
        for j,(col,c) in enumerate(cols):
            pin(f'{side}{col}{r+1:02}',layout[r][c],x,45.72-r*5.08-j*2.54,angle,side,r+1,col,r+1,c+1)
for c in range(10):
    for j,(col,r) in enumerate([('I',16),('O',17)]):
        pin(f'B{col}{c+1:02}',layout[r][c+4],-48.26+(2*c+j)*5.08,-63.50,90,'B',c+1,col,r+1,c+5)
for i,r in enumerate(range(11,15)):
    pin(f'S{i+1}',layout[r][14],-15.24+i*10.16,60.96,270,'SWD',i+1,'single',r+1,15)
for i,(r,signal) in enumerate([(3,'BOOT0'),(2,'BOOT1')]):
    pin(signal,signal,30.48+i*10.16,60.96,270,'BOOT',i+1,'center',r+1,5)
assert len(mapping)==98 and len({m[0] for m in mapping})==98
gpio=[m[4] for m in mapping if re.fullmatch('[ABCDE][0-9]+',m[4])]
assert len(gpio)==80 and set(gpio)=={port+str(i) for port in 'ABCDE' for i in range(16)}
assert not any('?' in m[4] for m in mapping)
p.extend([')',')'])
lib=root/'DevKit_STM32.kicad_sym'
header_symbol='\n'.join(p)
main_symbol=header_symbol.replace(name,'STM32F103VET6_DevKit')
lib.write_text('(kicad_symbol_lib (version 20241209) (generator "devkit_symbol")\n'+main_symbol+'\n'+header_symbol+'\n)\n',encoding='utf-8')
with (root/'DevKit_STM32_headers.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.writer(f)
    w.writerow(['position_id','header','row_from_top_or_left','column','signal','csv_row','csv_column'])
    w.writerows(mapping)
print('Validated: 98 contacts including BOOT0/BOOT1, 80 unique GPIOs without P prefix.')
