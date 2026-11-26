#!/usr/bin/env python3
"""ppsw_peek - a minimal reader for a swing document (.ppsw: a zstd-compressed CBOR document), standard library only
(Python 3.14's compression.zstd). Used by tone_ab_inputs.py for the launch monitor's clubhead speed.

    python3 -I tools/swinglab/ppsw_peek.py <swing.ppsw> [key ...]   prints the top-level keys, then each key's value
"""
import struct, sys, json
from compression import zstd

def cbor(b, i=0):
    ib = b[i]; mt, ai = ib >> 5, ib & 31; i += 1
    if ai < 24: v = ai
    elif ai == 24: v = b[i]; i += 1
    elif ai == 25: v = struct.unpack('>H', b[i:i+2])[0]; i += 2
    elif ai == 26: v = struct.unpack('>I', b[i:i+4])[0]; i += 4
    elif ai == 27: v = struct.unpack('>Q', b[i:i+8])[0]; i += 8
    elif ai == 31: v = None
    else: raise ValueError('ai %d' % ai)
    if mt == 0: return v, i
    if mt == 1: return -1 - v, i
    if mt in (2, 3):
        if v is None:
            parts = []
            while b[i] != 0xff:
                p, i = cbor(b, i); parts.append(p)
            i += 1
            return (b'' if mt == 2 else '').join(parts), i
        s = b[i:i+v]; i += v
        return (bytes(s) if mt == 2 else s.decode('utf-8')), i
    if mt == 4:
        out = []
        if v is None:
            while b[i] != 0xff:
                x, i = cbor(b, i); out.append(x)
            return out, i + 1
        for _ in range(v):
            x, i = cbor(b, i); out.append(x)
        return out, i
    if mt == 5:
        out = {}
        if v is None:
            while b[i] != 0xff:
                k, i = cbor(b, i); x, i = cbor(b, i); out[k] = x
            return out, i + 1
        for _ in range(v):
            k, i = cbor(b, i); x, i = cbor(b, i); out[k] = x
        return out, i
    if mt == 6:
        return cbor(b, i)
    if mt == 7:
        if ai == 20: return False, i
        if ai == 21: return True, i
        if ai in (22, 23): return None, i
        if ai == 25:
            return struct.unpack('>e', b[i-2:i])[0], i
        if ai == 26: return struct.unpack('>f', b[i-4:i])[0], i
        if ai == 27: return struct.unpack('>d', b[i-8:i])[0], i
    raise ValueError('mt %d ai %d' % (mt, ai))

def read_doc(path):
    b = open(path, 'rb').read()
    i = b.find(b'\x28\xb5\x2f\xfd')
    raw = zstd.ZstdDecompressor().decompress(b[i:])
    d, _ = cbor(raw, 0)
    return d

if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit('usage: ppsw_peek.py <swing.ppsw> [key ...]')
    d = read_doc(sys.argv[1])
    print(list(d.keys()))
    for k in sys.argv[2:]:
        print(k, json.dumps(d.get(k), default=str)[:1500])
