import gc
import time


def signed_arithmetic():
    cases = (
        (7, 3, 2, 1),
        (-7, 3, -3, 2),
        (7, -3, -3, -2),
        (-7, -3, 2, -1),
        (-9, 3, -3, 0),
        (0, -3, 0, 0),
        (-1073741824, 3, -357913942, 2),
        (1073741823, -3, -357913941, 0),
    )
    for a, b, q, r in cases:
        assert (a // b, a % b, divmod(a, b)) == (q, r, (q, r))
    for divisor in (0,):
        try:
            divmod(cases[0][0], divisor)
        except ZeroDivisionError:
            pass
        else:
            raise AssertionError("missing ZeroDivisionError")


def smallint_ticks():
    pairs = ((71, 3), (-71, 3), (71, -3), (-71, -3))
    digest = 0
    before = time.ticks_us()
    for _ in range(2500):
        for a, b in pairs:
            digest += a // b + a % b
    ticks = time.ticks_diff(time.ticks_us(), before)
    assert digest == -5000
    return ticks


signed_arithmetic()
gc.collect()
ticks = [smallint_ticks() for _ in range(7)]
print("MP_SMALLINT_OK", ",".join(str(t) for t in ticks))
