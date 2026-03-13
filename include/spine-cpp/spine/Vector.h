#ifndef SPINE_VECTOR_H
#define SPINE_VECTOR_H

#include <vector>
#include <cstddef>
#include <algorithm>

namespace spine {

template<typename T>
class Vector {
public:
    Vector() {}
    Vector(size_t capacity) { _data.reserve(capacity); }

    void add(const T &item) { _data.push_back(item); }
    void removeAt(size_t index) { _data.erase(_data.begin() + index); }
    void clear() { _data.clear(); }

    size_t size() const { return _data.size(); }
    bool isEmpty() const { return _data.empty(); }

    T &operator[](size_t index) { return _data[index]; }
    const T &operator[](size_t index) const { return _data[index]; }

    void setSize(size_t size, const T &defaultValue = T()) {
        _data.resize(size, defaultValue);
    }

    T *buffer() { return _data.data(); }
    const T *buffer() const { return _data.data(); }

    void ensureCapacity(size_t cap) {
        if (_data.capacity() < cap) _data.reserve(cap);
    }

    bool contains(const T &item) const {
        return std::find(_data.begin(), _data.end(), item) != _data.end();
    }

    int indexOf(const T &item) const {
        auto it = std::find(_data.begin(), _data.end(), item);
        return it != _data.end() ? static_cast<int>(it - _data.begin()) : -1;
    }

private:
    std::vector<T> _data;
};

} // namespace spine

#endif // SPINE_VECTOR_H
