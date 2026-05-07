import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from aof2_gui import HandClass, Kind, Strategy, RANKS

base = Path(__file__).resolve().parent.parent
strat = Strategy.load(base / "data" / "strategy.bin")

print(f"loaded: iters={strat.iterations:,} expl={strat.exploitability_bb:.3e} BB  SB EV={strat.sb_ev:+.4f} BB")
print(f"params: stack={strat.params.stack} SB={strat.params.sb_blind} BB={strat.params.bb_blind}")

print("\n[hand-class index round trip]")
seen = [False] * 169
for r in range(13):
    h = HandClass(r, r, Kind.PAIR)
    seen[h.index()] = True
for rh in range(1, 13):
    for rl in range(rh):
        for k in (Kind.SUITED, Kind.OFFSUIT):
            h = HandClass(rh, rl, k)
            seen[h.index()] = True
assert all(seen), "missing hand class indices"
print("169 indices covered")

print("\n[push range — SB]")
push_classes = []
for rh in range(13):
    for rl in range(13):
        if rh < rl: continue
        if rh == rl:
            h = HandClass(rh, rh, Kind.PAIR)
        else:
            for k in (Kind.SUITED, Kind.OFFSUIT):
                h = HandClass(rh, rl, k)
                p = strat.sb_push[h.index()]
                if p > 0.5:
                    push_classes.append((h.label(), p))
            continue
        p = strat.sb_push[h.index()]
        if p > 0.5:
            push_classes.append((h.label(), p))
push_pct = sum(
    (6 if h.kind is Kind.PAIR else (4 if h.kind is Kind.SUITED else 12)) * (1 if strat.sb_push[h.index()] > 0.5 else 0)
    for r in range(13) for rh in range(13) for rl in range(13)
    if (rh == rl and r == rh) or (rh > rl and 0 <= rl < rh < 13)
    for h in [HandClass(rh, rh, Kind.PAIR) if rh == rl and r == rh else HandClass(rh, rl, Kind.SUITED if r == 0 else Kind.OFFSUIT)]
    if (rh == rl and r == rh) or (rh > rl and r in (0, 1))
) / 1326

print(f"SB push as fractions of all combos:")
push_combos = 0
for h_idx in range(169):
    if h_idx < 13:
        h = HandClass(h_idx, h_idx, Kind.PAIR)
        n = 6
    elif h_idx < 91:
        off = h_idx - 13
        rh = 1
        while rh*(rh-1)//2 + (rh-1) < off:
            rh += 1
        rl = off - rh*(rh-1)//2
        h = HandClass(rh, rl, Kind.SUITED)
        n = 4
    else:
        off = h_idx - 91
        rh = 1
        while rh*(rh-1)//2 + (rh-1) < off:
            rh += 1
        rl = off - rh*(rh-1)//2
        h = HandClass(rh, rl, Kind.OFFSUIT)
        n = 12
    if strat.sb_push[h_idx] > 0.999:
        push_combos += n
    elif strat.sb_push[h_idx] > 0.001:
        push_combos += n * strat.sb_push[h_idx]
print(f"SB total push frequency: {push_combos / 1326 * 100:.2f}% of all 1326 combos")

call_combos = 0
for h_idx in range(169):
    if h_idx < 13:
        n = 6
    elif h_idx < 91:
        n = 4
    else:
        n = 12
    if strat.bb_call[h_idx] > 0.999:
        call_combos += n
    elif strat.bb_call[h_idx] > 0.001:
        call_combos += n * strat.bb_call[h_idx]
print(f"BB total call frequency: {call_combos / 1326 * 100:.2f}% of all 1326 combos")

print("\n[BB call mixed strategies — non-trivial cells]")
for h_idx in range(169):
    p = strat.bb_call[h_idx]
    if 0.001 < p < 0.999:
        if h_idx < 13:
            h = HandClass(h_idx, h_idx, Kind.PAIR)
        elif h_idx < 91:
            off = h_idx - 13
            rh = 1
            while rh*(rh-1)//2 + (rh-1) < off: rh += 1
            rl = off - rh*(rh-1)//2
            h = HandClass(rh, rl, Kind.SUITED)
        else:
            off = h_idx - 91
            rh = 1
            while rh*(rh-1)//2 + (rh-1) < off: rh += 1
            rl = off - rh*(rh-1)//2
            h = HandClass(rh, rl, Kind.OFFSUIT)
        print(f"  {h.label():4} call={p*100:.2f}%")

print("\n[SB push mixed strategies — non-trivial cells]")
for h_idx in range(169):
    p = strat.sb_push[h_idx]
    if 0.001 < p < 0.999:
        if h_idx < 13:
            h = HandClass(h_idx, h_idx, Kind.PAIR)
        elif h_idx < 91:
            off = h_idx - 13
            rh = 1
            while rh*(rh-1)//2 + (rh-1) < off: rh += 1
            rl = off - rh*(rh-1)//2
            h = HandClass(rh, rl, Kind.SUITED)
        else:
            off = h_idx - 91
            rh = 1
            while rh*(rh-1)//2 + (rh-1) < off: rh += 1
            rl = off - rh*(rh-1)//2
            h = HandClass(rh, rl, Kind.OFFSUIT)
        print(f"  {h.label():4} push={p*100:.2f}%")

print("\n[some queries]")
def show(rh: int, rl: int, suited: bool, name: str | None = None):
    h = HandClass.make(rh, rl, suited)
    label = name or h.label()
    print(f"  {label:5}  SB push={strat.sb_push[h.index()]*100:6.2f}%   BB call={strat.bb_call[h.index()]*100:6.2f}%")

show(12, 12, False, "AA")
show(11, 11, False, "KK")
show(0, 0, False, "22")
show(12, 11, True,  "AKs")
show(12, 11, False, "AKo")
show(12, 0,  True,  "A2s")
show(12, 0,  False, "A2o")
show(11, 0,  True,  "K2s")
show(11, 0,  False, "K2o")
show(10, 4,  True,  "Q6s")
show(8,  3,  True,  "T5s")
show(8,  3,  False, "T5o")
show(5,  0,  False, "72o")
show(2,  1,  True,  "43s")
