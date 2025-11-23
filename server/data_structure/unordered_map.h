#ifndef ID_VALUE_TABLE_H
#define ID_VALUE_TABLE_H

#include <unordered_map>
#include <shared_mutex>
#include <cstdint>

// Проста двостороння таблиця відповідностей:
//   ID (unsigned int) <-> Value (std::string)

template <typename Value>
class IdValueTable {
public:
    IdValueTable()
        : nextId(1)
    {}

    IdValueTable(const IdValueTable&)            = delete;
    IdValueTable& operator=(const IdValueTable&) = delete;
    IdValueTable(IdValueTable&&)                 = delete;
    IdValueTable& operator=(IdValueTable&&)      = delete;

    unsigned int add(const Value& value) {
        std::unique_lock<std::shared_mutex> lock(mutex);

        auto it = valueToId.find(value);
        if (it != valueToId.end()) {
            return it->second;
        }

        unsigned int id = nextId++;
        valueToId[value] = id;
        idToValue[id]    = value;
        return id;
    }

    unsigned int add(Value&& value) {
        std::unique_lock<std::shared_mutex> lock(mutex);

        auto it = valueToId.find(value);
        if (it != valueToId.end()) {
            return it->second;
        }

        unsigned int id = nextId++;
        valueToId[value] = id;
        idToValue[id]    = std::move(value);
        return id;
    }

    bool getId(const Value& value, unsigned int& outId) const {
        std::shared_lock<std::shared_mutex> lock(mutex);

        auto it = valueToId.find(value);
        if (it == valueToId.end()) {
            return false;
        }

        outId = it->second;
        return true;
    }

    bool getValue(unsigned int id, Value& outValue) const {
        std::shared_lock<std::shared_mutex> lock(mutex);

        auto it = idToValue.find(id);
        if (it == idToValue.end()) {
            return false;
        }

        outValue = it->second;
        return true;
    }

    bool hasId(unsigned int id) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return idToValue.find(id) != idToValue.end();
    }

    bool hasValue(const Value& value) const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return valueToId.find(value) != valueToId.end();
    }

    bool removeById(unsigned int id) {
        std::unique_lock<std::shared_mutex> lock(mutex);

        auto itId = idToValue.find(id);
        if (itId == idToValue.end()) {
            return false;
        }

        Value value = itId->second;
        idToValue.erase(itId);

        auto itValue = valueToId.find(value);
        if (itValue != valueToId.end()) {
            valueToId.erase(itValue);
        }

        return true;
    }

    bool removeByValue(const Value& value) {
        std::unique_lock<std::shared_mutex> lock(mutex);

        auto itValue = valueToId.find(value);
        if (itValue == valueToId.end()) {
            return false;
        }

        unsigned int id = itValue->second;
        valueToId.erase(itValue);

        auto itId = idToValue.find(id);
        if (itId != idToValue.end()) {
            idToValue.erase(itId);
        }

        return true;
    }

    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex);
        idToValue.clear();
        valueToId.clear();
        nextId = 1;
    }

    unsigned int size() const {
        std::shared_lock<std::shared_mutex> lock(mutex);
        return static_cast<unsigned int>(idToValue.size());
    }

private:
    mutable std::shared_mutex mutex;
    std::unordered_map<unsigned int, Value> idToValue;
    std::unordered_map<Value, unsigned int> valueToId;
    unsigned int nextId;
};

#endif
