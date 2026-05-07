from PyQt6 import QtCore
print("PyQt6", QtCore.PYQT_VERSION_STR, "Qt", QtCore.QT_VERSION_STR)

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))

import aof2_gui  # noqa: F401
print("aof2_gui imported OK")

base = Path(__file__).resolve().parent.parent
bundle = aof2_gui.load_bundle(base / "data")
print("2P strategy loaded =", bundle.s2p is not None)
if bundle.s2p_error:
    print("2P strategy note:", bundle.s2p_error)
print("3P strategy loaded =", bundle.s3p is not None)
if bundle.s3p_error:
    print("3P strategy note:", bundle.s3p_error)

if bundle.s2p is None:
    print("no 2P strategy available; import verification complete")
    raise SystemExit(0)

s = bundle.s2p
print(f"loaded 2P strategy: iters={s.iterations}, expl={s.exploitability_bb:.3e}, sb_ev={s.sb_ev:+.4f}")

aa = aof2_gui.HandClass(12, 12, aof2_gui.Kind.PAIR)
aks = aof2_gui.HandClass(12, 11, aof2_gui.Kind.SUITED)
ako = aof2_gui.HandClass(12, 11, aof2_gui.Kind.OFFSUIT)
print(f"AA push={s.sb_push[aa.index()]:.4f} call={s.bb_call[aa.index()]:.4f}")
print(f"AKs push={s.sb_push[aks.index()]:.4f} call={s.bb_call[aks.index()]:.4f}")
print(f"AKo push={s.sb_push[ako.index()]:.4f} call={s.bb_call[ako.index()]:.4f}")

t72 = aof2_gui.HandClass.make(5, 0, False)
print(f"72o push={s.sb_push[t72.index()]:.4f} call={s.bb_call[t72.index()]:.4f}")

q6s = aof2_gui.HandClass.make(10, 4, True)
print(f"Q6s push={s.sb_push[q6s.index()]:.4f} call={s.bb_call[q6s.index()]:.4f}")
