import sys
from PIL import Image, ImageDraw
src, out, c0, c1 = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
im = Image.open(src).convert('RGB')
n = c1 - c0
sc = 2
o = Image.new('RGB', (n*32*sc+24, 512*sc+10), (255,0,255))
for c in range(c0, c1):
    t = im.crop((c*32, 0, c*32+32, 512)).resize((64, 1024), Image.NEAREST)
    o.paste(t, (24+(c-c0)*64, 10))
d = ImageDraw.Draw(o)
for r in range(16):
    d.text((2, 10+r*64+25), str(r), fill=(255,255,255))
    for c in range(n):
        d.text((24+c*64+2, 10+r*64+2), f'{r},{c+c0}', fill=(255,0,0))
o.save(out)
