import gc
import time
from math import sqrt


def total(n):
    checksum = 0
    kept = []
    for i in range(n):
        x = (7 * i + 3) & 255
        if i & 7 == 0:
            kept.append((i, x))
        try:
            if x & 15 == 0:
                raise ValueError(x)
            checksum += x
        except ValueError as exc:
            checksum += 3 * exc.args[0]
        finally:
            checksum += i & 3
    return checksum, kept


assert sqrt(81.0) == 9.0
assert total(64) == (8448, [(0, 3), (8, 59), (16, 115), (24, 171),
                            (32, 227), (40, 27), (48, 83), (56, 139)])

samples = []
for _ in range(7):
    gc.collect()
    start = time.ticks_us()
    result, kept = total(4096)
    samples.append(time.ticks_diff(time.ticks_us(), start))
    gc.collect()
    assert result == 589824
    assert len(kept) == 512 and kept[0] == (0, 3) and kept[-1] == (4088, 203)

print("MP_INTERPRETER_OK", ",".join(str(ticks) for ticks in samples))
