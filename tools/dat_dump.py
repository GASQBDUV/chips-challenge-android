#!/usr/bin/env python3
"""Dump CHIPS.DAT summary. Usage: dat_dump.py CHIPS.DAT [level]"""
import struct, sys
d = open(sys.argv[1], 'rb').read()
magic, n = struct.unpack_from('<IH', d, 0)
print('magic %08x levels %d' % (magic, n))
p = 6
def rle(b):
    o = []; i = 0
    while i < len(b):
        if b[i] == 0xFF: o += [b[i+2]] * b[i+1]; i += 3
        else: o.append(b[i]); i += 1
    return o
for _ in range(n):
    sz, = struct.unpack_from('<H', d, p); rec = d[p+2:p+2+sz]; p += 2 + sz
    num, tl, chips, _x = struct.unpack_from('<HHHH', rec, 0)
    q = 8
    l1, = struct.unpack_from('<H', rec, q); up = rle(rec[q+2:q+2+l1]); q += 2 + l1
    l2, = struct.unpack_from('<H', rec, q); lo = rle(rec[q+2:q+2+l2]); q += 2 + l2
    ex, = struct.unpack_from('<H', rec, q); q += 2
    f = {}
    e = q + ex
    while q < e:
        t, l = rec[q], rec[q+1]; f[t] = rec[q+2:q+2+l]; q += 2 + l
    title = f[3].rstrip(b'\0').decode('latin1'); pw = bytes(x ^ 0x99 for x in f[6][:-1]).decode('latin1')
    print(num, 'time', tl, 'chips', chips, 'cells', len(up), len(lo), repr(title), pw, 'hint' in f and '' or '', 'mon', len(f.get(10, b''))//2, 'trap', len(f.get(4, b''))//10, 'clone', len(f.get(5, b''))//8)
