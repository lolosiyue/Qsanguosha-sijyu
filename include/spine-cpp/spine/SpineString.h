#ifndef SPINE_STRING_H
#define SPINE_STRING_H

#include <string>
#include <cstring>

namespace spine {

class String {
public:
    String() : _str() {}
    String(const char *s) : _str(s ? s : "") {}
    String(const std::string &s) : _str(s) {}
    String(const String &other) : _str(other._str) {}

    String &operator=(const String &other) { _str = other._str; return *this; }
    String &operator=(const char *s) { _str = s ? s : ""; return *this; }

    bool operator==(const String &other) const { return _str == other._str; }
    bool operator!=(const String &other) const { return _str != other._str; }
    bool operator<(const String &other) const { return _str < other._str; }

    const char *buffer() const { return _str.c_str(); }
    size_t length() const { return _str.length(); }
    bool isEmpty() const { return _str.empty(); }

    String &append(const char *s) { if (s) _str += s; return *this; }
    String &append(const String &s) { _str += s._str; return *this; }
    String &append(int val) { _str += std::to_string(val); return *this; }

    friend String operator+(const String &a, const String &b) {
        return String(a._str + b._str);
    }

private:
    std::string _str;
};

} // namespace spine

#endif // SPINE_STRING_H
