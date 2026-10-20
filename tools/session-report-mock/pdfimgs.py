# Extract Flate/DCT page images from a raster PDF (QPdfWriter output) -> PNGs, downscaled.
import re, sys, zlib, io
from PIL import Image
src, out, maxw = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1100
data = open(src, 'rb').read()
n = 0
for m in re.finditer(rb'<<([^>]*?/Subtype\s*/Image.*?)>>\s*stream\r?\n', data, re.S):
    d = m.group(1)
    w = int(re.search(rb'/Width\s+(\d+)', d).group(1)); h = int(re.search(rb'/Height\s+(\d+)', d).group(1))
    ln = re.search(rb'/Length\s+(\d+)(\s+0\s+R)?', d)
    start = m.end()
    end = data.index(b'endstream', start)
    raw = data[start:end]
    if b'DCTDecode' in d: im = Image.open(io.BytesIO(raw))
    else:
        px = zlib.decompress(raw)
        bpc = len(px) // (w * h)
        if bpc < 3: continue  # smask
        im = Image.frombytes('RGB', (w, h), px[:w*h*3])
    if w < 500: continue
    n += 1
    if im.width > maxw: im = im.resize((maxw, int(im.height * maxw / im.width)), Image.LANCZOS)
    im.save(f'{out}/p{n:02d}.png')
print(n, 'pages')
