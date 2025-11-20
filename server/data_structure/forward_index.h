#ifndef FORWARD_INDEX_H
#define FORWARD_INDEX_H

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <cstdint>

class ForwardIndex {
public:
    ForwardIndex() = default;

    ForwardIndex(const ForwardIndex&)            = delete;
    ForwardIndex& operator=(const ForwardIndex&) = delete;
    ForwardIndex(ForwardIndex&&)                 = delete;
    ForwardIndex& operator=(ForwardIndex&&)      = delete;

    void addWord(unsigned int docId, unsigned int wordId) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        wordIdsByDoc[docId].insert(wordId);
    }

    void setWords(unsigned int docId, const std::unordered_set<unsigned int>& wordIds) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        wordIdsByDoc[docId] = wordIds;
    }

    bool getWords(unsigned int docId, std::unordered_set<unsigned int>& outWordIds) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        auto it = wordIdsByDoc.find(docId);
        if (it == wordIdsByDoc.end()) {
            return false;
        }
        outWordIds = it->second;
        return true;
    }

    bool removeDocument(unsigned int docId) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        auto it = wordIdsByDoc.find(docId);
        if (it == wordIdsByDoc.end()) {
            return false;
        }
        wordIdsByDoc.erase(it);
        return true;
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex);
        wordIdsByDoc.clear();
    }

    bool hasDocument(unsigned int docId) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return wordIdsByDoc.find(docId) != wordIdsByDoc.end();
    }

    unsigned int size() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return static_cast<unsigned int>(wordIdsByDoc.size());
    }

    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return wordIdsByDoc.empty();
    }

private:
    mutable std::shared_mutex mutex;
    std::unordered_map<unsigned int, std::unordered_set<unsigned int>> wordIdsByDoc;
};

#endif
