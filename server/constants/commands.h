#ifndef COMMANDS_H
#define COMMANDS_H

#include <string_view>
#include <optional>

#ifdef INDEX_ALL
#undef INDEX_ALL
#endif
#ifdef ADD_FILE
#undef ADD_FILE
#endif
#ifdef REMOVE_FILE
#undef REMOVE_FILE
#endif
#ifdef REINDEX_FILE
#undef REINDEX_FILE
#endif
#ifdef HAS_FILE
#undef HAS_FILE
#endif
#ifdef INDEX_DIR
#undef INDEX_DIR
#endif
#ifdef REINDEX_DIR
#undef REINDEX_DIR
#endif
#ifdef REBUILD_INDEX
#undef REBUILD_INDEX
#endif
#ifdef SEARCH_ONE
#undef SEARCH_ONE
#endif
#ifdef SEARCH_ALL
#undef SEARCH_ALL
#endif
#ifdef SEARCH_ANY
#undef SEARCH_ANY
#endif

enum class CommandCode {
    INDEX_ALL,
    ADD_FILE,
    REMOVE_FILE,
    REINDEX_FILE,
    HAS_FILE,
    INDEX_DIR,
    REINDEX_DIR,
    REBUILD_INDEX,
    SEARCH_ONE,
    SEARCH_ALL,
    SEARCH_ANY
};

inline std::optional<CommandCode> parseCommand(std::string_view cmd) {
    if (cmd == "INDEX_ALL")     return CommandCode::INDEX_ALL;
    if (cmd == "ADD_FILE")      return CommandCode::ADD_FILE;
    if (cmd == "REMOVE_FILE")   return CommandCode::REMOVE_FILE;
    if (cmd == "REINDEX_FILE")  return CommandCode::REINDEX_FILE;
    if (cmd == "HAS_FILE")      return CommandCode::HAS_FILE;
    if (cmd == "INDEX_DIR")     return CommandCode::INDEX_DIR;
    if (cmd == "REINDEX_DIR")   return CommandCode::REINDEX_DIR;
    if (cmd == "REBUILD_INDEX") return CommandCode::REBUILD_INDEX;
    if (cmd == "SEARCH_ONE")    return CommandCode::SEARCH_ONE;
    if (cmd == "SEARCH_ALL")    return CommandCode::SEARCH_ALL;
    if (cmd == "SEARCH_ANY")    return CommandCode::SEARCH_ANY;
    return std::nullopt;
}

#endif