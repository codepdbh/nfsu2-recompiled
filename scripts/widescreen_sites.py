"""Locate the NFSU2 WidescreenFix (ThirteenAG) patch sites in a SPEED2.EXE.

Prints each pattern hit with its disassembly; config/hooks.txt and
src/runtime/widescreen.c were derived from this output for the pinned build.
Usage: python scripts/widescreen_sites.py work/SPEED2.analysis.exe
"""
import pefile,re,sys,struct
from capstone import *
pe=pefile.PE(sys.argv[1]);base=pe.OPTIONAL_HEADER.ImageBase
img=pe.get_memory_mapped_image()
def find(p):
    rx=b''.join(b'.' if t=='?' else re.escape(bytes([int(t,16)])) for t in p.split())
    return [base+m.start() for m in re.finditer(rx,img,re.S)]
md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
def dis(va,n=1):
    out=[]
    for i in md.disasm(img[va-base:va-base+64],va):
        out.append(i)
        if len(out)==n:break
    return out
pats={
 'hudScaleX':"D9 05 ? ? ? ? 89 7C 24 28 C7 44 24 2C 00 00 00 3F D8 C9 C7 44 24 30 00 00 00 3F",
 'pos1':"C7 84 24 A0 00 00 00 00 00 A0 43 C7 84 24 A4 00 00 00 00 00 70 43 C7 84 24 A8 00 00 00 00 00 00 00 0F B7 48 20",
 'pos2':"C7 44 24 74 00 00 A0 43 C7 44 24 78 00 00 70 43 C7 44 24 7C 00 00 00 00 E8 ? ? ? ? 8D 4C 24 70",
 'pos3':"C7 84 24 84 00 00 00 00 00 A0 43 C7 84 24 88 00 00 00 00 00 70 43 C7 84 24 8C 00 00 00 00 00 00 00 E8 ? ? ? ? 8D 8C 24 80 00 00 00",
 'pos4':"C7 84 24 94 00 00 00 00 00 A0 43 C7 84 24 98 00 00 00 00 00 70 43 C7 84 24 9C 00 00 00 00 00 00 00 E8 ? ? ? ? 8D 8C 24 90 00 00 00",
 'pos5':"C7 44 24 74 00 00 A0 43 C7 44 24 78 00 00 70 43 C7 44 24 7C 00 00 00 00 E8 ? ? ? ? 8D 54 24 70 52",
 'pos6':"C7 05 ? ? ? ? 00 00 A0 43 C7 05 ? ? ? ? 00 00 00 00 C7 05 ? ? ? ? 00 00 00 00 C6 05 ? ? ? ? 82",
 'pos7':"C7 84 24 A0 01 00 00 00 00 A0 43 C7 84 24 A4 01 00 00 00 00 70 43 C7 84 24 A8 01 00 00 00 00 00 00 E8 ? ? ? ? 8D 8C 24 30 01 00 00 51 8B F0",
 'posRef':"D8 25 ? ? ? ? 8B 4C 24 28 6A 00 8D 54 24 48 D9 5C 24 48 89 4C 24 50 D9 44 24 28 8B 0B",
 'posX2':"D8 0D ? ? ? ? D9 5C 24 ? D9 44 24 ? D8 74 24",
 'mirror':"C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? 33 D2",
 'borders12':"C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? E8 ? ? ? ? 83 C4 ? 5F",
 'borders34':"C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? E8 ? ? ? ? BA",
 'radar':"C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? 7D",
 'dyno':"6A ? 68 ? ? ? ? 68 ? ? ? ? 8B C8 E8 ? ? ? ? EB ? 33 C0 8B CF",
 'hudHook':"89 4C 24 60 89 54 24 64 74 ? 8D 8C 24 F0 00 00 00 51",
 'blips':"8B 4B 1C 8B 54 24 18 89 0A 8B 43 20",
 'hud2':"D9 56 48 8B 4E 1C D8 2D ? ? ? ? 89 4C 24 28 8B 56 20 89 54 24 2C 8B 46 24",
 'stop':"D8 02 D9 1E D9 41 04 D8 60 04 D8 4C 24 10 D8 42 04 D8 40 04 D9 5E 04 D9 41 08 D8 60 08",
 'fmv':"68 00 00 00 3F 68 00 00 00 3F 68 00 00 00 BF 68 00 00 00 BF 8B CB E8 ? ? ? ? 8B 44 24 18 8B CB",
 'fov':"DB 40 18 C7 44 24 20 00 00 80 3F DA 70 14",
 'fovA':"D8 3D ? ? ? ? D9 5C 24 1C DB 44 24 2C D8 4C 24 28 D8 0D ? ? ? ? E8",
 'fovB':"D8 0D ? ? ? ? E8 ? ? ? ? 8B D8 53",
 'fovC':"D8 3D ? ? ? ? D9 5C 24 38 D9 44 24 24 D8 64 24 30 D8 7C 24 24 D9 5C 24 34",
}
for k,p in pats.items():
    hits=find(p);print(f"== {k}: {[hex(h) for h in hits]}")
    for h in hits[:2]:
        for i in dis(h-6 if k=='fov' else h,14 if k in('mirror','borders12','borders34','radar','fov') else 6):
            print(f"   {i.address:08X}: {i.mnemonic} {i.op_str}")
