#pragma once
#include <stdexcept>
#include <string_view>

namespace hksr::framework::detail {
inline void RelativePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.find(':') != path.npos || path.find('\0') != path.npos)
        throw std::invalid_argument("asset path must be package-relative");
    std::size_t begin = 0;
    while (begin < path.size()) {
        const auto end = path.find_first_of("/\\", begin);
        if (path.substr(begin, end == path.npos ? path.size() - begin : end - begin) == "..")
            throw std::invalid_argument("asset path escapes package");
        if (end == path.npos) break;
        begin = end + 1;
    }
}
}
