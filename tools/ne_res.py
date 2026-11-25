#!/usr/bin/env python3
"""NE (Win16) resource table reader. Usage: ne_res.py CHIPS.EXE [outdir]
Lists resources; with outdir, dumps BITMAP resources as .bmp files (DIB + BMP header)."""
import struct, sys, os

RT = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',14:'GROUP_ICON',16:'VERSION'}

def parse(data):
    assert data[:2] == b'MZ'
    ne = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[ne:ne+2] == b'NE'
    rt_off = struct.unpack_from('<H', data, ne+0x24)[0] + ne
    p = rt_off
    shift = struct.unpack_from('<H', data, p)[0]; p += 2
    res = []
    while True:
        tid = struct.unpack_from('<H', data, p)[0]
        if tid == 0: break
        cnt = struct.unpack_from('<H', data, p+2)[0]
        p += 8
        for _ in range(cnt):
            off, ln, flags, rid = struct.unpack_from('<HHHH', data, p); p += 12
            isint = bool(tid & 0x8000)
            res.append(dict(type=tid & 0x7fff if tid & 0x8000 else tid,
                            id=rid & 0x7fff if rid & 0x8000 else rid,
                            intid=bool(rid & 0x8000),
                            data=data[off << shift:(off << shift) + (ln << shift)]))
    return res

def dib_to_bmp(d):
    hs = struct.unpack_from('<I', d, 0)[0]
    bpp = struct.unpack_from('<H', d, 14)[0]
    ncol = struct.unpack_from('<I', d, 32)[0] if hs >= 40 else 0
    if hs >= 40 and ncol == 0 and bpp <= 8: ncol = 1 << bpp
    off = 14 + hs + ncol * 4
    return b'BM' + struct.pack('<IHHI', 14 + len(d), 0, 0, off) + d

if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    res = parse(data)
    out = sys.argv[2] if len(sys.argv) > 2 else None
    if out: os.makedirs(out, exist_ok=True)
    for r in res:
        name = RT.get(r['type'], str(r['type']))
        info = ''
        if r['type'] == 2:
            w, h = struct.unpack_from('<ii', r['data'], 4); bpp = struct.unpack_from('<H', r['data'], 14)[0]
            info = f'{w}x{h} {bpp}bpp'
            if out: open(os.path.join(out, f"bmp_{r['id']}.bmp"), 'wb').write(dib_to_bmp(r['data']))
        elif out and r['type'] == 6:
            open(os.path.join(out, f"string_{r['id']}.bin"), 'wb').write(r['data'])
        print(name, r['id'], len(r['data']), info)
