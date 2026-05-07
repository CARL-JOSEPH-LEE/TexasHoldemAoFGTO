#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace aof2 {

constexpr int NUM_HAND_CLASSES = 169;
constexpr int NUM_RANKS = 13;
constexpr int NUM_SUITS = 4;
constexpr int NUM_CARDS = 52;
constexpr int NUM_COMBOS = 1326;

struct Combo
{
    uint8_t card_high;
    uint8_t card_low;
};

class HandClass
{
public:
    enum class Kind : uint8_t { Pair = 0, Suited = 1, Offsuit = 2 };

    HandClass() = default;

    HandClass(int rank_high, int rank_low, Kind kind)
        : m_rank_high(rank_high), m_rank_low(rank_low), m_kind(kind)
    {
        if (rank_high < 0 || rank_high >= NUM_RANKS)
            throw std::out_of_range("HandClass: rank_high out of range");
        if (rank_low < 0 || rank_low >= NUM_RANKS)
            throw std::out_of_range("HandClass: rank_low out of range");
        if (kind == Kind::Pair && rank_high != rank_low)
            throw std::invalid_argument("HandClass: pair requires equal ranks");
        if (kind != Kind::Pair && rank_high <= rank_low)
            throw std::invalid_argument("HandClass: non-pair requires rank_high > rank_low");
    }

    int rank_high() const { return m_rank_high; }
    int rank_low() const { return m_rank_low; }
    Kind kind() const { return m_kind; }

    int index() const
    {
        if (m_kind == Kind::Pair) {
            return m_rank_high;
        }
        const int offset = (m_rank_high * (m_rank_high - 1)) / 2 + m_rank_low;
        return (m_kind == Kind::Suited ? 13 : 91) + offset;
    }

    int num_combos() const
    {
        switch (m_kind) {
            case Kind::Pair:    return 6;
            case Kind::Suited:  return 4;
            case Kind::Offsuit: return 12;
        }
        throw std::logic_error("HandClass::num_combos: unreachable");
    }

    std::vector<Combo> enumerate_combos() const
    {
        std::vector<Combo> out;
        out.reserve(num_combos());

        const int rh = m_rank_high;
        const int rl = m_rank_low;

        if (m_kind == Kind::Pair) {
            for (int s1 = 0; s1 < NUM_SUITS; ++s1) {
                for (int s2 = s1 + 1; s2 < NUM_SUITS; ++s2) {
                    const uint8_t c1 = static_cast<uint8_t>(4 * rh + s1);
                    const uint8_t c2 = static_cast<uint8_t>(4 * rh + s2);
                    out.push_back({static_cast<uint8_t>(std::max(c1, c2)),
                                   static_cast<uint8_t>(std::min(c1, c2))});
                }
            }
        } else if (m_kind == Kind::Suited) {
            for (int s = 0; s < NUM_SUITS; ++s) {
                const uint8_t ch = static_cast<uint8_t>(4 * rh + s);
                const uint8_t cl = static_cast<uint8_t>(4 * rl + s);
                out.push_back({ch, cl});
            }
        } else {
            for (int sh = 0; sh < NUM_SUITS; ++sh) {
                for (int sl = 0; sl < NUM_SUITS; ++sl) {
                    if (sh == sl) continue;
                    const uint8_t ch = static_cast<uint8_t>(4 * rh + sh);
                    const uint8_t cl = static_cast<uint8_t>(4 * rl + sl);
                    out.push_back({ch, cl});
                }
            }
        }

        if (static_cast<int>(out.size()) != num_combos())
            throw std::logic_error("HandClass::enumerate_combos: combo count mismatch");
        return out;
    }

    std::string to_string() const
    {
        static constexpr char rank_chars[] = "23456789TJQKA";
        std::string s;
        s.push_back(rank_chars[m_rank_high]);
        s.push_back(rank_chars[m_rank_low]);
        if (m_kind == Kind::Suited)  s.push_back('s');
        if (m_kind == Kind::Offsuit) s.push_back('o');
        return s;
    }

    std::string to_omp_range() const
    {
        return to_string();
    }

    static HandClass from_index(int idx)
    {
        if (idx < 0 || idx >= NUM_HAND_CLASSES)
            throw std::out_of_range("HandClass::from_index: bad index");
        if (idx < 13) return HandClass(idx, idx, Kind::Pair);
        Kind kind = (idx < 91) ? Kind::Suited : Kind::Offsuit;
        int offset = idx - (kind == Kind::Suited ? 13 : 91);
        int rh = 1;
        while (rh * (rh - 1) / 2 + (rh - 1) < offset) ++rh;
        int rl = offset - rh * (rh - 1) / 2;
        return HandClass(rh, rl, kind);
    }

    static HandClass from_combo(uint8_t card1, uint8_t card2)
    {
        if (card1 >= NUM_CARDS || card2 >= NUM_CARDS || card1 == card2)
            throw std::invalid_argument("HandClass::from_combo: invalid cards");
        int r1 = card1 >> 2, s1 = card1 & 3;
        int r2 = card2 >> 2, s2 = card2 & 3;
        if (r1 == r2)
            return HandClass(r1, r1, Kind::Pair);
        int rh = std::max(r1, r2), rl = std::min(r1, r2);
        bool suited = (s1 == s2);
        return HandClass(rh, rl, suited ? Kind::Suited : Kind::Offsuit);
    }

private:
    int m_rank_high = 0;
    int m_rank_low = 0;
    Kind m_kind = Kind::Pair;
};

inline std::array<HandClass, NUM_HAND_CLASSES> all_hand_classes()
{
    std::array<HandClass, NUM_HAND_CLASSES> out{};
    int i = 0;
    for (int r = 0; r < NUM_RANKS; ++r)
        out[i++] = HandClass(r, r, HandClass::Kind::Pair);
    for (int rh = 1; rh < NUM_RANKS; ++rh)
        for (int rl = 0; rl < rh; ++rl)
            out[i++] = HandClass(rh, rl, HandClass::Kind::Suited);
    for (int rh = 1; rh < NUM_RANKS; ++rh)
        for (int rl = 0; rl < rh; ++rl)
            out[i++] = HandClass(rh, rl, HandClass::Kind::Offsuit);
    if (i != NUM_HAND_CLASSES)
        throw std::logic_error("all_hand_classes: count mismatch");
    return out;
}

}
