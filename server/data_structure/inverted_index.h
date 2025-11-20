#ifndef INVERTED_INDEX_H
#define INVERTED_INDEX_H

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <cstdint>

class InvertedIndex {
public:
    InvertedIndex() = default;

    InvertedIndex(const InvertedIndex&)            = delete;
    InvertedIndex& operator=(const InvertedIndex&) = delete;
    InvertedIndex(InvertedIndex&&)                 = delete;
    InvertedIndex& operator=(InvertedIndex&&)      = delete;

    void addPosting(unsigned int wordId, unsigned int docId) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        docIdsByWord[wordId].insert(docId);
    }

    void addPostingSet(unsigned int wordId, const std::unordered_set<unsigned int>& docIds) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        std::unordered_set<unsigned int>& dest = docIdsByWord[wordId];
        dest.insert(docIds.begin(), docIds.end());
    }

    bool getDocuments(unsigned int wordId, std::unordered_set<unsigned int>& outDocIds) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        auto it = docIdsByWord.find(wordId);
        if (it == docIdsByWord.end()) {
            return false;
        }
        outDocIds = it->second;
        return true;
    }

    bool removePosting(unsigned int wordId, unsigned int docId) {
        std::unique_lock<std::shared_mutex> lock(mutex);
        auto it = docIdsByWord.find(wordId);
        if (it == docIdsByWord.end()) {
            return false;
        }

        std::unordered_set<unsigned int>& docs = it->second;
        auto itDoc = docs.find(docId);
        if (itDoc == docs.end()) {
            return false;
        }

        docs.erase(itDoc);

        if (docs.empty()) {
            docIdsByWord.erase(it);
        }

        return true;
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex);
        docIdsByWord.clear();
    }

    bool hasWord(unsigned int wordId) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return docIdsByWord.find(wordId) != docIdsByWord.end();
    }

    unsigned int size() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return static_cast<unsigned int>(docIdsByWord.size());
    }

    bool empty() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return docIdsByWord.empty();
    }

private:
    mutable std::shared_mutex mutex;
    std::unordered_map<unsigned int, std::unordered_set<unsigned int>> docIdsByWord;
};

#endif
