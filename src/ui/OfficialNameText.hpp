#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hksr::framework::ui {
// Name metadata remains the official raw text outside the UI. Only observed
// formatting delimiters are removed; their contents, unknown tags and literal
// comparisons are not discarded. Unpaired delimiters remain visible as data.
inline std::string OfficialNameText(std::string_view text) {
    std::vector<std::size_t> opens;
    std::vector<std::pair<std::size_t, std::size_t>> formatting;
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, 4, "<ub>") == 0) {
            opens.push_back(at);
            at += 4;
        } else if (text.compare(at, 5, "</ub>") == 0) {
            if (!opens.empty()) {
                formatting.emplace_back(opens.back(), 4);
                opens.pop_back();
                formatting.emplace_back(at, 5);
            }
            at += 5;
        } else
            ++at;
    }
    std::sort(formatting.begin(), formatting.end());
    std::string plain;
    plain.reserve(text.size());
    std::size_t copied = 0;
    for (const auto& [at, length] : formatting) {
        plain.append(text.substr(copied, at - copied));
        copied = at + length;
    }
    plain.append(text.substr(copied));
    return plain;
}
}
