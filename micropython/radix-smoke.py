import gc
import time


def check_int():
    cases = (
        ("0b101", 0, 5), ("0B101", 2, 5), ("0o71", 0, 57),
        ("0O71", 8, 57), ("0xAf", 0, 175), ("-0XAf", 16, -175),
        ("+101", 2, 5), ("0", 0, 0), ("7", 0, 7), ("z", 36, 35),
        ("0b10", 16, 2832), ("10", 0, 10),
    )
    for text, base, expected in cases:
        assert int(text, base) == expected
    for text, base in (("", 0), ("0x", 0), ("0x1", 2), ("0b2", 0), ("0o9", 8)):
        try:
            int(text, base)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid radix accepted")
    return cases


def radix_ticks(cases):
    digest = 0
    before = time.ticks_us()
    for _ in range(500):
        for text, base, _ in cases:
            digest += int(text, base)
    elapsed = time.ticks_diff(time.ticks_us(), before)
    assert digest == 1506500
    return elapsed


cases = check_int()
phrase = "\u00e9\u20ac\U0001f600z"
assert len(phrase) == 4
assert ord(phrase[0]) == 233
assert ord(phrase[1]) == 8364
assert ord(phrase[2]) == 128512
assert phrase[1:3].encode() == b"\xe2\x82\xac\xf0\x9f\x98\x80"
assert " \t".isspace() and "F".isalpha() and "7".isdigit()
for _ in range(8):
    cohort = [bytearray(128) for _ in range(32)]
    cohort[0][31] = 23
    gc.collect()
    assert cohort[0][31] == 23
    del cohort
gc.collect()
print("MP_RADIX_OK", ",".join(str(radix_ticks(cases)) for _ in range(7)))
