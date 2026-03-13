#ifndef SPINE_COLOR_H
#define SPINE_COLOR_H

namespace spine {

struct Color {
    float r, g, b, a;

    Color() : r(1), g(1), b(1), a(1) {}
    Color(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}

    Color &set(float _r, float _g, float _b, float _a) {
        r = _r; g = _g; b = _b; a = _a;
        return *this;
    }

    Color &set(const Color &other) {
        r = other.r; g = other.g; b = other.b; a = other.a;
        return *this;
    }

    Color &add(const Color &other) {
        r += other.r; g += other.g; b += other.b; a += other.a;
        return *this;
    }

    Color &clamp() {
        if (r < 0) r = 0; else if (r > 1) r = 1;
        if (g < 0) g = 0; else if (g > 1) g = 1;
        if (b < 0) b = 0; else if (b > 1) b = 1;
        if (a < 0) a = 0; else if (a > 1) a = 1;
        return *this;
    }
};

} // namespace spine

#endif // SPINE_COLOR_H
