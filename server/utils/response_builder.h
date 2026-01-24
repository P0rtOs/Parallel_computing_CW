#ifndef RESPONSE_BUILDER_H
#define RESPONSE_BUILDER_H

#include <string>
#include <string_view>
#include <sstream>
#include <vector>
#include <cstddef>

namespace ProtocolResponse {

inline std::string ok(std::size_t count,
                      long long totalMs,
                      std::size_t docsTotal,
                      std::size_t wordsTotal,
                      std::string_view body = {}) {
    std::ostringstream oss;
    oss << "OK " << count << "\n";
    oss << "TIME_MS " << totalMs << "\n";
    oss << "DOCS_TOTAL " << docsTotal << "\n";
    oss << "WORDS_TOTAL " << wordsTotal << "\n";
    if (!body.empty()) {
        oss << body;
        if (body.back() != '\n') oss << "\n";
    }
    oss << "END\n";
    return oss.str();
}

inline std::string error(std::string_view msg, long long totalMs) {
    std::ostringstream oss;
    oss << "ERROR " << msg << "\n";
    oss << "TIME_MS " << totalMs << "\n";
    oss << "END\n";
    return oss.str();
}

inline std::string search(bool found,
                          const std::vector<std::string>& results,
                          long long searchMs,
                          long long totalMs,
                          std::size_t docsTotal,
                          std::size_t wordsTotal) {
    if (!found || results.empty()) {
        std::ostringstream oss;
        oss << "OK 0\n";
        oss << "TIME_MS " << totalMs << "\n";
        oss << "SEARCH_MS " << searchMs << "\n";
        oss << "DOCS_TOTAL " << docsTotal << "\n";
        oss << "WORDS_TOTAL " << wordsTotal << "\n";
        oss << "END\n";
        return oss.str();
    }

    std::ostringstream oss;
    oss << "OK " << results.size() << "\n";
    oss << "TIME_MS " << totalMs << "\n";
    oss << "SEARCH_MS " << searchMs << "\n";
    oss << "DOCS_TOTAL " << docsTotal << "\n";
    oss << "WORDS_TOTAL " << wordsTotal << "\n";
    for (const auto& p : results) {
        oss << p << "\n";
    }
    oss << "END\n";
    return oss.str();
}

}
#endif