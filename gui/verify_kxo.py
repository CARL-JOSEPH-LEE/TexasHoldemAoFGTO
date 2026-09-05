import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from aof2_gui import HandClass, find_2p_strategy_file
from aof2_gui import Strategy2P as Strategy

base = Path(__file__).resolve().parent.parent
strat = Strategy.load(find_2p_strategy_file(base / "data"))

print(
    f"params: stack={strat.params.stack}  sb_blind={strat.params.sb_blind}  bb_blind={strat.params.bb_blind}"
)
print(f"sb_ev={strat.sb_ev:+.6f} BB  expl={strat.exploitability_bb:.3e} BB\n")

print("All Kxo (BB):")
for low_rank in range(11):
    h = HandClass.make(11, low_rank, False)
    p_push = strat.sb_push[h.index()]
    p_call = strat.bb_call[h.index()]
    print(f"  {h.label():4}  SB push={p_push * 100:6.2f}%   BB call={p_call * 100:6.2f}%")

print("\nAll Kxs (BB):")
for low_rank in range(11):
    h = HandClass.make(11, low_rank, True)
    p_push = strat.sb_push[h.index()]
    p_call = strat.bb_call[h.index()]
    print(f"  {h.label():4}  SB push={p_push * 100:6.2f}%   BB call={p_call * 100:6.2f}%")

print("\nAll Qxo (BB):")
for low_rank in range(10):
    h = HandClass.make(10, low_rank, False)
    p_push = strat.sb_push[h.index()]
    p_call = strat.bb_call[h.index()]
    print(f"  {h.label():4}  SB push={p_push * 100:6.2f}%   BB call={p_call * 100:6.2f}%")

print("\nAll Axo (BB):")
for low_rank in range(12):
    h = HandClass.make(12, low_rank, False)
    p_push = strat.sb_push[h.index()]
    p_call = strat.bb_call[h.index()]
    print(f"  {h.label():4}  SB push={p_push * 100:6.2f}%   BB call={p_call * 100:6.2f}%")
