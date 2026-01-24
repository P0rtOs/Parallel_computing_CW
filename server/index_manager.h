#ifndef INDEX_MANAGER_H
#define INDEX_MANAGER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <shared_mutex>
#include <mutex>
#include <cstdint>
#include <chrono>
#include <thread>
#include <atomic>

#include "data_structure/unordered_map.h"
#include "data_structure/forward_index.h"
#include "data_structure/inverted_index.h"
#include "utils/text_utils.h"
#include "utils/logger.h"
#include "utils/timer.h"

class IndexManager {
public:
    struct IndexMetrics {
        std::size_t scanned   = 0;
        std::size_t added     = 0;
        std::size_t reindexed = 0;
        std::size_t skipped   = 0;
        std::size_t removed   = 0;
        std::size_t failed    = 0;
        long long   timeMs    = 0;
    };

    IndexManager() = default;

    IndexManager(const IndexManager&)            = delete;
    IndexManager& operator=(const IndexManager&) = delete;
    IndexManager(IndexManager&&)                 = delete;
    IndexManager& operator=(IndexManager&&)      = delete;

    void setLogger(Logger* logger) {
        std::unique_lock<std::shared_mutex> lock(rwLock);
        this->logger = logger;
    }

    std::size_t documentsCount() const {
        std::shared_lock<std::shared_mutex> lock(rwLock);
        return docTable.size();
    }

    std::size_t wordsCount() const {
        std::shared_lock<std::shared_mutex> lock(rwLock);
        return wordTable.size();
    }

    bool hasFile(const std::string& docPath, unsigned int& outDocId) const {
        std::shared_lock<std::shared_mutex> lock(rwLock);
        const std::string norm = normalizePathString(docPath);
        return docTable.getId(norm, outDocId);
    }

    bool hasFileNormalized(const std::string& normalizedDocPath, unsigned int& outDocId) const {
        std::shared_lock<std::shared_mutex> lock(rwLock);
        return docTable.getId(normalizedDocPath, outDocId);
    }

    bool indexDirectory(const std::filesystem::path& dirPath, bool clearFirst = true) {
        // legacy: повна переіндексація кожного файлу.
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path canonicalDir = fs::weakly_canonical(dirPath, ec);
        if (ec) canonicalDir = dirPath;
        if (!fs::exists(canonicalDir, ec) || !fs::is_directory(canonicalDir, ec)) {
            return false;
        }

        if (clearFirst) clearAll();

        fs::recursive_directory_iterator it(canonicalDir, fs::directory_options::skip_permission_denied, ec);
        for (const auto& entry : it) {
            if (ec) { ec.clear(); continue; }
            if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
            reindexFile(entry.path().generic_string());
        }
        return true;
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
        const std::string norm = normalizePathString(docPath);
        {
            std::shared_lock<std::shared_mutex> lock(rwLock);
            unsigned int existingId = 0;
            if (docTable.getId(norm, existingId)) {
                return false;
            }
        }

        std::string content;
        if (!getFileContent(norm, content)) {
            return false;
        }
        {
            std::unique_lock<std::shared_mutex> lock(rwLock);
            unsigned int existingId = 0;
            if (docTable.getId(norm, existingId)) {
                return false;
            }
            const unsigned int docId = addDocumentFromContent(norm, content);
            updateMetaLocked(docId, norm);
            return true;
        }
    }

    bool reindexFile(const std::string& docPath) {
        const std::string norm = normalizePathString(docPath);
        std::string content;
        if (!getFileContent(norm, content)) {
            return false;
        }
        
        std::unique_lock<std::shared_mutex> lock(rwLock);

        unsigned int docId = 0;
        if (!docTable.getId(norm, docId)) {
            const unsigned int newId = addDocumentFromContent(norm, content);
            updateMetaLocked(newId, norm);
            return true;
        }

        std::unordered_set<unsigned int> oldWordIds;
        if (forwardIndex.getWords(docId, oldWordIds)) {
            for (unsigned int wordId : oldWordIds) {
                invertedIndex.removePosting(wordId, docId);
            }
        }

        forwardIndex.removeDocument(docId);

        addDocumentFromContentWithExistingId(docId, norm, content);
        updateMetaLocked(docId, norm);

        return true;
    }

    bool removeFile(const std::string& docPath) {
        const std::string norm = normalizePathString(docPath);
        std::unique_lock<std::shared_mutex> lock(rwLock);

        unsigned int docId = 0;
        if (!docTable.getId(norm, docId)) {
            return false;
        }

        std::unordered_set<unsigned int> wordIds;
        if (forwardIndex.getWords(docId, wordIds)) {
            for (unsigned int wordId : wordIds) {
                invertedIndex.removePosting(wordId, docId);
            }
        }

        forwardIndex.removeDocument(docId);
        docTable.removeByValue(norm);
        metaByDocId.erase(docId);

        return true;
    }

    void clearAll() {
        std::unique_lock<std::shared_mutex> lock(rwLock);
        wordTable.clear();
        docTable.clear();
        forwardIndex.clear();
        invertedIndex.clear();
        metaByDocId.clear();
    }

    bool indexDirectoryIncremental(const std::filesystem::path& dirPath,
                                   IndexMetrics& out,
                                   bool removeMissing = true,
                                   std::size_t threadCount = 0) {
        namespace fs = std::filesystem;
        out = IndexMetrics{};

        Stopwatch sw;

        std::error_code ec;
        fs::path dir = fs::weakly_canonical(dirPath, ec);
        if (ec) dir = dirPath;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            return false;
        }

        std::string dirPrefix = normalizePathString(dir.generic_string());
        if (!dirPrefix.empty() && dirPrefix.back() != '/') dirPrefix.push_back('/');

        std::vector<std::string> files;
        std::unordered_set<std::string> seen;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
        for (const auto& entry : it) {
            if (ec) { ec.clear(); continue; }
            if (!entry.is_regular_file(ec)) { ec.clear(); continue; }
            std::string path = normalizePathString(entry.path().generic_string());
            if (path.empty()) continue;
            files.push_back(path);
            seen.insert(path);
        }

        out.scanned = files.size();

        auto computeThreads = [&](std::size_t requested) -> std::size_t {
            std::size_t t = requested;
            if (t == 0) {
                unsigned int hc = std::thread::hardware_concurrency();
                t = (hc == 0) ? 4u : static_cast<std::size_t>(hc);
            }
            if (t == 0) t = 1;
            if (t > files.size() && !files.empty()) t = files.size();
            return t;
        };

        const std::size_t threads = computeThreads(threadCount);
        struct LocalMetrics {
            std::size_t added     = 0;
            std::size_t reindexed = 0;
            std::size_t skipped   = 0;
            std::size_t failed    = 0;
        };

        auto buildWordIdsFromContent = [&](const std::string& content,
                                           std::unordered_set<unsigned int>& outWordIds) {
            std::string tmp = content;
            to_lower_ascii(tmp);
            std::vector<std::string> tokens = split_to_words_ascii(tmp);
            outWordIds.clear();
            outWordIds.reserve(tokens.size());
            for (const std::string& token : tokens) {
                if (token.empty()) continue;
                unsigned int wordId = wordTable.add(token);
                outWordIds.insert(wordId);
            }
        };
        auto commitIndexUpdate = [&](const std::string& normPath,
                                     const DocMeta& curMeta,
                                     const std::unordered_set<unsigned int>& newWordIds,
                                     bool& outWasAdded) -> bool {
            std::unique_lock<std::shared_mutex> lock(rwLock);

            unsigned int docId = 0;
            const bool existed = docTable.getId(normPath, docId);
            if (!existed) {
               docId = docTable.add(normPath);
            }
            outWasAdded = !existed;

            if (existed) {
                std::unordered_set<unsigned int> oldWordIds;
                if (forwardIndex.getWords(docId, oldWordIds)) {
                    for (unsigned int wid : oldWordIds) {
                        invertedIndex.removePosting(wid, docId);
                    }
                }
            }

            forwardIndex.setWords(docId, newWordIds);
            for (unsigned int wid : newWordIds) {
                invertedIndex.addPosting(wid, docId);
            }

            metaByDocId[docId] = curMeta;
            return true;
        };

        std::atomic<std::size_t> nextIdx{0};
        std::vector<std::thread> workers;
        std::vector<LocalMetrics> locals(threads);
        workers.reserve(threads);

        for (std::size_t ti = 0; ti < threads; ++ti) {
            workers.emplace_back([&, ti]() {
                LocalMetrics lm;
                std::unordered_set<unsigned int> newWordIds;
                std::string content;

                while (true) {
                    const std::size_t i = nextIdx.fetch_add(1);
                    if (i >= files.size()) break;

                    const std::string& path = files[i];

                    DocMeta cur{};
                    if (!getMetaFromFs(path, cur)) {
                        ++lm.failed;
                        continue;
                    }

                    unsigned int docId = 0;
                    const bool exists = hasFileNormalized(path, docId);

                    if (exists) {
                        DocMeta old{};
                        {
                            std::shared_lock<std::shared_mutex> lock(rwLock);
                            auto itMeta = metaByDocId.find(docId);
                            if (itMeta != metaByDocId.end()) old = itMeta->second;
                        }
                       if (old == cur) {
                            ++lm.skipped;
                            continue;
                        }
                    }

                    content.clear();
                    if (!getFileContent(path, content)) {
                        ++lm.failed;
                        continue;
                    }

                    buildWordIdsFromContent(content, newWordIds);

                    bool wasAdded = false;
                    bool ok = commitIndexUpdate(path, cur, newWordIds, wasAdded);
                    if (!ok) {
                        ++lm.failed;
                        continue;
                    }

                    if (wasAdded) {
                        ++lm.added;
                    } else {
                        ++lm.reindexed;
                    }
                }

                locals[ti] = lm;
            });
        }

        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }

        for (const auto& lm : locals) {
            out.added     += lm.added;
            out.reindexed += lm.reindexed;
            out.skipped   += lm.skipped;
            out.failed    += lm.failed;
        }

        if (removeMissing) {
            std::vector<std::string> allDocs;
            {
                std::shared_lock<std::shared_mutex> lock(rwLock);
                docTable.snapshotValues(allDocs);
            }

            for (const std::string& doc : allDocs) {
                if (!dirPrefix.empty() && doc.rfind(dirPrefix, 0) != 0) {
                    continue;
                }
                if (seen.find(doc) != seen.end()) {
                    continue;
                }

                std::error_code ec2;
                const bool exists = fs::exists(fs::path(doc), ec2);
                if (ec2) {
                    ++out.failed;
                    continue;
                }
                if (!exists) {
                    bool ok = removeFile(doc);
                    if (ok) ++out.removed;
                    else    ++out.failed;
                }
            }
        }

        out.timeMs = sw.elapsedMs();
        if (logger) {
            std::ostringstream oss;
            oss << "Index incremental: scanned=" << out.scanned
                << " added=" << out.added
                << " reindexed=" << out.reindexed
                << " skipped=" << out.skipped
                << " removed=" << out.removed
                << " failed=" << out.failed
                << " timeMs=" << out.timeMs;
            logger->info(oss.str());
        }
        return true;
    }


    bool indexDirectory(const std::string& dirPath,
                        std::size_t& outIndexed,
                        std::size_t& outFailed,
                        bool reindexExisting = false) {
        namespace fs = std::filesystem;
        outIndexed = 0;
        outFailed  = 0;

        std::error_code ec;
        fs::path dirRoot(dirPath);
        if (!fs::exists(dirRoot, ec) || !fs::is_directory(dirRoot, ec)) {
            return false;
        }

        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(dirRoot, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;

            std::string filePath = entry.path().string();

            bool ok = false;
            if (reindexExisting) {
                ok = reindexFile(filePath);
            } else {
                unsigned int tmpId = 0;
                if (hasFile(filePath, tmpId)) {
                    continue;
                }
                ok = addFile(filePath);
            }

            if (ok) ++outIndexed;
            else    ++outFailed;
        }

        return true;
    }

    bool searchSingleWord(const std::string& rawWord,
                          std::vector<std::string>& outDocPaths) const {
        std::shared_lock<std::shared_mutex> lock(rwLock);

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
        std::shared_lock<std::shared_mutex> lock(rwLock);
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
        std::shared_lock<std::shared_mutex> lock(rwLock);
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
    struct DocMeta {
        std::uint64_t mtimeStamp = 0;
       std::uint64_t size       = 0;

        bool operator==(const DocMeta& o) const {
            return mtimeStamp == o.mtimeStamp && size == o.size;
        }
        bool operator!=(const DocMeta& o) const {
            return !(*this == o);
        }
    };

    static std::string normalizePathString(const std::string& p) {
        namespace fs = std::filesystem;
        if (p.empty()) return {};
        std::error_code ec;
        fs::path path(p);
        fs::path norm = fs::weakly_canonical(path, ec);
        if (ec) {
            ec.clear();
            norm = path.lexically_normal();
        }
        return norm.generic_string();
    }

    static std::uint64_t fileTimeStamp(const std::filesystem::file_time_type& t) {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(duration_cast<nanoseconds>(t.time_since_epoch()).count());
   }

    static bool getMetaFromFs(const std::string& path, DocMeta& out) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path p(path);
        if (!fs::exists(p, ec) || ec) return false;
        auto wt = fs::last_write_time(p, ec);
        if (ec) return false;
        auto sz = fs::file_size(p, ec);
        if (ec) return false;
        out.mtimeStamp = fileTimeStamp(wt);
        out.size = static_cast<std::uint64_t>(sz);
        return true;
    }

    void updateMetaLocked(unsigned int docId, const std::string& docPath) {
        DocMeta meta{};
        if (getMetaFromFs(docPath, meta)) {
            metaByDocId[docId] = meta;
        } else {
            metaByDocId.erase(docId);
        }
    }

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
    mutable std::shared_mutex rwLock;
    
    IdValueTable<std::string> wordTable;
    IdValueTable<std::string> docTable;
    ForwardIndex              forwardIndex;
    InvertedIndex             invertedIndex;
    std::unordered_map<unsigned int, DocMeta> metaByDocId;
    Logger* logger = nullptr;
};

#endif
