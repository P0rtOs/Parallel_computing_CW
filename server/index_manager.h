#ifndef INDEX_MANAGER_H
#define INDEX_MANAGER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <fstream>
#include <iterator>

#include "IdValueTable.h"
#include "ForwardIndex.h"
#include "InvertedIndex.h"
#include "text_utils.h"

class IndexManager {
public:
    IndexManager() = default;

    IndexManager(const IndexManager&)            = delete;
    IndexManager& operator=(const IndexManager&) = delete;
    IndexManager(IndexManager&&)                 = delete;
    IndexManager& operator=(IndexManager&&)      = delete;

    bool hasFile(const std::string& docPath, unsigned int& outDocId) const {
        return docTable.getId(docPath, outDocId);
    }

    bool getFileContent(const std::string& docPath, std::string& outContent) const {
        std::ifstream in(docPath, std::ios::binary);
        if (!in) {
            return false;
        }
        outContent.assign(
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        );
        return true;
    }

    bool addFile(const std::string& docPath) {
        std::string content;
        if (!getFileContent(docPath, content)) {
            return false;
        }
        addDocumentFromContent(docPath, content);
        return true;
    }

    bool reindexFile(const std::string& docPath) {
        std::string content;
        if (!getFileContent(docPath, content)) {
            return false;
        }

        unsigned int docId = 0;
        if (!docTable.getId(docPath, docId)) {
            addDocumentFromContent(docPath, content);
            return true;
        }

        std::unordered_set<unsigned int> oldWordIds;
        if (forwardIndex.getWords(docId, oldWordIds)) {
            for (unsigned int wordId : oldWordIds) {
                invertedIndex.removePosting(wordId, docId);
            }
        }

        forwardIndex.removeDocument(docId);

        addDocumentFromContentWithExistingId(docId, docPath, content);

        return true;
    }

    bool removeFile(const std::string& docPath) {
        unsigned int docId = 0;
        if (!docTable.getId(docPath, docId)) {
            return false;
        }

        std::unordered_set<unsigned int> wordIds;
        if (forwardIndex.getWords(docId, wordIds)) {
            for (unsigned int wordId : wordIds) {
                invertedIndex.removePosting(wordId, docId);
            }
        }

        forwardIndex.removeDocument(docId);
        docTable.removeByValue(docPath);

        return true;
    }

    void clearAll() {
        wordTable.clear();
        docTable.clear();
        forwardIndex.clear();
        invertedIndex.clear();
    }

    bool searchSingleWord(const std::string& rawWord,
                          std::vector<std::string>& outDocPaths) const {
        outDocPaths.clear();

        std::string word = rawWord;
        to_lower_ascii(word);
        if (word.empty()) {
            return false;
        }

        unsigned int wordId = 0;
        if (!wordTable.getId(word, wordId)) {
            return false;
        }

        std::unordered_set<unsigned int> docIds;
        if (!invertedIndex.getDocuments(wordId, docIds)) {
            return false;
        }

        outDocPaths.reserve(docIds.size());
        for (unsigned int docId : docIds) {
            std::string path;
            if (docTable.getValue(docId, path)) {
                outDocPaths.push_back(path);
            }
        }

        std::sort(outDocPaths.begin(), outDocPaths.end());
        return !outDocPaths.empty();
    }

    bool searchAllWords(const std::vector<std::string>& rawWords,
                        std::vector<std::string>& outDocPaths) const {
        outDocPaths.clear();
        if (rawWords.empty()) {
            return false;
        }

        bool first = true;
        std::unordered_set<unsigned int> resultDocIds;

        for (const std::string& rawWord : rawWords) {
            std::string word = rawWord;
            to_lower_ascii(word);
            if (word.empty()) {
                continue;
            }

            unsigned int wordId = 0;
            if (!wordTable.getId(word, wordId)) {
                resultDocIds.clear();
                return false;
            }

            std::unordered_set<unsigned int> docIdsForWord;
            if (!invertedIndex.getDocuments(wordId, docIdsForWord)) {
                resultDocIds.clear();
                return false;
            }

            if (first) {
                resultDocIds = std::move(docIdsForWord);
                first = false;
            } else {
                std::unordered_set<unsigned int> intersection;
                for (unsigned int docId : resultDocIds) {
                    if (docIdsForWord.find(docId) != docIdsForWord.end()) {
                        intersection.insert(docId);
                    }
                }
                resultDocIds.swap(intersection);

                if (resultDocIds.empty()) {
                    return false;
                }
            }
        }

        if (resultDocIds.empty()) {
            return false;
        }

        outDocPaths.reserve(resultDocIds.size());
        for (unsigned int docId : resultDocIds) {
            std::string path;
            if (docTable.getValue(docId, path)) {
                outDocPaths.push_back(path);
            }
        }

        std::sort(outDocPaths.begin(), outDocPaths.end());
        return !outDocPaths.empty();
    }

    bool searchAnyWord(const std::vector<std::string>& rawWords,
                       std::vector<std::string>& outDocPaths) const {
        outDocPaths.clear();

        std::unordered_set<unsigned int> resultDocIds;

        for (const std::string& rawWord : rawWords) {
            std::string word = rawWord;
            to_lower_ascii(word);
            if (word.empty()) {
                continue;
            }

            unsigned int wordId = 0;
            if (!wordTable.getId(word, wordId)) {
                continue;
            }

            std::unordered_set<unsigned int> docIdsForWord;
            if (!invertedIndex.getDocuments(wordId, docIdsForWord)) {
                continue;
            }

            resultDocIds.insert(docIdsForWord.begin(), docIdsForWord.end());
        }

        if (resultDocIds.empty()) {
            return false;
        }

        outDocPaths.reserve(resultDocIds.size());
        for (unsigned int docId : resultDocIds) {
            std::string path;
            if (docTable.getValue(docId, path)) {
                outDocPaths.push_back(path);
            }
        }

        std::sort(outDocPaths.begin(), outDocPaths.end());
        return !outDocPaths.empty();
    }

private:
    unsigned int addDocumentFromContent(const std::string& docPath,
                                        const std::string& content) {
        unsigned int docId = 0;
        if (!docTable.getId(docPath, docId)) {
            docId = docTable.add(docPath);
        }

        addDocumentFromContentWithExistingId(docId, docPath, content);
        return docId;
    }

    void addDocumentFromContentWithExistingId(unsigned int docId,
                                              const std::string& docPath,
                                              const std::string& content) {
        std::string tmp = content;
        to_lower_ascii(tmp);
        std::vector<std::string> tokens = split_to_words_ascii(tmp);

        std::unordered_set<unsigned int> wordIdsForDoc;
        wordIdsForDoc.reserve(tokens.size());

        for (const std::string& token : tokens) {
            if (token.empty()) {
                continue;
            }
            unsigned int wordId = wordTable.add(token);
            wordIdsForDoc.insert(wordId);
        }

        forwardIndex.setWords(docId, wordIdsForDoc);

        for (unsigned int wordId : wordIdsForDoc) {
            invertedIndex.addPosting(wordId, docId);
        }
    }

private:
    IdValueTable<std::string> wordTable;
    IdValueTable<std::string> docTable;
    ForwardIndex              forwardIndex;
    InvertedIndex             invertedIndex;
};

#endif
