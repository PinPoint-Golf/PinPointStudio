# overflow_check.py <pages dir> — flags any page whose content reaches into the folio band (between the
# content box's foot, y = 1053 logical, and the folio rule at y = 1069). Pages are 794 logical px wide.
import sys, glob
from PIL import Image
bad = []
for f in sorted(glob.glob(sys.argv[1] + '/p*.png')):
    im = Image.open(f).convert('L'); k = im.width / 794
    band = im.crop((int(60 * k), int(1054 * k), int(734 * k), int(1067 * k)))
    if min(band.getdata()) < 140: bad.append(f.split('/')[-1])
print('OVERFLOW into the folio:', ', '.join(bad) if bad else 'none')
sys.exit(1 if bad else 0)
