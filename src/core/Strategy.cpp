#include "Strategy.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace aof2 {

static const char STRAT_MAGIC[8]   = {'A','O','F','2','S','T','R','2'};
static const uint32_t STRAT_VERSION = 2;

void Strategy::save(const std::string& path) const
{
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Strategy::save: cannot open " + path);

    f.write(STRAT_MAGIC, 8);
    f.write(reinterpret_cast<const char*>(&STRAT_VERSION), sizeof(STRAT_VERSION));

    uint32_t n = NUM_HAND_CLASSES;
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));

    f.write(reinterpret_cast<const char*>(&params.sb_blind), sizeof(params.sb_blind));
    f.write(reinterpret_cast<const char*>(&params.bb_blind), sizeof(params.bb_blind));
    f.write(reinterpret_cast<const char*>(&params.stack),    sizeof(params.stack));

    f.write(reinterpret_cast<const char*>(sb_push.data()),
            sizeof(double) * NUM_HAND_CLASSES);
    f.write(reinterpret_cast<const char*>(bb_call.data()),
            sizeof(double) * NUM_HAND_CLASSES);

    f.write(reinterpret_cast<const char*>(sb_push_ev.data()),
            sizeof(double) * NUM_HAND_CLASSES);
    f.write(reinterpret_cast<const char*>(bb_call_ev.data()),
            sizeof(double) * NUM_HAND_CLASSES);
    f.write(reinterpret_cast<const char*>(&sb_fold_ev), sizeof(sb_fold_ev));
    f.write(reinterpret_cast<const char*>(&bb_fold_ev), sizeof(bb_fold_ev));

    f.write(reinterpret_cast<const char*>(&sb_ev),            sizeof(sb_ev));
    f.write(reinterpret_cast<const char*>(&exploitability_bb), sizeof(exploitability_bb));
    f.write(reinterpret_cast<const char*>(&iterations),       sizeof(iterations));

    if (!f) throw std::runtime_error("Strategy::save: write failure");
}

void Strategy::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Strategy::load: cannot open " + path);

    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, STRAT_MAGIC, 8) != 0)
        throw std::runtime_error("Strategy::load: bad magic in " + path);

    uint32_t version = 0, n = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (version != STRAT_VERSION)
        throw std::runtime_error("Strategy::load: version mismatch");
    if (n != NUM_HAND_CLASSES)
        throw std::runtime_error("Strategy::load: hand class count mismatch");

    f.read(reinterpret_cast<char*>(&params.sb_blind), sizeof(params.sb_blind));
    f.read(reinterpret_cast<char*>(&params.bb_blind), sizeof(params.bb_blind));
    f.read(reinterpret_cast<char*>(&params.stack),    sizeof(params.stack));

    f.read(reinterpret_cast<char*>(sb_push.data()),
           sizeof(double) * NUM_HAND_CLASSES);
    f.read(reinterpret_cast<char*>(bb_call.data()),
           sizeof(double) * NUM_HAND_CLASSES);

    f.read(reinterpret_cast<char*>(sb_push_ev.data()),
           sizeof(double) * NUM_HAND_CLASSES);
    f.read(reinterpret_cast<char*>(bb_call_ev.data()),
           sizeof(double) * NUM_HAND_CLASSES);
    f.read(reinterpret_cast<char*>(&sb_fold_ev), sizeof(sb_fold_ev));
    f.read(reinterpret_cast<char*>(&bb_fold_ev), sizeof(bb_fold_ev));

    f.read(reinterpret_cast<char*>(&sb_ev),            sizeof(sb_ev));
    f.read(reinterpret_cast<char*>(&exploitability_bb), sizeof(exploitability_bb));
    f.read(reinterpret_cast<char*>(&iterations),       sizeof(iterations));

    if (!f) throw std::runtime_error("Strategy::load: read truncated");
}

}
