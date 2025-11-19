#ifndef TEXT_UTILS_H
#define TEXT_UTILS_H

#include <string>
#include <vector>
#include <cctype>

inline void to_lower_ascii(std::string &s) {
    for (char &ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        ch = static_cast<char>(std::tolower(c));
    }
}

inline bool is_alnum_ascii(char ch) {
    unsigned char c = static_cast<unsigned char>(ch);
    return std::isalnum(c) != 0;
}

inline std::vector<std::string> split_to_words_ascii(const std::string &text) {
    std::vector<std::string> words;
    std::string current;

    for (char ch : text) {
        if (is_alnum_ascii(ch)) {
            current.push_back(ch);
        } else {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        }
    }

    if (!current.empty()) {
        words.push_back(current);
    }

    return words;
}

#endif
