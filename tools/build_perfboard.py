"""Editable, explicitly PROVISIONAL 70x90 perfboard assembly project.
Stages: schematic (standard Python), board (KiCad Python), render (bundled Python).
No manufactured copper routes are generated. Never infer dimensions from symbols.
"""
from pathlib import Path
import sys, json, re, uuid, csv, shutil, math, collections, heapq, hashlib
BASE=Path(__file__).resolve().parents[1]
OUT=BASE/'output'/'Drone_Perfboard_70x90'
OUT.mkdir(exist_ok=True)
(OUT/'validation').mkdir(exist_ok=True)
NAME='Drone_Perfboard_70x90'
P=2.54
def ident(s): return str(uuid.uuid5(uuid.NAMESPACE_URL,'drone-perf70x90/'+s))
def parse(t):
    ts=re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+',t)
    def rd(i):
        if ts[i]=='(':
            a=[]; i+=1
            while ts[i]!=')': v,i=rd(i); a.append(v)
            return a,i+1
        return (json.loads(ts[i]) if ts[i].startswith('"') else ts[i]),i+1
    return rd(0)[0]
def kids(n,k): return [x for x in n if isinstance(x,list) and x and x[0]==k]
def one(n,k): return kids(n,k)[0]
def q(s): return json.dumps(str(s),ensure_ascii=False)
def xy(c,r): return [round(1.98+(c-1)*P,5),round(1.82+(r-1)*P,5)]
def hole(c,r): return f'R{r:02d}-C{c:02d}'
def symbols():
    d={}
    for fn in ['DevKit_STM32','BMI270_DevKit','SB_Nano_2G4RX_Physical','ERC_Only']:
        src=BASE/'output'/(fn+'.kicad_sym'); shutil.copy2(src,OUT/src.name)
        # Preserve the entire original symbol text, rather than reconstruct graphics.
        text=src.read_text(encoding='utf-8-sig')
        root=parse(text)
        for sym in kids(root,'symbol'):
            start=text.index('(symbol '+q(sym[1]))
            depth=0; quoted=False; escaped=False
            for i in range(start,len(text)):
                ch=text[i]
                if quoted:
                    if escaped: escaped=False
                    elif ch=='\\': escaped=True
                    elif ch=='"': quoted=False
                elif ch=='"': quoted=True
                elif ch=='(': depth+=1
                elif ch==')':
                    depth-=1
                    if depth==0: break
            libid=fn+':'+sym[1]
            d[libid]=(sym,text[start:i+1].replace('(symbol '+q(sym[1]),'(symbol '+q(libid),1))
    io='''(kicad_symbol_lib (version 20231120) (generator "kicad_symbol_editor")
    (symbol "ESC_S_NC_GND" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)
      (property "Reference" "J" (at 0 11.43 0) (effects (font (size 1.27 1.27))))
      (property "Value" "ESC: S - NC - GND" (at 0 8.89 0) (effects (font (size 1.27 1.27))))
      (symbol "ESC_S_NC_GND_0_1" (rectangle (start -12.7 7.62) (end 12.7 -7.62) (stroke (width 0.254) (type default)) (fill (type background))))
      (symbol "ESC_S_NC_GND_1_1"
        (pin passive line (at -17.78 5.08 0) (length 5.08) (name "S" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.016 1.016)))))
        (pin passive line (at -17.78 0 0) (length 5.08) (name "NC" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.016 1.016)))))
        (pin passive line (at -17.78 -5.08 0) (length 5.08) (name "GND" (effects (font (size 1.27 1.27)))) (number "3" (effects (font (size 1.016 1.016)))))
      ))
    (symbol "Solder_Point" (pin_names (offset 0)) (in_bom no) (on_board yes)
      (property "Reference" "TP" (at 0 3.81 0) (effects (font (size 1.27 1.27))))
      (property "Value" "WIRE SOLDER POINT" (at 0 6.35 0) (effects (font (size 1.27 1.27))))
      (symbol "Solder_Point_0_1" (circle (center 0 0) (radius 1.016) (stroke (width 0.254) (type default)) (fill (type none))))
      (symbol "Solder_Point_1_1" (pin passive line (at 0 0 90) (length 0) (name "" (effects (font (size 1.016 1.016)))) (number "1" (effects (font (size 1.016 1.016)))))))
    )'''
    (OUT/'Perfboard_IO.kicad_sym').write_text(io,encoding='utf-8')
    # Recursive extraction from the small library via balanced blocks.
    rr=parse(io)
    for s in kids(rr,'symbol'):
        a=io.index('(symbol '+q(s[1])); level=0
        for j in range(a,len(io)):
            level+= (io[j]=='(')-(io[j]==')')
            if level==0: break
        lib='Perfboard_IO:'+s[1]; d[lib]=(s,io[a:j+1].replace('(symbol '+q(s[1]),'(symbol '+q(lib),1))
    return d

def stage_schematic():
    libs=symbols()
    specs=[('A1','DevKit_STM32:STM32F103VET6_DevKit',175.26,147.32,0),
      ('U1','BMI270_DevKit:BMI270_DevKit_V1_3',81.28,162.56,0),
      ('U2','SB_Nano_2G4RX_Physical:SB_Nano_2G4RX_Physical',276.86,193.04,270),
      ('J1','Perfboard_IO:ESC_S_NC_GND',284.48,45.72,0),
      ('J2','Perfboard_IO:ESC_S_NC_GND',350.52,111.76,0),
      ('J3','Perfboard_IO:ESC_S_NC_GND',350.52,200.66,0),
      ('J4','Perfboard_IO:ESC_S_NC_GND',284.48,248.92,0),
      ('TP1','Perfboard_IO:Solder_Point',50.8,35.56,0),('TP2','Perfboard_IO:Solder_Point',50.8,60.96,0)]
    pins={}; pn={}
    for ref,lib,cx,cy,ang in specs:
        for u in kids(libs[lib][0],'symbol'):
            for p in kids(u,'pin'):
                x,y=map(float,one(p,'at')[1:3]); a=math.radians(ang)
                key=ref+'.'+one(p,'number')[1]
                pins[key]=[round(cx+x*math.cos(a)-y*math.sin(a),5),round(cy-x*math.sin(a)-y*math.cos(a),5)]
                pn[key]=one(p,'name')[1]
    spec={'5V':{'TP1.1','A1.LO03','U2.3'},'GND':{'TP2.1','A1.LO01','U1.2','U2.4',*[f'J{i}.3' for i in range(1,5)]},'3V3':{'A1.LI02','U1.1'},
      'MISO':{'A1.LO14','U1.3'},'MOSI':{'A1.LI14','U1.4'},'SCK':{'A1.LI13','U1.5'},'CS':{'A1.LO13','U1.6'},'TX':{'A1.RO16','U2.1'},'RX':{'A1.RI15','U2.2'}}
    for i,h in enumerate(['RO05','RI05','RO04','RI04'],1): spec['PWM'+str(i)]={'A1.'+h,f'J{i}.1'}
    routes=[]
    def route(n,points):
        points=[pins.get(p,p) if isinstance(p,str) else p for p in points]
        for a,b in zip(points,points[1:]):
            if a!=b:
                assert a[0]==b[0] or a[1]==b[1],(n,a,b)
                routes.append((n,tuple(a),tuple(b)))
    for n,up,ap,x in [('MISO',3,'LO14',105.41),('MOSI',4,'LI14',102.87),('SCK',5,'LI13',107.95),('CS',6,'LO13',110.49)]:
        route(n,[f'U1.{up}',[x,pins[f'U1.{up}'][1]],[x,pins['A1.'+ap][1]],'A1.'+ap])
    route('GND',['TP2.1',[104.14,60.96],[104.14,220.98],[325.12,220.98],[325.12,73.66],[261.62,73.66],[261.62,50.8],'J1.3'])
    for dst,via in [('A1.LO01',[[104.14,101.6]]),('U1.2',[[106.68,157.48],[106.68,101.6],[104.14,101.6]]),('U2.4',[[254,200.66],[254,220.98]]),('J2.3',[[325.12,116.84]]),('J3.3',[[325.12,205.74]]),('J4.3',[[261.62,254],[261.62,220.98]])]: route('GND',[dst,*via])
    route('5V',['TP1.1',[109.22,35.56],[109.22,111.76],'A1.LO03'])
    route('5V',[[109.22,35.56],[307.34,35.56],[307.34,172.72],[254,172.72],[254,195.58],'U2.3'])
    route('3V3',['A1.LI02',[105.41,109.22],[105.41,152.4],'U1.1'])
    route('3V3',[[97.79,132.08],[105.41,132.08]])
    for n,h,u,x in [('TX','RO16',1,248.92),('RX','RI15',2,252.73)]:
        route(n,['A1.'+h,[x,pins['A1.'+h][1]],[x,pins[f'U2.{u}'][1]],f'U2.{u}'])
    for n,h,j,via in [('PWM1','RO05',1,[[246.38,121.92],[246.38,40.64]]),('PWM2','RI05',2,[[312.42,124.46],[312.42,106.68]]),('PWM3','RO04',3,[[317.5,116.84],[317.5,195.58]]),('PWM4','RI04',4,[[251.46,119.38],[251.46,243.84]])]: route(n,['A1.'+h,*via,f'J{j}.1'])
    flags=[('#FLG01',[83.82,35.56]),('#FLG02',[76.2,60.96]),('#FLG03',[97.79,132.08])]
    def on(p,a,b): return (a[0]==b[0]==p[0] and min(a[1],b[1])<=p[1]<=max(a[1],b[1])) or (a[1]==b[1]==p[1] and min(a[0],b[0])<=p[0]<=max(a[0],b[0]))
    seg=set()
    for n,a,b in routes:
        cuts={a,b}
        for nn,c,d in routes:
            if nn==n: cuts|={p for p in [c,d] if on(p,a,b)}
        cuts|={tuple(p) for _,p in flags if on(p,a,b)}
        for c,d in zip(sorted(cuts),sorted(cuts)[1:]): seg.add((n,c,d))
    deg=collections.Counter(p for _,a,b in seg for p in [a,b])
    rootid=ident('schematic'); chunks=[f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {rootid}) (paper "A3")', '(title_block (title "PERFBOARD 70x90 - PROVISIONAL MODULE WIRING") (date "2026-11-03") (rev "0.1 - NOT SOLDER READY"))','(lib_symbols']
    for lib in sorted(set(s[1] for s in specs)|{'ERC_Only:PWR_FLAG'}): chunks.append(libs[lib][1])
    chunks.append(')')
    for ref,lib,x,y,ang in specs+[(r,'ERC_Only:PWR_FLAG',*pt,0) for r,pt in flags]:
        physical=not ref.startswith('#'); inbom=physical and not ref.startswith('TP')
        value=next(p[2] for p in kids(libs[lib][0],'property') if p[1]=='Value')
        if ref=='TP1': value='FROM EXTERNAL LM2596 OUT+ (5V)'
        if ref=='TP2': value='FROM EXTERNAL LM2596 OUT-'
        fp='' if not physical else 'Drone_Perfboard:'+ref+'_PROVISIONAL'
        chunks.append(f'(symbol (lib_id {q(lib)}) (at {x} {y} {ang}) (unit 1) (in_bom {"yes" if inbom else "no"}) (on_board {"yes" if physical else "no"}) (dnp no) (uuid {ident(ref)})')
        tx,ty=(x,y-12.7) if ref!='A1' else (x,y-36.83)
        for k,v,yy in [('Reference',ref,ty),('Value',value,ty+2.54),('Footprint',fp,y)]:
            hide='hide' if k=='Footprint' or ref.startswith('#') and k=='Reference' else ''
            if ref.startswith('#'): yy=y-3.81 if k=='Value' else y
            chunks.append(f'(property {q(k)} {q(v)} (at {tx} {yy} {90 if ang in [90,270] else 0}) (effects (font (size 1.0 1.0)) {hide}))')
        chunks.append(f'(instances (project {q(NAME)} (path "/{rootid}" (reference {q(ref)}) (unit 1)))) )')
    for i,(n,a,b) in enumerate(sorted(seg)): chunks.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) (uuid {ident("wire"+str(i))}))')
    for p,d in deg.items():
        if d>=3: chunks.append(f'(junction (at {p[0]} {p[1]}) (diameter 0) (color 0 0 0 0) (uuid {ident("junction"+str(p))}))')
    used=set().union(*spec.values())
    for key,p in pins.items():
        if key not in used: chunks.append(f'(no_connect (at {p[0]} {p[1]}) (uuid {ident("nc"+key)}))')
    notes=[('LM2596 / LiPo / ESC POWER STAGES ARE OFF BOARD. INPUT = VERIFIED 5V ONLY.',20,18),('J1..J4: 1=S, 2=NC (NO BEC), 3=GND. Connector position does NOT define firmware motor mixer.',20,267),('SPI1 and USART1 default mapping; TIM4_REMAP=0. Reserve PA13/PA14 for SWD. BOOT0 existing jumper=0.',20,274),('INT1 / INT2 / SDX / SCX unused: verify V1.3 auxiliary interface before changing. PA0 unused.',20,281),('All module footprint dimensions and header coordinates are ASSUMED. NOT READY TO SOLDER.',20,288)]
    for i,(s,x,y) in enumerate(notes): chunks.append(f'(text {q(s)} (at {x} {y} 0) (effects (font (size 1.15 1.15)) (justify left)) (uuid {ident("note"+str(i))}))')
    chunks.append(')'); (OUT/(NAME+'.kicad_sch')).write_text('\n'.join(chunks),encoding='utf-8')
    libslist=['DevKit_STM32','BMI270_DevKit','SB_Nano_2G4RX_Physical','ERC_Only','Perfboard_IO']
    (OUT/'sym-lib-table').write_text('(sym_lib_table (version 7)\n'+'\n'.join(f'(lib (name {q(l)}) (type "KiCad") (uri "${{KIPRJMOD}}/{l}.kicad_sym") (options "") (descr "Project local"))' for l in libslist)+'\n)',encoding='utf-8')
    (OUT/(NAME+'.kicad_pro')).write_text(json.dumps({'meta':{'filename':NAME+'.kicad_pro','version':1}},indent=2))
    # The following geometry is an explicitly assumed engineering model, not measurements.
    pads={}; parts={}
    def pad(ref,num,c,r):
        pads[ref+'.'+str(num)]={'ref':ref,'pad':str(num),'column':c,'row':r,'hole':hole(c,r),'xy':xy(c,r),'name':pn.get(ref+'.'+str(num),'')}
    for row in csv.DictReader((BASE/'hardware/kicad/DevKit_STM32_headers.csv').open()):
        n=row['position_id']; idx=int(row['row_from_top_or_left'])
        if n in ['BOOT0','BOOT1']: c,r=9,11 if n=='BOOT0' else 13
        elif n[:2] in ['RO','RI']: c,r=idx+4,5 if n[:2]=='RO' else 6
        elif n[:2] in ['LO','LI']: c,r=idx+4,17 if n[:2]=='LO' else 16
        elif n[:2] in ['BO','BI']: c,r=25 if n[:2]=='BO' else 24,17-idx
        elif n.startswith('S'): c,r=7,13-idx
        else: c,r=9,11 if n=='BOOT0' else 13
        pad('A1',n,c,r)
    for i in range(1,8): pad('U1',i,12+i,20)
    for i in range(8,11): pad('U1',i,i+7,28)
    for i in range(1,5): pad('U2',i,10-i,25)
    for i,c,r in [(1,3,2),(2,23,2),(3,23,34),(4,3,34)]:
        for j in range(1,4): pad('J'+str(i),j,c+j-1,r)
    pad('TP1',1,4,22); pad('TP2',1,6,22)
    for ref,box in {'A1':[3,4,26,18],'U1':[12,21,20,27],'U2':[5,26,10,30], 'J1':[2.5,1.3,5.5,2.7],'J2':[22.5,1.3,25.5,2.7],'J3':[22.5,33.3,25.5,34.7],'J4':[2.5,33.3,5.5,34.7]}.items(): parts[ref]={'body':[xy(*box[:2]),xy(*box[2:])],'mechanical_status':'ASSUMED_NOT_VERIFIED'}
    colors={'5V':'#d62828','3V3':'#e87500','GND':'#151c24','MISO':'#8a43c3','MOSI':'#138a62','SCK':'#2474c7','CS':'#886330','TX':'#008b9a','RX':'#2f77a4','PWM1':'#c99000','PWM2':'#a64ac9','PWM3':'#d44a86','PWM4':'#62717f'}
    wires=[]
    def w(n,a,b,way=()): wires.append({'id':f'W{len(wires)+1:02d}','signal':n,'from':a,'to':b,'color':colors[n],'side':'B / solder side','waypoints':list(way)})
    w('5V','TP1.1','A1.LO03',[(7.5,22),(7.5,17)])
    w('5V','TP1.1','U2.3',[(4,23.5),(7,23.5)])
    w('GND','TP2.1','A1.LO01',[(5,22)])
    w('GND','TP2.1','U2.4')
    w('GND','A1.LO01','U1.2',[(5,18.75),(14,18.75)])
    w('3V3','A1.LI02','U1.1',[(5.75,19.25),(13,19.25)])
    for n,a,b in [('CS','LO13',6),('SCK','LI13',5),('MISO','LO14',3),('MOSI','LI14',4)]: w(n,'A1.'+a,'U1.'+str(b))
    w('TX','A1.RO16','U2.1',[(21.5,5),(21.5,18.5),(10,18.5),(10,24.5)])
    w('RX','U2.2','A1.RI15',[(9.5,24.25),(9.5,18.25),(21.25,18.25),(21.25,6)])
    for i,h,way,gway in [(1,'RO05',[(9,3.5),(3,3.5)],[(2,17),(2,3.25),(5,3.25)]),(2,'RI05',[(9,3.75),(23,3.75)],[(26.5,18.75),(26.5,3.25),(25,3.25)]),(3,'RO04',[(8,3.25),(26,3.25),(26,32.5),(23,32.5)],[(25.5,19.25),(25.5,32.75),(25,32.75)]),(4,'RI04',[(8,3),(1,3),(1,32.5),(3,32.5)],[(1.5,17),(1.5,33),(5,33)])]:
        w('PWM'+str(i),'A1.'+h,f'J{i}.1',way); w('GND','A1.LO01',f'J{i}.3',gway)
    data={'name':NAME,'root_uuid':rootid,'spec':{k:sorted(v) for k,v in spec.items()},'pins':pn,'pads':pads,'parts':parts,'colors':colors,'wires':wires,'grid':{'pitch_assumed':P,'columns_assumed':27,'rows_assumed':35,'first_hole_assumed':[1.98,1.82],'board_size_user':[70,90]},'mechanical_status':'PROVISIONAL - NOT SOLDER READY','reference_image':'C:/Users/ASUS/Downloads/đi dây.png'}
    (OUT/'design.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    print('Created controller schematic and provisional geometry',OUT)

def stage_board():
    import pcbnew as k
    d=json.loads((OUT/'design.json').read_text()); pads=d['pads']
    import xml.etree.ElementTree as ET
    xml=ET.parse(OUT/'validation/controller.net.xml').getroot()
    values={c.attrib['ref']:c.findtext('value') for c in xml.findall('./components/comp')}
    pin_nets={n.attrib['ref']+'.'+n.attrib['pin']:net.attrib['name'] for net in xml.findall('./nets/net') for n in net.findall('node')}
    actual_groups={frozenset(n.attrib['ref']+'.'+n.attrib['pin'] for n in net.findall('node')) for net in xml.findall('./nets/net') if len(net.findall('node'))>1}
    assert actual_groups=={frozenset(v) for v in d['spec'].values()}
    b=k.BOARD(); v=lambda p:k.VECTOR2I(k.FromMM(p[0]),k.FromMM(p[1]))
    enabled=b.GetEnabledLayers()
    for i in range(1,18): enabled.AddLayer(getattr(k,'User_'+str(i)))
    b.SetEnabledLayers(enabled)
    layer_names={1:'Hole_Grid_ASSUMED',2:'Module_Envelope_ASSUMED',3:'Antenna_Clearance_ASSUMED'}
    for i,n in enumerate(d['colors'],4): layer_names[i]='B_Wire_'+n
    for i,n in layer_names.items(): b.SetLayerName(getattr(k,'User_'+str(i)),n)
    nets={}
    for name in sorted(set(pin_nets.values())):
        nn=k.NETINFO_ITEM(b,name); b.Add(nn); nets[name]=nn
    def line(a,bb,layer,width=.15):
        s=k.PCB_SHAPE(b); s.SetShape(k.SHAPE_T_SEGMENT); s.SetStart(v(a)); s.SetEnd(v(bb)); s.SetLayer(layer); s.SetWidth(k.FromMM(width)); b.Add(s)
    def text(s,p,layer,size=.8):
        t=k.PCB_TEXT(b); t.SetText(s); t.SetPosition(v(p)); t.SetTextSize(v([size,size])); t.SetTextThickness(k.FromMM(.13)); t.SetLayer(layer); b.Add(t)
    for a,bb in [([0,0],[70,0]),([70,0],[70,90]),([70,90],[0,90]),([0,90],[0,0])]: line(a,bb,k.Edge_Cuts,.05)
    pretty=OUT/'Drone_Perfboard.pretty'; pretty.mkdir(exist_ok=True)
    refs=sorted(set(p['ref'] for p in pads.values()))
    footprint_info={}
    for ref in refs:
        fp=k.FOOTPRINT(b); fp.SetReference(ref); fp.SetValue(values[ref]); fp.SetFPID(k.LIB_ID('Drone_Perfboard',ref+'_PROVISIONAL'))
        fp.SetAttributes(k.FP_THROUGH_HOLE | (k.FP_EXCLUDE_FROM_BOM if ref.startswith('TP') else 0))
        path=k.KIID_PATH(); path.push_back(k.KIID(d['root_uuid'])); path.push_back(k.KIID(ident(ref))); fp.SetPath(path)
        fp.Reference().SetVisible(False); fp.Value().SetVisible(False)
        for key,p in pads.items():
            if p['ref']!=ref: continue
            pa=k.PAD(fp); pa.SetNumber(p['pad']); pa.SetAttribute(k.PAD_ATTRIB_PTH); pa.SetShape(k.PAD_SHAPE_CIRCLE); pa.SetSize(v([1.65,1.65])); pa.SetDrillSize(v([.95,.95])); pa.SetLayerSet(k.LSET.AllCuMask()); pa.SetPosition(v(p['xy'])); pa.SetNet(nets[pin_nets[key]]); fp.Add(pa)
        if ref in d['parts']:
            aa,bb=d['parts'][ref]['body']
            for a,z in [(aa,[bb[0],aa[1]]),([bb[0],aa[1]],bb),(bb,[aa[0],bb[1]]),([aa[0],bb[1]],aa)]:
                sh=k.PCB_SHAPE(fp); sh.SetShape(k.SHAPE_T_SEGMENT); sh.SetStart(v(a)); sh.SetEnd(v(z)); sh.SetLayer(k.User_2); sh.SetWidth(k.FromMM(.2)); fp.Add(sh)
        b.Add(fp)
        plugin=k.PCB_IO_MGR.FindPlugin(k.PCB_IO_MGR.KICAD_SEXP)
        plugin.FootprintSave(str(pretty),fp)
        footprint_info[ref]={'pads':sum(p['ref']==ref for p in pads.values()),'verified':False}
    # All unused holes remain separate copper islands. Board-only substrate model.
    occupied={tuple(p['xy']) for p in pads.values()}
    grid=k.FOOTPRINT(b); grid.SetReference('GRID'); grid.SetValue('ASSUMED_PERFBOARD_SUBSTRATE'); grid.SetAttributes(k.FP_BOARD_ONLY|k.FP_EXCLUDE_FROM_BOM|k.FP_EXCLUDE_FROM_POS_FILES); grid.Reference().SetVisible(False); grid.Value().SetVisible(False)
    for r in range(1,36):
        for c in range(1,28):
            pos=xy(c,r)
            if tuple(pos) in occupied: continue
            pa=k.PAD(grid); pa.SetNumber(''); pa.SetAttribute(k.PAD_ATTRIB_PTH); pa.SetShape(k.PAD_SHAPE_CIRCLE); pa.SetSize(v([1.65,1.65])); pa.SetDrillSize(v([.95,.95])); pa.SetLayerSet(k.LSET.AllCuMask()); pa.SetPosition(v(pos)); grid.Add(pa)
    b.Add(grid)
    for c in range(1,28): text(f'C{c:02}',xy(c,0),k.User_1,.65)
    for r in range(1,36): text(f'R{r:02}',[-1.3,xy(1,r)[1]],k.User_1,.65)
    text('FRONT / MUI DRONE ^',[35,-5],k.User_2,1.1)
    text('USB < / A1 ASSUMED',[35,24],k.User_2,1)
    text('PROVISIONAL - NOT SOLDER READY',[35,94],k.User_2,1)
    for ref,p in d['parts'].items():
        aa,bb=p['body']; text(ref,[(aa[0]+bb[0])/2,(aa[1]+bb[1])/2],k.User_2,1.1)
    for ref in ['TP1','TP2']:
        pp=pads[ref+'.1']['xy']; text(ref+(' 5V IN' if ref=='TP1' else ' GND IN'),[pp[0],pp[1]+1.4],k.User_2,.75)
    for i in range(1,5):
        pp=pads[f'J{i}.1']['xy']; text(f'ESC{i}: S NC GND',[pp[0]+2.54,pp[1]+(2.6 if i<3 else -2.6)],k.User_2,.7)
    # A* on quarter-hole pitch: pad centres other than endpoints are obstacles.
    # Graphic crossings are insulated, never interpreted as electrical joints.
    padobs={(4*(p['column']-1),4*(p['row']-1)) for p in pads.values()}
    edges=set()
    def route_seg(a,z,allowed):
        pq=[(0,0,a)]; came={}; costs={a:0}; bounds=(-2,106,-2,138)
        while pq:
            _,g,u=heapq.heappop(pq)
            if u==z:
                path=[u]
                while u!=a: u=came[u]; path.append(u)
                return path[::-1]
            if g!=costs[u]: continue
            for dx,dy in [(1,0),(-1,0),(0,1),(0,-1)]:
                vv=(u[0]+dx,u[1]+dy)
                if not(bounds[0]<=vv[0]<=bounds[1] and bounds[2]<=vv[1]<=bounds[3]): continue
                if vv in padobs and vv not in allowed: continue
                edge=tuple(sorted((u,vv))); ng=g+1+(12 if edge in edges else 0)
                if ng<costs.get(vv,1e20):
                    costs[vv]=ng; came[vv]=u; heapq.heappush(pq,(ng+abs(vv[0]-z[0])+abs(vv[1]-z[1]),ng,vv))
        raise RuntimeError('No graphical jumper route')
    def gpos(c,r): return (round((c-1)*4),round((r-1)*4))
    for wire in d['wires']:
        p1,p2=pads[wire['from']],pads[wire['to']]
        start=gpos(p1['column'],p1['row']); end=gpos(p2['column'],p2['row'])
        ways=[start]+[gpos(c,r) for c,r in wire['waypoints']]+[end]; path=[start]
        for a,z in zip(ways,ways[1:]):
            part=route_seg(a,z,{start,end}); path+=part[1:]
            for u,vv in zip(part,part[1:]): edges.add(tuple(sorted((u,vv))))
        compact=[path[0]]
        for a,u,z in zip(path,path[1:],path[2:]):
            if (u[0]-a[0],u[1]-a[1])!=(z[0]-u[0],z[1]-u[1]): compact.append(u)
        compact.append(path[-1]); points=[xy(1+a/4,1+z/4) for a,z in compact]
        layer=getattr(k,'User_'+str(4+list(d['colors']).index(wire['signal'])))
        for a,z in zip(points,points[1:]): line(a,z,layer,.28 if wire['signal'] in ['5V','GND'] else .2)
        wire['points']=points; wire['length_mm']=round(sum(abs(a[0]-z[0])+abs(a[1]-z[1]) for a,z in zip(points,points[1:])),2)
    # Antenna corridor is only a placement reservation, not an invented RF footprint.
    for a,z in [([14,78],[27,78]),([27,78],[27,90]),([27,90],[14,90]),([14,90],[14,78])]: line(a,z,k.User_3,.2)
    text('ANTENNA OUT / VERIFY',[20.5,85],k.User_3,.65)
    b.GetDesignSettings().SetGridOrigin(v(xy(1,1))); b.GetDesignSettings().SetAuxOrigin(v(xy(1,1)))
    b.SetTitleBlock(k.TITLE_BLOCK()); b.GetTitleBlock().SetTitle('PERFBOARD 70x90 - ASSUMED, NOT SOLDER READY')
    k.SaveBoard(str(OUT/(NAME+'.kicad_pcb')),b)
    (OUT/'fp-lib-table').write_text('(fp_lib_table (version 7) (lib (name "Drone_Perfboard") (type "KiCad") (uri "${KIPRJMOD}/Drone_Perfboard.pretty") (options "") (descr "PROVISIONAL geometry, not measured")))')
    d['layers']=layer_names; d['footprints']=footprint_info
    (OUT/'design.json').write_text(json.dumps(d,indent=2),encoding='utf-8')
    print('Saved editable PCB with',len(pads),'mapped pads, graphics only;',len(d['wires']),'jumpers')

if __name__=='__main__':
    {'schematic':stage_schematic,'board':stage_board}[sys.argv[1]]()
