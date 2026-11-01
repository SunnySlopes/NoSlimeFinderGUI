#include "csv_reader.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool parseInt64(const std::string& s, int64_t& out) {
    try {
        size_t pos = 0;
        long long v = std::stoll(trim(s), &pos);
        if (pos != trim(s).size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

std::vector<Candidate> readCandidates(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open CSV: " + path);

    // Keyed by (chunkX, chunkZ); value is the largest count seen.
    std::map<std::pair<int32_t, int32_t>, int> best;

    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;

        std::stringstream ss(t);
        std::string fx, fz, fc;
        if (!std::getline(ss, fx, ',')) continue;
        if (!std::getline(ss, fz, ',')) continue;
        std::getline(ss, fc, ',');  // count is optional

        int64_t x, z, c = 0;
        if (!parseInt64(fx, x) || !parseInt64(fz, z)) {
            // Likely a header row (or garbage) -> skip.
            continue;
        }
        if (!fc.empty()) parseInt64(fc, c);

        auto key = std::make_pair(static_cast<int32_t>(x), static_cast<int32_t>(z));
        auto it = best.find(key);
        if (it == best.end() || static_cast<int>(c) > it->second) {
            best[key] = static_cast<int>(c);
        }
    }

    std::vector<Candidate> out;
    out.reserve(best.size());
    for (const auto& kv : best) {
        out.push_back(Candidate{kv.first.first, kv.first.second, kv.second});
    }
    return out;
}
