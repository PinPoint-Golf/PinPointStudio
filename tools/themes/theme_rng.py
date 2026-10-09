"""The app's random numbers, bit for bit: std::mt19937_64 as pinpoint::analysis::DetRng uses it.

The C++ port of the themes (src/Analysis/swing_themes.h) is tested against this prototype's
output, so the parallel-analysis shuffles and the bootstrap resamples must draw the SAME numbers
in both languages. numpy's generators are not std::mt19937_64; this is.

  * seed(value)      — the reference init_genrand64 (what std::mt19937_64(value) does).
  * raw()            — one 64-bit word; raw_array(n) the next n words at once (numpy uint64).
  * below(n)         — raw() % n, exactly DetRng::below.
  * fisher_yates(a)  — in place: for i = n-1 down to 1, j = below(i + 1), swap a[i], a[j].
  * seed_for(stream, index) — one seed per replicate, so replicates may run in any order:
        ((20261009 * 1000003 + stream) * 1000003 + index) mod 2^64.

The state is twisted a block of 312 words at a time with numpy (three vector steps), so the
500 parallel-analysis shuffles of a 133 × 46 table take a second or two.

    /opt/homebrew/bin/python3 -I -B tools/themes/theme_rng.py      # self-check
"""
from __future__ import annotations

import numpy as np

NN, MM = 312, 156
MATRIX_A = np.uint64(0xB5026F5AA96619E9)
UM = np.uint64(0xFFFFFFFF80000000)   # most significant 33 bits
LM = np.uint64(0x7FFFFFFF)           # least significant 31 bits
MASK64 = (1 << 64) - 1

SEED_BASE = 20261009
STREAM_PA = 1
STREAM_BOOT = 2


def seed_for(stream: int, index: int, base: int = SEED_BASE) -> int:
    return ((base * 1000003 + stream) * 1000003 + index) & MASK64


class MT19937_64:
    def __init__(self, seed: int = 5489):
        self.seed(seed)

    def seed(self, value: int) -> None:
        mt = [0] * NN
        mt[0] = value & MASK64
        for i in range(1, NN):
            mt[i] = (6364136223846793005 * (mt[i - 1] ^ (mt[i - 1] >> 62)) + i) & MASK64
        self._mt = np.array(mt, dtype=np.uint64)
        self._out = np.empty(0, dtype=np.uint64)
        self._pos = 0

    def _twist(self) -> None:
        mt = self._mt
        one, s1, s29, s17, s37, s43 = (np.uint64(v) for v in (1, 1, 29, 17, 37, 43))
        # i = 0 .. 155: mt[i + 1] and mt[i + 156] are still the old words
        x = (mt[:NN - MM] & UM) | (mt[1:NN - MM + 1] & LM)
        mt[:NN - MM] = mt[MM:] ^ (x >> s1) ^ np.where((x & one) != 0, MATRIX_A, np.uint64(0))
        # i = 156 .. 310: mt[i + 1] old, mt[i - 156] already new
        x = (mt[NN - MM:NN - 1] & UM) | (mt[NN - MM + 1:NN] & LM)
        mt[NN - MM:NN - 1] = mt[:MM - 1] ^ (x >> s1) ^ np.where((x & one) != 0, MATRIX_A,
                                                              np.uint64(0))
        # i = 311: wraps onto the new mt[0]
        x = (mt[NN - 1] & UM) | (mt[0] & LM)
        mt[NN - 1] = mt[MM - 1] ^ (x >> s1) ^ (MATRIX_A if int(x) & 1 else np.uint64(0))
        # tempering, the whole block at once
        y = mt.copy()
        y ^= (y >> s29) & np.uint64(0x5555555555555555)
        y ^= (y << s17) & np.uint64(0x71D67FFFEDA60000)
        y ^= (y << s37) & np.uint64(0xFFF7EEE000000000)
        y ^= y >> s43
        self._out = y
        self._pos = 0

    def raw_array(self, n: int) -> np.ndarray:
        """The next n raw words, in order."""
        parts, need = [], n
        while need > 0:
            if self._pos >= len(self._out):
                self._twist()
            take = min(need, len(self._out) - self._pos)
            parts.append(self._out[self._pos:self._pos + take])
            self._pos += take
            need -= take
        return np.concatenate(parts) if parts else np.empty(0, dtype=np.uint64)

    def raw(self) -> int:
        if self._pos >= len(self._out):
            self._twist()
        v = int(self._out[self._pos])
        self._pos += 1
        return v

    def below(self, n: int) -> int:
        return self.raw() % n if n else 0

    def fisher_yates(self, a: list) -> list:
        """Shuffle `a` in place (and return it): i = n-1 .. 1, j = below(i + 1), swap."""
        n = len(a)
        if n < 2:
            return a
        js = (self.raw_array(n - 1) % np.arange(n, 1, -1, dtype=np.uint64)).tolist()
        for t, i in enumerate(range(n - 1, 0, -1)):
            j = js[t]
            a[i], a[j] = a[j], a[i]
        return a

    def permutation_index(self, n: int) -> np.ndarray:
        """fisher_yates applied to [0, 1, .., n-1]; `col[perm]` is the shuffled column."""
        return np.array(self.fisher_yates(list(range(n))), dtype=int)


if __name__ == "__main__":
    import time
    g = MT19937_64()
    first = g.raw()
    g2 = MT19937_64(5489)
    w = g2.raw_array(10000)
    ok1 = first == 14514284786278117030
    ok2 = int(w[-1]) == 9981545732273789042
    # raw() and raw_array() walk the same stream
    g3 = MT19937_64(seed_for(STREAM_PA, 7))
    a = [g3.raw() for _ in range(700)]
    b = MT19937_64(seed_for(STREAM_PA, 7)).raw_array(700).tolist()
    ok3 = a == b
    print(f"first output {first} {'OK' if ok1 else 'WRONG'}")
    print(f"10000th output {int(w[-1])} {'OK' if ok2 else 'WRONG'}")
    print(f"raw() == raw_array() {'OK' if ok3 else 'WRONG'}")
    print(f"seed_for(1, 0) = {seed_for(1, 0)}, seed_for(2, 499) = {seed_for(2, 499)}")
    t0 = time.time()
    for i in range(500):
        r = MT19937_64(seed_for(STREAM_PA, i))
        for _ in range(46):
            r.permutation_index(133)
    print(f"500 x 46 shuffles of 133: {time.time() - t0:.2f} s")
    raise SystemExit(0 if ok1 and ok2 and ok3 else 1)
