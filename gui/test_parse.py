import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from aof2_gui import HandClass, HandParseError, Kind, parse_hand_input


def expect_ok(text: str, rh: int, rl: int, kind: Kind) -> None:
    h = parse_hand_input(text)
    assert h == HandClass(rh, rl, kind), f"{text!r} -> {h.label()} != expected"
    print(f"[OK]   parse({text!r:8}) -> {h.label()}  (idx={h.index()})")


def expect_fail(text: str, fragment: str = "") -> None:
    try:
        h = parse_hand_input(text)
    except HandParseError as e:
        msg = str(e)
        ok = (fragment.lower() in msg.lower()) if fragment else True
        status = "OK" if ok else "WRONG-MSG"
        print(f"[{status}] parse({text!r:10}) raised: {msg}")
        if not ok:
            raise AssertionError(f"expected fragment '{fragment}' in error message, got: {msg}")
        return
    raise AssertionError(f"expected error for {text!r}, but got {h.label()}")


expect_ok("AA",  12, 12, Kind.PAIR)
expect_ok("aa",  12, 12, Kind.PAIR)
expect_ok("Aa",  12, 12, Kind.PAIR)
expect_ok("KK",  11, 11, Kind.PAIR)
expect_ok("22",  0,  0,  Kind.PAIR)
expect_ok("TT",  8,  8,  Kind.PAIR)

expect_ok("AKs", 12, 11, Kind.SUITED)
expect_ok("aks", 12, 11, Kind.SUITED)
expect_ok("AkS", 12, 11, Kind.SUITED)
expect_ok("KAs", 12, 11, Kind.SUITED)
expect_ok("T9s", 8,  7,  Kind.SUITED)
expect_ok("76s", 5,  4,  Kind.SUITED)
expect_ok("32s", 1,  0,  Kind.SUITED)

expect_ok("AKo", 12, 11, Kind.OFFSUIT)
expect_ok("ako", 12, 11, Kind.OFFSUIT)
expect_ok("KAo", 12, 11, Kind.OFFSUIT)
expect_ok("AQO", 12, 10, Kind.OFFSUIT)
expect_ok("T9o", 8,  7,  Kind.OFFSUIT)
expect_ok("32o", 1,  0,  Kind.OFFSUIT)

expect_ok("  AA  ", 12, 12, Kind.PAIR)
expect_ok(" aks ",  12, 11, Kind.SUITED)

expect_fail("",         "empty")
expect_fail("A",        "length")
expect_fail("AKsx",     "length")
expect_fail("Z2",       "rank")
expect_fail("A1",       "rank")
expect_fail("110",      "rank")
expect_fail("AKp",      "must be 's' or 'o'")
expect_fail("AAo",      "pocket pair")
expect_fail("KKs",      "pocket pair")
expect_fail("22s",      "pocket pair")
expect_fail("AK",       "must end with")

print("\n[parse] all parser tests passed")
