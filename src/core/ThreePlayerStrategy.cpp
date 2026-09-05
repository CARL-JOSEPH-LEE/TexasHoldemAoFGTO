#include <filesystem>
#include "ThreePlayerStrategy.h"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace aof2 {

static const char STRAT3_MAGIC[8]   = {'A','O','F','2','S','T','3','1'};
static const uint32_t STRAT3_VERSION = 1;

namespace {

void w_array(std::ofstream& f, const std::array<double, NUM_HAND_CLASSES>& a)
{
    f.write(reinterpret_cast<const char*>(a.data()), sizeof(double) * NUM_HAND_CLASSES);
}

void r_array(std::ifstream& f, std::array<double, NUM_HAND_CLASSES>& a)
{
    f.read(reinterpret_cast<char*>(a.data()), sizeof(double) * NUM_HAND_CLASSES);
}

}

void ThreePlayerStrategy::save(const std::string& path) const
{
    std::ofstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) throw std::runtime_error("ThreePlayerStrategy::save: cannot open " + path);

    f.write(STRAT3_MAGIC, 8);
    f.write(reinterpret_cast<const char*>(&STRAT3_VERSION), sizeof(STRAT3_VERSION));
    uint32_t n = NUM_HAND_CLASSES;
    f.write(reinterpret_cast<const char*>(&n), sizeof(n));
    f.write(reinterpret_cast<const char*>(&params.sb_blind), sizeof(params.sb_blind));
    f.write(reinterpret_cast<const char*>(&params.bb_blind), sizeof(params.bb_blind));
    f.write(reinterpret_cast<const char*>(&params.stack),    sizeof(params.stack));

    w_array(f, btn_push);
    w_array(f, sb_call_vs_btn_push);
    w_array(f, sb_push_vs_btn_fold);
    w_array(f, bb_call_3way);
    w_array(f, bb_call_vs_btn_only);
    w_array(f, bb_call_vs_sb_only);

    w_array(f, btn_push_ev);
    w_array(f, sb_call_ev_vs_btn_push);
    w_array(f, sb_push_ev_vs_btn_fold);
    w_array(f, bb_call_ev_3way);
    w_array(f, bb_call_ev_vs_btn_only);
    w_array(f, bb_call_ev_vs_sb_only);

    f.write(reinterpret_cast<const char*>(&btn_fold_ev), sizeof(btn_fold_ev));
    f.write(reinterpret_cast<const char*>(&sb_fold_ev_vs_btn_push), sizeof(sb_fold_ev_vs_btn_push));
    f.write(reinterpret_cast<const char*>(&sb_fold_ev_vs_btn_fold), sizeof(sb_fold_ev_vs_btn_fold));
    f.write(reinterpret_cast<const char*>(&bb_fold_ev), sizeof(bb_fold_ev));

    f.write(reinterpret_cast<const char*>(&btn_ev), sizeof(btn_ev));
    f.write(reinterpret_cast<const char*>(&sb_ev),  sizeof(sb_ev));
    f.write(reinterpret_cast<const char*>(&bb_ev),  sizeof(bb_ev));

    f.write(reinterpret_cast<const char*>(&exploitability_bb), sizeof(exploitability_bb));
    f.write(reinterpret_cast<const char*>(&iterations),       sizeof(iterations));

    if (!f) throw std::runtime_error("ThreePlayerStrategy::save: write failure");
}

void ThreePlayerStrategy::load(const std::string& path)
{
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) throw std::runtime_error("ThreePlayerStrategy::load: cannot open " + path);

    char magic[8]{};
    f.read(magic, 8);
    if (std::memcmp(magic, STRAT3_MAGIC, 8) != 0)
        throw std::runtime_error("ThreePlayerStrategy::load: bad magic in " + path);

    uint32_t version = 0, n = 0;
    f.read(reinterpret_cast<char*>(&version), sizeof(version));
    f.read(reinterpret_cast<char*>(&n), sizeof(n));
    if (version != STRAT3_VERSION)
        throw std::runtime_error("ThreePlayerStrategy::load: version mismatch");
    if (n != NUM_HAND_CLASSES)
        throw std::runtime_error("ThreePlayerStrategy::load: hand class count mismatch");

    f.read(reinterpret_cast<char*>(&params.sb_blind), sizeof(params.sb_blind));
    f.read(reinterpret_cast<char*>(&params.bb_blind), sizeof(params.bb_blind));
    f.read(reinterpret_cast<char*>(&params.stack),    sizeof(params.stack));

    r_array(f, btn_push);
    r_array(f, sb_call_vs_btn_push);
    r_array(f, sb_push_vs_btn_fold);
    r_array(f, bb_call_3way);
    r_array(f, bb_call_vs_btn_only);
    r_array(f, bb_call_vs_sb_only);

    r_array(f, btn_push_ev);
    r_array(f, sb_call_ev_vs_btn_push);
    r_array(f, sb_push_ev_vs_btn_fold);
    r_array(f, bb_call_ev_3way);
    r_array(f, bb_call_ev_vs_btn_only);
    r_array(f, bb_call_ev_vs_sb_only);

    f.read(reinterpret_cast<char*>(&btn_fold_ev), sizeof(btn_fold_ev));
    f.read(reinterpret_cast<char*>(&sb_fold_ev_vs_btn_push), sizeof(sb_fold_ev_vs_btn_push));
    f.read(reinterpret_cast<char*>(&sb_fold_ev_vs_btn_fold), sizeof(sb_fold_ev_vs_btn_fold));
    f.read(reinterpret_cast<char*>(&bb_fold_ev), sizeof(bb_fold_ev));

    f.read(reinterpret_cast<char*>(&btn_ev), sizeof(btn_ev));
    f.read(reinterpret_cast<char*>(&sb_ev),  sizeof(sb_ev));
    f.read(reinterpret_cast<char*>(&bb_ev),  sizeof(bb_ev));

    f.read(reinterpret_cast<char*>(&exploitability_bb), sizeof(exploitability_bb));
    f.read(reinterpret_cast<char*>(&iterations),       sizeof(iterations));

    if (!f) throw std::runtime_error("ThreePlayerStrategy::load: read truncated");
}

}
