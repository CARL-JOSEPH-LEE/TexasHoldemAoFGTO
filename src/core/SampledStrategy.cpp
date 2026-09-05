#include "SampledSolver.h"
#include "AtomicFile.h"
#include "Checksum.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>

namespace aof2 {
namespace {
template<class T> void write(std::ostream& f, const T& value) {
    std::array<char, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
    const uint16_t endian = 1;
    if (*reinterpret_cast<const char*>(&endian) != 1) std::reverse(bytes.begin(), bytes.end());
    f.write(bytes.data(), bytes.size());
}
template<class T> T read(std::istream& f) {
    std::array<char, sizeof(T)> bytes{};
    f.read(bytes.data(), bytes.size());
    if (!f) throw std::runtime_error("truncated strategy file");
    const uint16_t endian = 1;
    if (*reinterpret_cast<const char*>(&endian) != 1) std::reverse(bytes.begin(), bytes.end());
    T value;
    std::memcpy(&value, bytes.data(), sizeof(T));
    return value;
}
void finite(double d) {
    if (!std::isfinite(d)) throw std::runtime_error("non-finite strategy value");
}
}

void SampledStrategy::validate() const {
    const MultiwayGame game(players, params, rake);
    if (iterations > 1000010000000ULL || evaluation_samples > 1000010000000ULL || audit_samples > 1000010000000ULL
        || training_sweeps > iterations || (training_method && !training_sweeps))
        throw std::runtime_error("invalid strategy sampling counters");
    if (nodes.size() != game.nodes.size()) throw std::runtime_error("wrong strategy node count");
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (n.decision.seat != game.nodes[i].seat || n.decision.prior_mask != game.nodes[i].prior_mask)
            throw std::runtime_error("invalid strategy action history");
        for (int h = 0; h < NUM_HAND_CLASSES; ++h) {
            finite(n.frequency[h]); finite(n.action_ev[h]); finite(n.fold_ev[h]);
            finite(n.reach[h]); finite(n.effective_samples[h]);
            finite(n.action_ev_std_error[h]); finite(n.advantage_lower[h]); finite(n.advantage_upper[h]);
            if (n.frequency[h] < 0 || n.frequency[h] > 1 || n.reach[h] < 0 || n.reach[h] > 1.000000001
                || n.effective_samples[h] < 0 || n.effective_samples[h] > std::max(audit_samples, evaluation_samples) + 1.0
                || n.action_ev_std_error[h] < 0 || n.advantage_lower[h] > n.advantage_upper[h])
                throw std::runtime_error("invalid probability or sample count in strategy");
        }
    }
    for (int p = 0; p < 4; ++p) {
        finite(ev[p]); finite(ev_std_error[p]);
        finite(conditional_ev[p]); finite(conditional_ev_std_error[p]);
        if (ev_std_error[p] < 0 || conditional_ev_std_error[p] < 0) throw std::runtime_error("negative standard error");
    }
    finite(expected_rake); finite(sampled_nash_conv);
    finite(confidence); finite(deviation_upper);
    if (training_method > 4 || confidence < 0 || confidence >= 1 || deviation_upper < 0
        || (audit_samples && confidence == 0)) throw std::runtime_error("invalid precision metadata");
    if (expected_rake < 0 || sampled_nash_conv < 0) throw std::runtime_error("negative strategy diagnostic");
}

void SampledStrategy::save(const std::string& path) const {
    validate();
    const auto destination = std::filesystem::u8path(path);
    auto temporary = destination; temporary += ".tmp";
    std::ostringstream f(std::ios::binary);
    if (!f) throw std::runtime_error("cannot write strategy: " + path);
    f.write("AOFMSTR1", 8);
    write(f, uint32_t{3}); write(f, uint32_t(players));
    write(f, uint32_t{NUM_HAND_CLASSES}); write(f, uint32_t(nodes.size()));
    for (double d : {params.sb_blind, params.bb_blind, params.stack, rake.rate, rake.cap}) write(f, d);
    write(f, uint32_t(rake.no_flop_no_drop));
    for (uint64_t v : {iterations, seed, evaluation_samples}) write(f, v);
    for (double v : ev) write(f, v);
    for (double v : ev_std_error) write(f, v);
    write(f, expected_rake); write(f, sampled_nash_conv);
    write(f, training_method); write(f, training_sweeps); write(f, audit_samples);
    write(f, confidence); write(f, deviation_upper);
    for (double v : conditional_ev) write(f, v);
    for (double v : conditional_ev_std_error) write(f, v);
    for (const auto& node : nodes) {
        write(f, uint32_t(node.decision.seat)); write(f, uint32_t(node.decision.prior_mask));
        for (const auto* row : {&node.frequency, &node.action_ev, &node.fold_ev, &node.reach, &node.effective_samples,
                               &node.action_ev_std_error, &node.advantage_lower, &node.advantage_upper})
            for (double v : *row) write(f, v);
    }
    const auto payload = f.str();
    std::ofstream out(temporary, std::ios::binary);
    out.write(payload.data(), payload.size());
    write(out, crc32(payload));
    out.close();
    if (!out) throw std::runtime_error("strategy write failed: " + path);
    atomic_replace(temporary, destination);
}

void SampledStrategy::load(const std::string& path) {
    const auto file = std::filesystem::u8path(path);
    if (std::filesystem::file_size(file) > 1000000) throw std::runtime_error("strategy file too large");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open strategy: " + path);
    std::string bytes((std::istreambuf_iterator<char>(input)), {});
    std::istringstream f(bytes, std::ios::binary);
    char magic[8]{};
    f.read(magic, 8);
    if (!f || std::memcmp(magic, "AOFMSTR1", 8)) throw std::runtime_error("not a sampled strategy file");
    const auto version = read<uint32_t>(f);
    if (version != 1 && version != 2 && version != 3) throw std::runtime_error("unsupported sampled strategy version");
    if (version == 3) {
        if (bytes.size() < 16) throw std::runtime_error("truncated strategy checksum");
        std::istringstream trailer(bytes.substr(bytes.size() - 4), std::ios::binary);
        bytes.resize(bytes.size() - 4);
        if (read<uint32_t>(trailer) != crc32(bytes)) throw std::runtime_error("strategy checksum mismatch");
        f.str(bytes); f.seekg(12);
    }
    SampledStrategy result;
    result.players = static_cast<int>(read<uint32_t>(f));
    if (read<uint32_t>(f) != NUM_HAND_CLASSES) throw std::runtime_error("invalid hand class count");
    const uint32_t count = read<uint32_t>(f);
    const MultiwayGame layout(result.players);
    if (count != layout.nodes.size()) throw std::runtime_error("invalid node count");
    result.params.sb_blind = read<double>(f); result.params.bb_blind = read<double>(f);
    result.params.stack = read<double>(f); result.rake.rate = read<double>(f); result.rake.cap = read<double>(f);
    const auto flag = read<uint32_t>(f);
    if (flag > 1) throw std::runtime_error("invalid rake flag");
    result.rake.no_flop_no_drop = flag != 0;
    result.iterations = read<uint64_t>(f); result.seed = read<uint64_t>(f); result.evaluation_samples = read<uint64_t>(f);
    for (double& v : result.ev) v = read<double>(f);
    for (double& v : result.ev_std_error) v = read<double>(f);
    result.expected_rake = read<double>(f); result.sampled_nash_conv = read<double>(f);
    if (version >= 2) {
        result.training_method = read<uint32_t>(f); result.training_sweeps = read<uint64_t>(f);
        result.audit_samples = read<uint64_t>(f); result.confidence = read<double>(f); result.deviation_upper = read<double>(f);
        for (double& v : result.conditional_ev) v = read<double>(f);
        for (double& v : result.conditional_ev_std_error) v = read<double>(f);
    }
    for (uint32_t i = 0; i < count; ++i) {
        SampledNode node;
        node.decision.seat = static_cast<int>(read<uint32_t>(f)); node.decision.prior_mask = read<uint32_t>(f);
        for (auto* row : {&node.frequency, &node.action_ev, &node.fold_ev, &node.reach, &node.effective_samples})
            for (double& v : *row) v = read<double>(f);
        if (version >= 2) for (auto* row : {&node.action_ev_std_error, &node.advantage_lower, &node.advantage_upper})
            for (double& v : *row) v = read<double>(f);
        result.nodes.push_back(node);
    }
    if (f.peek() != std::char_traits<char>::eof()) throw std::runtime_error("trailing bytes in strategy file");
    result.validate();
    *this = std::move(result);
}

void SampledStrategy::export_csv(const std::string& path) const {
    validate();
    std::ofstream f(std::filesystem::u8path(path));
    if (!f) throw std::runtime_error("cannot write CSV: " + path);
    f.imbue(std::locale::classic());
    f << std::setprecision(12);
    f << "players,stack_bb,sb_blind,bb_blind,rake_percent,rake_cap_bb,no_flop_no_drop,position,prior_mask,hand,all_in_frequency,action_ev_bb,fold_ev_bb,history_reach,effective_samples,action_ev_std_error,advantage_lower,advantage_upper,simultaneous_confidence,training_method,training_samples,audit_samples,deviation_upper\n";
    const MultiwayGame game(players, params, rake);
    for (const auto& node : nodes) for (int h = 0; h < NUM_HAND_CLASSES; ++h) {
        f << players << ',' << params.stack << ',' << params.sb_blind << ',' << params.bb_blind << ','
          << rake.rate * 100 << ',' << rake.cap << ',' << rake.no_flop_no_drop << ','
          << game.position(node.decision.seat) << ',' << node.decision.prior_mask << ','
          << HandClass::from_index(h).to_string() << ',' << node.frequency[h] << ',';
        if (node.effective_samples[h] > 0) f << node.action_ev[h];
        f << ',';
        if (node.effective_samples[h] > 0) f << node.fold_ev[h];
        f << ',' << node.reach[h] << ',' << node.effective_samples[h] << ',';
        if (audit_samples && node.effective_samples[h] > 0)
            f << node.action_ev_std_error[h] << ',' << node.advantage_lower[h] << ',' << node.advantage_upper[h];
        else f << ",,";
        f << ',' << confidence << ',' << training_method << ',' << iterations << ',' << audit_samples << ',' << deviation_upper << '\n';
    }
    f.close();
    if (!f) throw std::runtime_error("CSV write failed");
}
}
