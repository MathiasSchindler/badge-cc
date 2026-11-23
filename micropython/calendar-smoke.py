import gc
import time


cases = (
    (0, (1970, 1, 1, 0, 0, 0, 3, 1)),
    (951782400, (2000, 2, 29, 0, 0, 0, 1, 60)),
    (1583020799, (2020, 2, 29, 23, 59, 59, 5, 60)),
    (1609459200, (2021, 1, 1, 0, 0, 0, 4, 1)),
    (2147483647, (2038, 1, 19, 3, 14, 7, 1, 19)),
)

for seconds, expected in cases:
    assert time.gmtime(seconds) == expected
    assert time.mktime(expected) == seconds
assert time.mktime((2020, 3, 0, 0, 0, 0, 0, 0)) == 1582934400


def calendar_ticks():
    before = time.ticks_us()
    digest = 0
    for _ in range(250):
        for seconds, _ in cases:
            parts = time.gmtime(seconds)
            digest ^= time.mktime(parts) ^ seconds
    elapsed = time.ticks_diff(time.ticks_us(), before)
    assert digest == 0
    return elapsed


gc.collect()
print("MP_CALENDAR_OK", ",".join(str(calendar_ticks()) for _ in range(7)))
