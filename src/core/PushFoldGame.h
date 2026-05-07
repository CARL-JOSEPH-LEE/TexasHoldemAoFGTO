#pragma once

namespace aof2 {

struct GameParams
{
    double sb_blind = 0.5;
    double bb_blind = 1.0;
    double stack    = 10.0;

    double sb_remaining_after_blind() const { return stack - sb_blind; }
    double bb_remaining_after_blind() const { return stack - bb_blind; }

    double sb_fold_ev() const  { return -sb_blind; }
    double bb_fold_ev() const  { return -bb_blind; }

    double sb_push_bbfold_ev() const { return bb_blind; }
    double bb_call_pot_after() const { return 2.0 * stack; }

    double sb_push_called_ev(double sb_eq) const
    {
        return (2.0 * sb_eq - 1.0) * stack;
    }

    double bb_call_called_ev(double sb_eq) const
    {
        return (1.0 - 2.0 * sb_eq) * stack;
    }
};

}
