#include "core/CfrSolver.h"
#include "core/EquityTable.h"
#include "core/HandClass.h"
#include "core/Strategy.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

int main(int argc, char** argv)
{
    std::string equity_path  = "data/equity_table.bin";
    std::string strategy_path = "data/strategy.bin";

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--equity"   && i + 1 < argc) equity_path  = argv[++i];
        else if (a == "--strategy" && i + 1 < argc) strategy_path = argv[++i];
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    aof2::EquityTable et;
    et.load(equity_path);

    aof2::Strategy s;
    s.load(strategy_path);

    aof2::CfrSolver solver(et, s.params);
    auto bd = solver.breakdown(s.sb_push, s.bb_call);

    auto classes = aof2::all_hand_classes();

    std::printf("                            SB                       |              BB\n");
    std::printf("hand   push%%   push EV     fold EV    diff   |  call%%   call EV     fold EV    diff\n");
    std::printf("-----  ------  ----------  ----------  ------  |  ------  ----------  ----------  ------\n");

    for (int idx = 0; idx < aof2::NUM_HAND_CLASSES; ++idx) {
        const auto h = classes[idx];
        const double pp = s.sb_push[idx];
        const double cp = s.bb_call[idx];
        const double pe = bd.sb_push_ev[idx];
        const double pf = bd.sb_fold_ev;
        const double ce = bd.bb_call_ev[idx];
        const double cf = bd.bb_fold_ev;
        std::printf("%-5s  %5.1f%%  %+9.5f  %+9.5f  %+6.4f  |  %5.1f%%  %+9.5f  %+9.5f  %+6.4f\n",
            h.to_string().c_str(),
            100.0 * pp, pe, pf, pe - pf,
            100.0 * cp, ce, cf, ce - cf);
    }
    return 0;
}
