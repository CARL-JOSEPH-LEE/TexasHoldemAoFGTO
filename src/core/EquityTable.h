#pragma once

#include "HandClass.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aof2 {

class EquityTable
{
public:
    EquityTable();

    void compute(unsigned threads = 0, bool verbose = true);

    void load(const std::string& path);
    void save(const std::string& path) const;

    double equity(int sb_class, int bb_class) const
    {
        return m_equity[sb_class * NUM_HAND_CLASSES + bb_class];
    }

    int64_t feasible_combos(int sb_class, int bb_class) const
    {
        return m_feasible[sb_class * NUM_HAND_CLASSES + bb_class];
    }

    const std::vector<double>& equity_matrix() const { return m_equity; }
    const std::vector<int64_t>& feasibility_matrix() const { return m_feasible; }

    int64_t total_feasible_pairs() const;

    double row_total_weight(int sb_class) const;

    static EquityTable load_from(const std::string& path);

private:
    std::vector<double>  m_equity;
    std::vector<int64_t> m_feasible;
    bool m_computed = false;
};

}
