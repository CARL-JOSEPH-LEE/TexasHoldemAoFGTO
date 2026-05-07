#pragma once

#include "PushFoldGame.h"

namespace aof2 {

inline double pot_3way(const GameParams& p)
{
    return 3.0 * p.stack;
}

inline double pot_btn_bb(const GameParams& p)
{
    return 2.0 * p.stack + p.sb_blind;
}

inline double pot_btn_sb(const GameParams& p)
{
    return 2.0 * p.stack + p.bb_blind;
}

inline double pot_sb_bb(const GameParams& p)
{
    return 2.0 * p.stack;
}

inline double called_all_in_ev(double pot, double equity, double stack)
{
    return pot * equity - stack;
}

inline double sb_vs_bb_called_ev(const GameParams& p, double sb_equity)
{
    return called_all_in_ev(pot_sb_bb(p), sb_equity, p.stack);
}

inline double bb_vs_sb_called_ev(const GameParams& p, double sb_equity)
{
    return called_all_in_ev(pot_sb_bb(p), 1.0 - sb_equity, p.stack);
}

}
