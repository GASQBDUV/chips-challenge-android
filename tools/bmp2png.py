import sys
from PIL import Image
for f in sys.argv[1:]:
    Image.open(f).convert('RGB').save(f.rsplit('.',1)[0]+'.png')
