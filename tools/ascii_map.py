import struct, sys
d = open(sys.argv[1], 'rb').read(); want = int(sys.argv[2])
n, = struct.unpack_from('<H', d, 4); p = 6
def rle(b):
    o = []; i = 0
    while i < len(b):
        if b[i] == 0xFF: o += [b[i+2]] * b[i+1]; i += 3
        else: o.append(b[i]); i += 1
    return o
SYM = {0:'.',1:'#',2:'c',3:'~',4:'^',5:'h',0xA:'B',0xB:'d',0xC:'i',0xD:'v',0x12:'n',0x13:'e',0x14:'w',0x15:'X',0x16:'1',0x17:'2',0x18:'3',0x19:'4',0x22:'S',0x23:'G',0x24:'R',0x25:'t',0x26:'T',0x27:'b',0x28:'u',0x29:'@',0x2A:'*',0x2B:'_',0x2C:'H',0x2D:'g',0x2E:'p',0x2F:'?',0x31:'C',0x21:'$',0x1E:'f',0x1F:'F',0x6E:'P',0x6C:'P',0x6D:'P',0x6F:'P'}
for _ in range(n):
    sz, = struct.unpack_from('<H', d, p); rec = d[p+2:p+2+sz]; p += 2 + sz
    num, = struct.unpack_from('<H', rec, 0)
    if num != want: continue
    q = 8; l1, = struct.unpack_from('<H', rec, q); up = rle(rec[q+2:q+2+l1]); q += 2 + l1
    l2, = struct.unpack_from('<H', rec, q); lo = rle(rec[q+2:q+2+l2])
    for y in range(32):
        row = ''
        for x in range(32):
            c = up[y*32+x] or lo[y*32+x]
            if 0x40 <= c <= 0x63: s = 'M'
            elif 0x64 <= c <= 0x67: s = 'k'
            elif 0x68 <= c <= 0x6B: s = 'o'
            else: s = SYM.get(c, '?' if c else '.')
            row += s
        print(row)
