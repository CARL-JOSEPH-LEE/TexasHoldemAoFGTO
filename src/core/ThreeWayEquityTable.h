#pragma once

#include "HandClass.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aof2 {

class ThreeWayEquityTable
{
public:
    ThreeWayEquityTable();

    void compute(unsigned threads = 0, bool verbose = true);

    void load(const std::string& path);
    void save(const std::string& path) const;

    static ThreeWayEquityTable load_from(const std::string& path);

    static constexpr int64_t flat_index(int i, int j, int k) noexcept
    {
        return (static_cast<int64_t>(i) * NUM_HAND_CLASSES + j) * NUM_HAND_CLASSES + k;
    }

    double equity_p0(int i, int j, int k) const { return m_eq[3 * flat_index(i, j, k) + 0]; }
    double equity_p1(int i, int j, int k) const { return m_eq[3 * flat_index(i, j, k) + 1]; }
    double equity_p2(int i, int j, int k) const { return m_eq[3 * flat_index(i, j, k) + 2]; }

    int64_t feasible(int i, int j, int k) const { return m_feas[flat_index(i, j, k)]; }

    const std::vector<double>&  equity_array()      const { return m_eq; }
    const std::vector<int64_t>& feasibility_array() const { return m_feas; }

    bool computed() const { return m_computed; }

private:
    std::vector<double>  m_eq;
    std::vector<int64_t> m_feas;
    bool m_computed = false;
};

}
