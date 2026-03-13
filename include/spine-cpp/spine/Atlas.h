#ifndef SPINE_ATLAS_H
#define SPINE_ATLAS_H

#include <spine/SpineString.h>
#include <spine/Vector.h>
#include <spine/TextureLoader.h>

namespace spine {

enum AtlasFormat {
    AtlasFormat_Alpha,
    AtlasFormat_Intensity,
    AtlasFormat_LuminanceAlpha,
    AtlasFormat_RGB565,
    AtlasFormat_RGBA4444,
    AtlasFormat_RGB888,
    AtlasFormat_RGBA8888
};

enum AtlasFilter {
    AtlasFilter_Unknown,
    AtlasFilter_Nearest,
    AtlasFilter_Linear,
    AtlasFilter_MipMap,
    AtlasFilter_MipMapNearestNearest,
    AtlasFilter_MipMapLinearNearest,
    AtlasFilter_MipMapNearestLinear,
    AtlasFilter_MipMapLinearLinear
};

enum AtlasWrap {
    AtlasWrap_MirroredRepeat,
    AtlasWrap_ClampToEdge,
    AtlasWrap_Repeat
};

class AtlasPage {
public:
    String name;
    AtlasFormat format;
    AtlasFilter minFilter;
    AtlasFilter magFilter;
    AtlasWrap uWrap;
    AtlasWrap vWrap;

    void *texHandle;   // opaque; set by TextureLoader
    int width;
    int height;

    AtlasPage() : format(AtlasFormat_RGBA8888),
        minFilter(AtlasFilter_Linear), magFilter(AtlasFilter_Linear),
        uWrap(AtlasWrap_ClampToEdge), vWrap(AtlasWrap_ClampToEdge),
        texHandle(nullptr), width(0), height(0) {}
};

class AtlasRegion {
public:
    String name;
    int x, y, width, height;
    float u, v, u2, v2;
    int offsetX, offsetY;
    int originalWidth, originalHeight;
    int index;
    bool rotate;
    int degrees;
    AtlasPage *page;

    AtlasRegion() : x(0), y(0), width(0), height(0),
        u(0), v(0), u2(0), v2(0),
        offsetX(0), offsetY(0),
        originalWidth(0), originalHeight(0),
        index(-1), rotate(false), degrees(0), page(nullptr) {}
};

class Atlas {
public:
    /// Construct atlas from a .atlas file.
    Atlas(const String &path, TextureLoader *textureLoader, bool createTexture = true);

    /// Construct atlas from in-memory data.
    Atlas(const char *data, int length, const String &dir, TextureLoader *textureLoader, bool createTexture = true);

    ~Atlas();

    /// Find a named region (attachment image).
    AtlasRegion *findRegion(const String &name);

    Vector<AtlasPage *> &getPages() { return _pages; }
    Vector<AtlasRegion *> &getRegions() { return _regions; }

private:
    void parse(const char *data, int length, const String &dir, bool createTexture);

    Vector<AtlasPage *>   _pages;
    Vector<AtlasRegion *> _regions;
    TextureLoader        *_textureLoader;
};

} // namespace spine

#endif // SPINE_ATLAS_H
